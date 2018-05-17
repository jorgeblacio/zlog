#include <atomic>
#include <random>
#include <cassert>
#include <semaphore.h>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <boost/program_options.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <signal.h>
#include <time.h>
#include <hdr_histogram.h>
#include <rados/librados.hpp>

namespace po = boost::program_options;

// this guards a hack that has nothing to do with the append benchmark. it can
// be deleted later, but there is just an emergency benchmark :)
#define SEQ_HACK 1

class rand_data_gen {
 public:
  rand_data_gen(size_t buf_size, size_t samp_size) :
    buf_size_(buf_size),
    dist_(0, buf_size_ - samp_size - 1)
  {}

  void generate() {
    std::uniform_int_distribution<uint64_t> d(
        std::numeric_limits<uint64_t>::min(),
        std::numeric_limits<uint64_t>::max());
    buf_.reserve(buf_size_);
    while (buf_.size() < buf_size_) {
      uint64_t val = d(gen_);
      buf_.append((const char *)&val, sizeof(val));
    }
    if (buf_.size() > buf_size_)
      buf_.resize(buf_size_);
  }

  inline const char *sample() {
    assert(!buf_.empty());
    return buf_.c_str() + dist_(gen_);
  }

 private:
  const size_t buf_size_;
  std::string buf_;
  std::default_random_engine gen_;
  std::uniform_int_distribution<size_t> dist_;
};

struct aio_state {
  librados::AioCompletion *c;
  uint64_t start_us;
  int omap_cmp_r = 0;
#ifdef SEQ_HACK
  uint64_t osize = 0;
#endif
};

static size_t entry_size;

static sem_t sem;
static std::mutex lock;
static std::condition_variable cond;

static volatile int stop = 0;
static void sig_handler(int sig) {
  if (sig == SIGALRM) {
    std::cout << "runtime exceeded" << std::endl;
  }
  stop = 1;
}

static struct hdr_histogram *histogram;
static std::mutex hist_lock;

static std::atomic<uint64_t> ops_done;

static inline std::string u64tostr(uint64_t value)
{
  std::stringstream ss;
  ss << std::setw(20) << std::setfill('0') << value;
  return ss.str();
}

static inline uint64_t __getns(clockid_t clock)
{
  struct timespec ts;
  clock_gettime(clock, &ts);
  return (((uint64_t)ts.tv_sec) * 1000000000ULL) + ts.tv_nsec;
}

static inline uint64_t getus()
{
  return __getns(CLOCK_MONOTONIC) / 1000;
}

static void handle_aio_append_cb(librados::completion_t cb, void *arg)
{
  aio_state *io = (aio_state*)arg;

  auto latency_us = getus() - io->start_us;
  ops_done.fetch_add(1);

  sem_post(&sem);

  if (io->omap_cmp_r != 0) {
    std::cout << "error: omap cmp r = " << std::endl;
    stop = 1;
    assert(0);
    exit(1);
  }

  if (io->c->get_return_value()) {
    std::cout << "error: io retval = "
      << io->c->get_return_value() << std::endl;
    assert(io->c->get_return_value() == 0);
    stop = 1;
    exit(1);
  }

#ifdef SEQ_HACK
  if (io->osize != 0) {
    std::cout << "error: seq hack broken" << std::endl;
    assert(0);
    stop = 1;
    exit(1);
  }
#endif

  io->c->release();
  delete io;

  std::lock_guard<std::mutex> lk(hist_lock);
  hdr_record_value(histogram, latency_us);
}

static void reporter(const std::string prefix)
{
  FILE *tpf = nullptr;
  if (!prefix.empty()) {
    std::stringstream fn;
    fn << prefix << ".iops.csv";
    tpf = fopen(fn.str().c_str(), "w");
    fprintf(tpf, "us,iops\n");
  }

  while (true) {
    std::unique_lock<std::mutex> lk(lock);

    auto ops_start = ops_done.load();
    auto start_us = getus();

    cond.wait_for(lk, std::chrono::seconds(1),
        [&] { return stop; });
    if (stop)
      break;

    auto end_us = getus();
    auto ops_end = ops_done.load();

    auto elapsed_us = end_us - start_us;

    auto iops = (double)((ops_end - ops_start) *
        1000000ULL) / (double)elapsed_us;

    std::cout << iops << std::endl;

    if (tpf) {
      fprintf(tpf, "%lu,%f\n", end_us, iops);
    }
  }

  if (tpf) {
    fclose(tpf);
  }

  std::lock_guard<std::mutex> lk(hist_lock);

  if (!prefix.empty()) {
    std::stringstream fn;
    fn << prefix << ".latency.csv";
    FILE *ltf = fopen(fn.str().c_str(), "w");
    hdr_percentiles_print(histogram,
        ltf, 5, 1.0, CSV);
    fclose(ltf);
  }

  hdr_percentiles_print(histogram,
      stdout, 1, 1.0, CLASSIC);
}

enum Store {
  OMAP,
  APPEND,
  XATTR,
  OMAP_APPEND,
  OMAP_APPEND_RMW,
  OMAP_FIXED_APPEND,
  XATTR_FIXED_APPEND,
  XATTR_FIXED_APPEND_RMW,
#ifdef SEQ_HACK
  PSEQ,
  VSEQ,
#endif
};

int main(int argc, char **argv)
{
  std::string pool;
  std::string logname;
  int runtime;
  int qdepth;
  int width;
  std::string prefix;
  double max_gbs;
  size_t entries_per_object;
  std::string store_name;
  size_t md_size;

  po::options_description opts("Benchmark options");
  opts.add_options()
    ("help,h", "show help message")
    ("logname,l", po::value<std::string>(&logname)->default_value(""), "Log name")
    ("runtime,r", po::value<int>(&runtime)->default_value(0), "runtime")
    ("store", po::value<std::string>(&store_name)->default_value("append"), "storage")
    ("pool,p", po::value<std::string>(&pool)->required(), "Pool name")
    ("width,w", po::value<int>(&width)->default_value(10), "stripe width")
    ("size,s", po::value<size_t>(&entry_size)->default_value(1024), "entry size")
    ("epo", po::value<size_t>(&entries_per_object)->default_value(1000), "entries per obj")
    ("qdepth,q", po::value<int>(&qdepth)->default_value(1), "aio queue depth")
    ("prefix", po::value<std::string>(&prefix)->default_value(""), "name prefix")
    ("max_gbs", po::value<double>(&max_gbs)->default_value(0.0), "max gbs to write")
    ("md_size", po::value<size_t>(&md_size)->default_value(50), "fixed md size")
  ;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, opts), vm);

  if (vm.count("help")) {
    std::cout << opts << std::endl;
    return 1;
  }

  po::notify(vm);

  if ((entry_size * entries_per_object) > (1ULL << 26)) {
    std::cerr << "target object size is too large" << std::endl;
    exit(1);
  }

  if (logname.empty()) {
    std::stringstream ss;
    auto uuid = boost::uuids::random_generator()();
    ss << uuid << ".log";
    logname = ss.str();
  }

  int nstripes = 500;

  Store store;
  if (store_name == "omap") {
    prefix = prefix + ".omap";
    store = OMAP;
  } else if (store_name == "append") {
    prefix = prefix + ".append";
    store = APPEND;
  } else if (store_name == "xattr") {
    prefix = prefix + ".xattr";
    store = XATTR;
  } else if (store_name == "omap_append") {
    prefix = prefix + ".omap_append";
    store = OMAP_APPEND;
  } else if (store_name == "omap_append_rmw") {
    prefix = prefix + ".omap_append_rmw";
    store = OMAP_APPEND_RMW;
  } else if (store_name == "omap_fixed_append") {
    prefix = prefix + ".omap_fixed_append";
    store = OMAP_FIXED_APPEND;
  } else if (store_name == "xattr_fixed_append") {
    prefix = prefix + ".xattr_fixed_append";
    store = XATTR_FIXED_APPEND;
  } else if (store_name == "xattr_fixed_append_rmw") {
    prefix = prefix + ".xattr_fixed_append_rmw";
    store = XATTR_FIXED_APPEND_RMW;
#ifdef SEQ_HACK
  } else if (store_name == "pseq") {
    prefix = prefix + ".pseq";
    store = PSEQ;
    width = 1;
    nstripes = 1;
  } else if (store_name == "vseq") {
    prefix = prefix + ".vseq";
    store = VSEQ;
    width = 1;
    nstripes = 1;
#endif
  } else {
    std::cerr << "invalid store" << std::endl;
    exit(1);
  }

  if (runtime == 0 && max_gbs == 0.0) {
    runtime = 30;
  }

  const uint64_t max_bytes = (1ULL << 30) * max_gbs;

  librados::Rados cluster;
  cluster.init(NULL);
  cluster.conf_read_file(NULL);
  int ret = cluster.connect();
  assert(ret == 0);
  (void)ret;

  librados::IoCtx ioctx;
  ret = cluster.ioctx_create(pool.c_str(), ioctx);
  assert(ret == 0);

  signal(SIGINT, sig_handler);
  signal(SIGALRM, sig_handler);
  alarm(runtime);

  rand_data_gen dgen(1ULL << 22, std::max(md_size, entry_size));
  dgen.generate();

  hdr_init(1,
      INT64_C(30000000),
      3, &histogram);

  std::vector<librados::AioCompletion*> creates;

  // pre-generate the object names for 200 stripes. mostly to avoid doing this
  // on demand in the dispatch loop. this is sufficient for our experiments.
  std::vector<std::vector<std::string>> oids;
  for (int i = 0; i < nstripes; i++) {
    std::vector<std::string> stripe_oids;
    for (int j = 0; j < width; j++) {
      std::stringstream oid;
      oid << logname << "." << i << "." << j;
      stripe_oids.push_back(oid.str());

      // create the object. this is useful because we use cmpxattr assertion to
      // simulate a rmw cycle. without the object existing, the assertion errors
      // out with enoent.
      librados::ObjectWriteOperation op;
#ifdef SEQ_HACK
      // basically cause we want to be able to use the same object names across
      // invocations of the benchmark..
      op.create(false);
#else
      op.create(true);
#endif
      auto c = librados::Rados::aio_create_completion();
      int ret = ioctx.aio_operate(oid.str(), c, &op);
      if (ret) {
        std::cerr << "failed create" << std::endl;
        exit(1);
      }
    }
    oids.emplace_back(stripe_oids);
  }

  for (auto c : creates) {
    c->wait_for_safe();
    c->release();
  }
  creates.clear();
  sleep(2);

  ops_done = 0;
  std::thread reporter_thread(reporter, prefix);
  sem_init(&sem, 0, qdepth);

  const size_t entries_per_stripe = width * entries_per_object;
  uint64_t pos = 0;
  uint64_t bytes_written = 0;
  for (;;) {
    sem_wait(&sem);
    if (stop || (max_bytes > 0 && bytes_written >= max_bytes)) {
      if (max_bytes > 0 && bytes_written >= max_bytes) {
        std::cout << "max bytes written reached" << std::endl;
      }
      sem_post(&sem);
      break;
    }


#ifdef SEQ_HACK
    switch (store) {
      case PSEQ:
        break;

      case VSEQ:
        {
          // aio reads seem to want the complete callback...
          aio_state *io = new aio_state;
          io->c = librados::Rados::aio_create_completion(io, handle_aio_append_cb, NULL);
          io->start_us = getus();
          int ret = ioctx.aio_stat(oids[0][0], io->c, &io->osize, NULL);
          if (ret) {
            std::cerr << "aio stat failed " << ret << std::endl;
            exit(1);
          }
          continue;
        }

      default:
        break;
    }
#endif

    aio_state *io = new aio_state;
    io->c = librados::Rados::aio_create_completion(io, NULL, handle_aio_append_cb);

    auto data = dgen.sample();
    ceph::bufferlist bl;
    bl.append(data, entry_size);

    ceph::bufferlist md_bl;
    md_bl.append(data, md_size);

    size_t obj_idx = pos % width;

    size_t stripe = pos / entries_per_stripe;
    if (stripe >= oids.size()) {
      std::cerr << "increase the number of stripes" << std::endl;
      exit(1);
    }

    auto oid = oids[stripe][obj_idx];

    switch (store) {
      case OMAP:
      {
        librados::ObjectWriteOperation op;
        auto key = u64tostr(pos);
        std::map<std::string, ceph::bufferlist> kvs;
        kvs.emplace(key, bl);
        op.omap_set(kvs);

        io->start_us = getus();
        int ret = ioctx.aio_operate(oid, io->c, &op);
        if (ret) {
          std::cerr << "aio operate failed " << ret << std::endl;
          exit(1);
        }
      }
      break;

      case APPEND:
      {
        io->start_us = getus();
        int ret = ioctx.aio_append(oid, io->c, bl, bl.length());
        if (ret) {
          std::cerr << "aio append failed " << ret << std::endl;
          exit(1);
        }
      }
      break;

      case OMAP_APPEND:
      {
        librados::ObjectWriteOperation op;

        // entry
        op.append(bl);

        // metadata. simulated (32 bit offset and length)
        // actual value doesn't matter
        ceph::bufferlist pos_bl;
        pos_bl.append((char*)&pos, sizeof(pos));
        auto key = u64tostr(pos);
        std::map<std::string, ceph::bufferlist> kvs;
        kvs.emplace(key, pos_bl);
        op.omap_set(kvs);

        io->start_us = getus();
        int ret = ioctx.aio_operate(oid, io->c, &op);
        if (ret) {
          std::cerr << "aio operate (omap+append) failed " << ret << std::endl;
          exit(1);
        }
      }
      break;

      case OMAP_APPEND_RMW:
      {
        librados::ObjectWriteOperation op;
        auto key = u64tostr(pos);

        // simulate rmw. if the key doesn't exist, then its treated like an
        // empty value. since we are always checking a new position that hasn't
        // been written yet in these experiments. then it should work fine.
        ceph::bufferlist cmp_bl;
        std::map<std::string, std::pair<ceph::bufferlist, int>> assertions;
        assertions[key] = std::make_pair(cmp_bl, LIBRADOS_CMPXATTR_OP_EQ);
        op.omap_cmp(assertions, &io->omap_cmp_r);

        // entry
        op.append(bl);

        // metadata. simulated (32 bit offset and length)
        // actual value doesn't matter
        ceph::bufferlist pos_bl;
        pos_bl.append((char*)&pos, sizeof(pos));
        std::map<std::string, ceph::bufferlist> kvs;
        kvs.emplace(key, pos_bl);
        op.omap_set(kvs);

        io->start_us = getus();
        int ret = ioctx.aio_operate(oid, io->c, &op);
        if (ret) {
          std::cerr << "aio operate (omap+append) failed " << ret << std::endl;
          exit(1);
        }
      }
      break;

      case OMAP_FIXED_APPEND:
      {
        librados::ObjectWriteOperation op;

        // entry
        op.append(bl);

        // metadata (simulated bitmap)
        std::map<std::string, ceph::bufferlist> kvs;
        kvs.emplace("index", md_bl);
        op.omap_set(kvs);

        io->start_us = getus();
        int ret = ioctx.aio_operate(oid, io->c, &op);
        if (ret) {
          std::cerr << "aio operate (omap+fixed+append) failed " << ret << std::endl;
          exit(1);
        }
      }
      break;

      case XATTR_FIXED_APPEND:
      {
        librados::ObjectWriteOperation op;

        // entry
        op.append(bl);

        // metadata (simulated bitmap)
        op.setxattr("index", md_bl);

        io->start_us = getus();
        int ret = ioctx.aio_operate(oid, io->c, &op);
        if (ret) {
          std::cerr << "aio operate (xattr+fixed+append) failed " << ret << std::endl;
          exit(1);
        }
      }
      break;

      case XATTR_FIXED_APPEND_RMW:
      {
        librados::ObjectWriteOperation op;

        // internally this will read the xattr and compare it to the zero byte.
        // this is used to simulate the read side of rmw. initially its empty,
        // so NE passes, then we set it to something non-zero so the test
        // continues to pass.
        ceph::bufferlist cmp_val;
        cmp_val.append(0);
        op.cmpxattr("index", LIBRADOS_CMPXATTR_OP_NE, cmp_val);

        // entry
        op.append(bl);

        // metadata (simulated bitmap) after simulated update
        op.setxattr("index", md_bl);

        io->start_us = getus();
        int ret = ioctx.aio_operate(oid, io->c, &op);
        if (ret) {
          std::cerr << "aio operate (xattr+fixed+append) failed " << ret << std::endl;
          exit(1);
        }
      }
      break;

      case XATTR:
      {
        auto key = u64tostr(pos);
        io->start_us = getus();
        int ret = ioctx.aio_setxattr(oid, io->c, key.c_str(), bl);
        if (ret) {
          std::cerr << "aio setxattr failed " << ret << std::endl;
          exit(1);
        }
      }
      break;

      default:
        std::cerr << "invalid store" << std::endl;
        exit(1);
    }

    pos++;
    bytes_written += entry_size;
  }

  while (true) {
    int val;
    sem_getvalue(&sem, &val);
    if (val == qdepth)
      break;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(100));
  }

  stop = 1;
  cond.notify_all();
  reporter_thread.join();
  hdr_close(histogram);

  ioctx.aio_flush();
  ioctx.close();
  cluster.shutdown();

  return 0;
}
