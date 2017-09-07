#include <iostream>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <fstream>
#include <random>
#include <thread>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <rados/librados.hpp>
#include <boost/program_options.hpp>

#define CLS_ZLOG_V2 1

#ifdef CLS_ZLOG_V1
#include "libzlog/backend/cls_zlog_client.h"
#endif

#ifdef CLS_ZLOG_V2
#include "libzlog/storage/ceph/cls_zlog_client.h"
#endif

namespace po = boost::program_options;

static librados::IoCtx ioctx;
static const char *rand_buf_raw;

// grab random slices from the random bytes
static std::default_random_engine generator;
static std::uniform_int_distribution<int> rand_dist;

static std::atomic<uint64_t> io_latency_ns;
static std::atomic<uint64_t> ios_completed;
static uint64_t outstanding_ios;
static std::condition_variable io_cond;
static std::mutex io_lock;
  
static std::string pool;
static int runtime;
static int qdepth;
static int entry_size;
static uint32_t width;
static int stats_window;
static uint32_t v2_num_stripes;
static std::string tp_log_fn;

#ifdef CLS_ZLOG_V2
static uint64_t objectno(uint64_t position)
{
  const uint64_t stripe_num = position / width;
  const uint64_t stripepos = position % width;
  const uint64_t objectsetno = stripe_num / v2_num_stripes;
  const uint64_t objectno = objectsetno * width + stripepos;
  return objectno;
}
#endif

static volatile int stop;
static void sigint_handler(int sig) {
  stop = 1;
}

static inline uint64_t __getns(clockid_t clock)
{
  struct timespec ts;
  int ret = clock_gettime(clock, &ts);
  assert(ret == 0);
  return (((uint64_t)ts.tv_sec) * 1000000000ULL) + ts.tv_nsec;
}

static inline uint64_t getns()
{
  return __getns(CLOCK_MONOTONIC);
}

struct aio_state {
  uint64_t startns;
  librados::AioCompletion *c;
};

static void iocb(librados::completion_t cb, void *arg)
{
  io_lock.lock();
  outstanding_ios--;
  io_lock.unlock();
  io_cond.notify_one();

  aio_state *io = (aio_state*)arg;
  uint64_t latns = getns() - io->startns;
  int ret = io->c->get_return_value();
  if (ret) {
    std::cerr << "error: io retval = " << ret << std::endl << std::flush;
    stop = 1;
    exit(1);
  }
  io->c->release();
  delete io;

  ios_completed++;
  io_latency_ns += latns;
}

static void report(int stats_window, const std::string tp_log_fn)
{
  ios_completed = 0;
  io_latency_ns = 0;
  uint64_t window_start = getns();

  // open the output stream
  int fd = -1;
  if (!tp_log_fn.empty()) {
    fd = open(tp_log_fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0440);
    assert(fd != -1);
  }

  while (!stop) {
    sleep(stats_window);

    uint64_t latns_in_window = io_latency_ns.exchange(0);
    uint64_t ios_completed_in_window = ios_completed.exchange(0);
    uint64_t window_end = getns();
    uint64_t window_dur = window_end - window_start;
    window_start = window_end;

    double iops = (double)(ios_completed_in_window *
        1000000000ULL) / (double)window_dur;

    uint64_t avg_io_lat = latns_in_window / ios_completed_in_window;

    time_t now = time(NULL);

    if (stop)
      break;

    if (fd != -1) {
      dprintf(fd, "time %llu iops %llu avglatns %llu\n",
          (unsigned long long)now,
          (unsigned long long)iops,
          (unsigned long long)avg_io_lat);
    }

    std::cout << "time " << now << " iops " << (int)iops <<
      " avglatns " << avg_io_lat << std::endl;
  }

  if (fd != -1) {
    fsync(fd);
    close(fd);
  }
}

int main(int argc, char **argv)
{
  po::options_description general_opts("General options");
  general_opts.add_options()
    ("help,h", "show help message")
    ("pool,p", po::value<std::string>(&pool)->required(), "Pool name")
    ("runtime,r", po::value<int>(&runtime)->default_value(0), "runtime")
    ("qdepth,q", po::value<int>(&qdepth)->default_value(1), "aio queue depth")
    ("esize,e", po::value<int>(&entry_size)->default_value(0), "entry size")
    ("window", po::value<int>(&stats_window)->default_value(2), "stats collection period")
    ("iops-log", po::value<std::string>(&tp_log_fn)->default_value(""), "throughput log file")
    ("width", po::value<uint32_t>(&width)->default_value(1), "stripe width")
  ;

  po::options_description bev2_opts("Backend v2 options");
  bev2_opts.add_options()
    ("v2_num_stripes", po::value<uint32_t>(&v2_num_stripes)->default_value(1), "num_stripes")
  ;

  po::options_description all_opts("Allowed options");
  all_opts.add(general_opts).add(bev2_opts);

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, all_opts), vm);

  if (vm.count("help")) {
    std::cout << all_opts << std::endl;
    return 1;
  }

  po::notify(vm);

  // create random data to use for payloads
  size_t rand_buf_size = 1ULL<<23;
  std::string rand_buf;
  rand_buf.reserve(rand_buf_size);
  std::ifstream ifs("/dev/urandom", std::ios::binary | std::ios::in);
  std::copy_n(std::istreambuf_iterator<char>(ifs),
      rand_buf_size, std::back_inserter(rand_buf));
  rand_buf_raw = rand_buf.c_str();

  // distribution for grabbing random bytes
  rand_dist = std::uniform_int_distribution<int>(0,
      rand_buf_size - entry_size - 1);

  // connect to rados
  librados::Rados cluster;
  cluster.init(NULL);
  cluster.conf_read_file(NULL);
  int ret = cluster.connect();
  assert(ret == 0);

  // open pool i/o context
  ret = cluster.ioctx_create(pool.c_str(), ioctx);
  assert(ret == 0);

  std::thread report_runner(report, stats_window, tp_log_fn);
  std::thread stop_thread;
  if (runtime > 0) {
    stop_thread = std::thread([&](){
      int left = runtime;
      while (!stop && left) {
        left = sleep(left);
      }
      stop = 1;
      io_cond.notify_one();
    });
  }

  srand(time(NULL));
  std::stringstream lnss;
  lnss << "cls_zlog_bench." << rand();
  std::string logname = lnss.str();
  std::vector<std::string> objnames;
  for (unsigned i = 0; i < width; i++) {
    std::stringstream ss;
    ss << logname << "." << i;
    objnames.push_back(ss.str());
  }

  int x = 0;
  std::vector<std::string> v2_objs;
  uint64_t maxpos = (uint64_t)runtime * (uint64_t)50000;
  std::cout << "initializing: " << maxpos << std::endl;
  for (uint64_t curpos = 0; curpos < maxpos; curpos++) {
    const int ono = objectno(curpos);
    if (v2_objs.size() <= (unsigned)ono) {
      v2_objs.resize(ono+1);
    }
    if (v2_objs[ono].size())
      continue;

    std::stringstream ss;
    ss << logname << "." << ono;
    auto oid = ss.str();

    librados::ObjectWriteOperation op;
    auto estmp = entry_size == 0 ? 1 : entry_size;
    cls_zlog_client::init(op, estmp, width, v2_num_stripes, ono);
    ret = ioctx.operate(oid, &op);
    assert(ret == 0);

    v2_objs[ono] = oid;
    x++;
  }
  std::cout << "initialized " << x << " objs" << std::endl;

  uint64_t pos = 0;

  stop = 0;
  signal(SIGINT, sigint_handler);

  outstanding_ios = 0;
  std::unique_lock<std::mutex> lock(io_lock);
  for (;;) {
    while (outstanding_ios < (unsigned)qdepth) {
      lock.unlock();

      auto io = new aio_state;
      io->c = librados::Rados::aio_create_completion(io, NULL, iocb);

      const size_t buf_offset = rand_dist(generator);
      ceph::bufferlist data = ceph::bufferlist::static_from_mem(
          (char*)(rand_buf_raw + buf_offset), entry_size);

#ifdef CLS_ZLOG_V1
      const int objectno = pos % width;
      std::string& oid = objnames[objectno];

      librados::ObjectWriteOperation op;
      zlog::cls_zlog_write(op, 1, pos, data);
#endif

#ifdef CLS_ZLOG_V2
      const int ono = objectno(pos);
      std::string& oid = v2_objs[ono];

      librados::ObjectWriteOperation op;
      cls_zlog_client::write(op, pos, data);
#endif

      io->startns = getns();
      ret = ioctx.aio_operate(oid, io->c, &op);
      assert(ret == 0);

      pos++;

      lock.lock();
      outstanding_ios++;
    }

    io_cond.wait(lock, [&]{ return outstanding_ios < (unsigned)qdepth || stop; });

    if (stop)
      break;
  }

  for (;;) {
    std::cout << "draining ios: " << outstanding_ios << " remaining" << std::endl;
    if (outstanding_ios == 0)
      break;
    lock.unlock();
    sleep(1);
    lock.lock();
  }

  report_runner.join();
  if (stop_thread.joinable())
    stop_thread.join();

  ioctx.close();
  cluster.shutdown();

  return 0;
}
