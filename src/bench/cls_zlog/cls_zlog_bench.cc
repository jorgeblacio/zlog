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
#include <rados/librados.hpp>
#include "libzlog/backend/cls_zlog_client.h"

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
    std::cerr << "error " << r << "/" << strerror(r) << std::endl; \
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

static int job_key;
static std::vector<std::string> oids;
static std::atomic<unsigned long> op_count;
static std::atomic<unsigned long> rd_count;
static std::atomic<unsigned long> position;
static std::atomic<unsigned long> bad_reads;

static volatile bool stop;
static void sigint_handler(int sig) {
  stop = true;
}

static void worker_thread()
{
  std::minstd_rand gen;

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

  librados::Rados cluster;
  cluster.init(NULL);
  cluster.conf_read_file(NULL);
  int ret = cluster.connect();
  checkret(ret, 0);

  librados::IoCtx ioctx;
  ret = cluster.ioctx_create(pool.c_str(), ioctx);
  checkret(ret, 0);

  const double read_scale = (double)read_pct / 100.0;

  while (!stop) {
    auto pos = position++;
    size_t oid_idx = pos % width;
    std::string& oid = oids[oid_idx];
    ceph::bufferlist bl;
    size_t buf_offset = rand_dist(gen);
    bl.append(rand_buf_raw + buf_offset, iosize);
    librados::ObjectWriteOperation op;
    zlog::cls_zlog_write(op, 0, pos, bl);
    ret = ioctx.operate(oid, &op);
    checkret(ret, 0);
    if (position >= (unsigned)fill_size)
      break;
  }

  if (stop) {
    ioctx.close();
    cluster.shutdown();
    return;
  }

  while (!stop) {
    if (rd_count >= (op_count * read_scale)) {
      auto pos = position++;
      size_t oid_idx = pos % width;
      std::string& oid = oids[oid_idx];
      ceph::bufferlist bl;
      size_t buf_offset = rand_dist(gen);
      bl.append(rand_buf_raw + buf_offset, iosize);
      librados::ObjectWriteOperation op;
      zlog::cls_zlog_write(op, 0, pos, bl);
      int ret = ioctx.operate(oid, &op);
      checkret(ret, 0);
    } else {
      std::uniform_int_distribution<> dis(0, position - nthreads);
      uint64_t random_pos = dis(gen);
      size_t oid_idx = random_pos % width;
      std::string& oid = oids[oid_idx];
      ceph::bufferlist bl;
      librados::ObjectReadOperation op;
      zlog::cls_zlog_read(op, 0, random_pos);
      int ret = ioctx.operate(oid, &op, &bl);
      if (ret == 3) {
        bad_reads++;
      } else {
        checkret(ret, 0);
      }
      rd_count++;
    }
    op_count++;
  }

  ioctx.close();
  cluster.shutdown();
}

static void report_thread()
{
  std::cout
    << "start_ns" << ","
    << "end_ns" << ","
    << "pos" << ","
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

    // don't print stats for partial period
    if (stop)
      break;

    std::cout
      << start_ns << ","
      << end_ns   << ","
      << position << ","
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

  // generate unique oids
  srand(time(NULL));
  job_key = rand();
  for (int i = 0; i < width; i++) {
    std::stringstream ss;
    ss << "oid." << job_key << "." << i;
    oids.push_back(ss.str());
  }

  op_count = 0;
  rd_count = 0;
  position = 0;
  bad_reads = 0;
  fill_size = std::max(fill_size, nthreads);

  stop = false;
  signal(SIGINT, sigint_handler);

  // start workers
  std::vector<std::thread> threads;
  for (int i = 0; i < nthreads; i++) {
    threads.push_back(std::thread(worker_thread));
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
