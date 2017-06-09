#include <atomic>
#include <cassert>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <signal.h>
#include <boost/program_options.hpp>
#include "zlog/log.h"
#define USE_CEPH_BACKEND
#ifdef USE_RAM_BACKEND
# include "zlog/backend/ram.h"
#elif defined(USE_LMDB_BACKEND)
# include "zlog/backend/lmdb.h"
#elif defined(USE_CEPH_BACKEND)
# include "include/zlog/backend/ceph.h"
#else
# error "no backend configured"
#endif
#include "zlog/backend/fakeseqr.h"

namespace po = boost::program_options;

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

#define checkret(r,v) do { \
  if (r != v) { \
    std::cerr << "error " << r << "/" << strerror(-r) << std::endl; \
    assert(0); \
    exit(1); \
  } } while (0)

// params
static std::string pool;
static int read_pct;
static int width;
static int nthreads;
static int fill_size;
static size_t iosize;

// stats
static std::atomic<unsigned long> op_count;
static std::atomic<unsigned long> rd_count;
static std::atomic<unsigned long> bad_reads;

// control
static volatile bool stop;
static void sigint_handler(int sig) {
  stop = true;
}

static void worker_thread(zlog::Log *log)
{
  std::random_device rd;
  std::mt19937 gen(rd());

  // create random data to use for payloads
  const size_t rand_buf_size = 1ULL<<18;
  std::string rand_buf;
  rand_buf.reserve(rand_buf_size);
  std::ifstream ifs("/dev/urandom", std::ios::binary | std::ios::in);
  std::copy_n(std::istreambuf_iterator<char>(ifs),
      rand_buf_size, std::back_inserter(rand_buf));
  ifs.close();
  const char *rand_buf_raw = rand_buf.c_str();

  // grab random slices from the random bytes
  std::uniform_int_distribution<int> rand_dist;
  rand_dist = std::uniform_int_distribution<int>(0,
      rand_buf_size - iosize - 1);

  // position defines the upper bound on the range used to generate reads.
  // this is updated after each write we do which will be the global tail.
  uint64_t position;

  // pre-fill log
  while (!stop) {
    size_t buf_offset = rand_dist(gen);
    int ret = log->Append(Slice(rand_buf_raw + buf_offset, iosize), &position);
    checkret(ret, 0);
    if (position >= (unsigned)fill_size)
      break;
  }

  if (stop)
    return;

  // main workload generator
  const double read_scale = (double)read_pct / 100.0;
  while (!stop) {
    if (rd_count >= (op_count * read_scale)) {
      size_t buf_offset = rand_dist(gen);
      int ret = log->Append(Slice(rand_buf_raw + buf_offset, iosize), &position);
      checkret(ret, 0);
    } else {
      std::string data;
      std::uniform_int_distribution<> dis(0, position - nthreads);
      uint64_t random_pos = dis(gen);
      int ret = log->Read(random_pos, &data);
      if (ret == -ENODEV) {
        bad_reads++;
      } else {
        checkret(ret, 0);
      }
      rd_count++;
    }
    op_count++;
  }
}

static void report_thread()
{
  std::cout
    << "start_ns" << ","
    << "end_ns" << ","
    << "total_ops" << ","
    << "total_rds" << ","
    << "curr_ops" << ","
    << "curr_rds" << ","
    << "bad_rds" << std::endl;

  while (!stop) {
    auto start_ns = getns();
    auto start_op_count = op_count.load();
    auto start_rd_count = rd_count.load();

    sleep(2);

    auto end_ns = getns();
    auto curr_op_count = op_count.load();
    auto curr_rd_count = rd_count.load();
    auto end_op_count = curr_op_count;
    auto end_rd_count = curr_rd_count;

    auto total_op_count = end_op_count - start_op_count;
    auto total_rd_count = end_rd_count - start_rd_count;

    // dont print partial period
    if (stop)
      break;

    std::cout
      << start_ns << ","
      << end_ns   << ","
      << total_op_count << ","
      << total_rd_count << ","
      << curr_op_count << ","
      << curr_rd_count << ","
      << bad_reads << std::endl;
  }
}

int main(int argc, char **argv)
{
  int runtime;

  po::options_description gen_opts("Benchmark options");
  gen_opts.add_options()
    ("help,h", "show help message")
    ("pool", po::value<std::string>(&pool)->required(), "pool name")
    ("read-pct", po::value<int>(&read_pct)->default_value(0), "read percentage")
    ("width", po::value<int>(&width)->default_value(1), "stripe width")
    ("nthreads", po::value<int>(&nthreads)->default_value(1), "num threads")
    ("fill-size", po::value<int>(&fill_size)->default_value(0), "fill size")
    ("runtime", po::value<int>(&runtime)->default_value(0), "runtime (sec)")
    ("iosize", po::value<size_t>(&iosize)->default_value(0), "io size")
    ;

  po::options_description all_opts("Allowed options");
  all_opts.add(gen_opts);

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, all_opts), vm);

  if (vm.count("help")) {
    std::cout << all_opts << std::endl;
    return 1;
  }

  po::notify(vm);

  assert(runtime >= 0);
  assert(fill_size >= 0);
  assert(nthreads > 0);
  assert(width > 0);
  assert(0 <= read_pct && read_pct <= 100);

  auto client = new FakeSeqrClient();

  int ret;
#ifdef USE_RAM_BACKEND
  auto be = new RAMBackend();
#elif defined(USE_LMDB_BACKEND)
  auto be = new LMDBBackend();
  be->Init(pool, true);
#elif defined(USE_CEPH_BACKEND)
  librados::Rados cluster;
  cluster.init(NULL);
  cluster.conf_read_file(NULL);
  ret = cluster.connect();
  checkret(ret, 0);

  librados::IoCtx ioctx;
  ret = cluster.ioctx_create(pool.c_str(), ioctx);
  checkret(ret, 0);

  auto be = new CephBackend(&ioctx);
#endif

  srand(time(NULL));
  std::stringstream ss;
  ss << "log." << rand();

  zlog::Log *log;
  ret = zlog::Log::CreateWithStripeWidth(be, ss.str(), client, width, &log);
  checkret(ret, 0);

  // IMPORTANT: we need to run checktail here to initialize the indexes in the
  // fake sequencer because there is no locking. this (and any other reconfig)
  // needs to be done before starting multiple threads.
  uint64_t position;
  ret = log->CheckTail(&position);
  checkret(ret, 0);

  // TODO: generate unique log name

  op_count = 0;
  rd_count = 0;
  bad_reads = 0;
  fill_size = std::max(fill_size, nthreads);

  stop = false;
  signal(SIGINT, sigint_handler);

  // start workers
  std::vector<std::thread> threads;
  for (int i = 0; i < nthreads; i++) {
    threads.push_back(std::thread(worker_thread, log));
  }

  // start stats worker
  std::thread reporter(report_thread);

  if (runtime > 0) {
    // wait for initial fill to start timer
    while (!stop) {
      sleep(1);
      if (op_count > 0)
        break;
    }
    // sleep for runtime seconds
    int left = runtime;
    while (!stop) {
      left = sleep(left);
      if (left == 0)
        break;
    }
    stop = true;
  }

  for (auto& t : threads) {
    t.join();
  }
  reporter.join();

  return 0;
}
