#include <atomic>
#include <random>
#include <thread>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <signal.h>
#include <boost/program_options.hpp>
#include "zlog/log.h"
#include "zlog/backend/lmdb.h"
#include "include/zlog/backend/ceph.h"
#include "zlog/backend/fakeseqr.h"

namespace po = boost::program_options;

#define checkret(r,v) do { \
  if (r != v) { \
    std::cerr << "error " << r << "/" << strerror(-r) << std::endl; \
    assert(0); \
    exit(1); \
  } } while (0)

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

static volatile bool stop = false;
static void sigint_handler(int sig)
{
  stop = true;
}

static int count = 0;
static std::vector<std::pair<uint64_t,uint64_t>> trace;

static void worker(zlog::Log *log, size_t entry_size, bool dotrace)
{
  // create random data to use for payloads
  std::random_device rd;
  std::mt19937 gen(rd());

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
      rand_buf_size - entry_size - 1);

  while (!stop) {
    uint64_t position;
    size_t buf_offset = rand_dist(gen);
    uint64_t start = __getns(CLOCK_REALTIME); // may need to compare across machines
    int ret = log->Append(Slice(rand_buf_raw + buf_offset, entry_size), &position);
    std::cout << position << std::endl;
    uint64_t end = __getns(CLOCK_REALTIME);
    checkret(ret, 0);
    if (dotrace) {
      trace.emplace_back(start, end);
    }
    count++;
  }
}

static void monitor()
{
  while (!stop) {
    std::cerr << "progress: " << count << std::endl;
    sleep(5);
  }
}

int main(int argc, char **argv)
{
  int runtime;
  int width;
  size_t entry_size;
  std::string pool;
  std::string tracefn;
  std::string backend;
  std::string lmdbdir;

  po::options_description gen_opts("Benchmark options");
  gen_opts.add_options()
    ("help,h", "show help message")
    ("pool", po::value<std::string>(&pool)->default_value("zlog"), "pool name")
    ("width", po::value<int>(&width)->default_value(1), "stripe width")
    ("runtime", po::value<int>(&runtime)->default_value(0), "runtime (sec)")
    ("entry_size", po::value<size_t>(&entry_size)->default_value(0), "entry size")
    ("trace", po::value<std::string>(&tracefn)->default_value(""), "trace file")
    ("backend", po::value<std::string>(&backend)->default_value("ceph"), "backend")
    ("lmdbdir", po::value<std::string>(&lmdbdir)->default_value(""), "lmdb dir")
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

  assert(pool.size() > 0);
  assert(width > 0);
  assert(runtime >= 0);
  assert(backend == "ceph" ||
         backend == "lmdb");

  bool is_tracing = tracefn != "";
  if (is_tracing)
    trace.reserve(8000000);

  // open trace file
  int trace_fd;
  bool close_trace_fd = false;
  if (is_tracing) {
    if (tracefn == "-")
      trace_fd = fileno(stdout);
    else {
      trace_fd = open(tracefn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0440);
      assert(trace_fd != -1);
      close_trace_fd = true;
    }
  }

  signal(SIGINT, sigint_handler);

  // ceph backend bits
  librados::Rados cluster;
  librados::IoCtx ioctx;

  // build backend
  Backend *be;
  if (backend == "lmdb") {
    auto tmp = new LMDBBackend();
    tmp->Init(lmdbdir, true);
    be = tmp;

  } else if (backend == "ceph") {
    cluster.init(NULL);
    cluster.conf_read_file(NULL);

    int ret = cluster.connect();
    checkret(ret, 0);

    ret = cluster.ioctx_create(pool.c_str(), ioctx);
    checkret(ret, 0);

    be = new CephBackend(&ioctx);
  } else {
    assert(0);
  }

  // name of log
  srand(time(NULL));
  std::stringstream logname;
  logname << "log." << rand();

  // open log with fake sequencer
  zlog::Log *log;
	auto client = new FakeSeqrClient();
  int ret = zlog::Log::CreateWithStripeWidth(be, logname.str(), client, width, &log);
  checkret(ret, 0);

  // IMPORTANT: we need to run checktail here to initialize the indexes in the
  // fake sequencer because there is no locking. this (and any other reconfig)
  // needs to be done before starting multiple threads.
  uint64_t position;
  ret = log->CheckTail(&position);
  checkret(ret, 0);

  // start workers
  std::vector<std::thread> threads;
  threads.push_back(std::thread(worker, log, entry_size, is_tracing));
  threads.push_back(std::thread(monitor));

  if (runtime) {
    auto left = runtime;
    while (true) {
      left = sleep(left);
      if (left == 0)
        stop = true;
      if (stop)
        break;
    }
  }

  for (auto& thread : threads)
    thread.join();

  // dump trace
  if (is_tracing) {
    dprintf(trace_fd, "start,end\n");
    for (auto& event : trace) {
      dprintf(trace_fd, "%llu,%llu\n",
          (unsigned long long)event.first,
          (unsigned long long)event.second);
    }

    fsync(trace_fd);
    if (close_trace_fd)
      close(trace_fd);
  }

	return 0;
}
