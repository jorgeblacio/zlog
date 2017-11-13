#include "core/print.hh"
#include "core/reactor.hh"
#include "core/app-template.hh"
#include "core/future-util.hh"
#include "core/distributed.hh"
#include "core/semaphore.hh"
#include "core/future-util.hh"
#include "util/log.hh"
#include <chrono>
#include <array>

#undef SEQ_BASIC
#define SEQ_BASIC 1

namespace po = boost::program_options;

class connection {
 public:
  seastar::connected_socket fd;
  seastar::input_stream<char> in;
  seastar::output_stream<char> out;
  bool& client_done;
  uint64_t& ops;

  connection(seastar::connected_socket&& fd, bool& client_done, uint64_t& ops) :
    fd(std::move(fd)),
    in(this->fd.input()),
    out(this->fd.output()),
    client_done(client_done),
    ops(ops)
  {}

  seastar::future<> launch_workload() {
    return seastar::do_until([this] { return client_done; }, [this] {
      return out.write(seastar::sstring{"a", 1}).then([this] {
        return out.flush();
      }).then([this] {
#ifdef SEQ_BASIC
        return in.read_exactly(sizeof(uint64_t)).then([this] (auto&& data) {
#else
        return in.read_exactly(1).then([this] (auto&& data) {
#endif
          if (data.empty()) {
            return seastar::make_exception_future(std::runtime_error("no data"));
          }
          ops++;
          return seastar::make_ready_future<>();
        });
      });
    });
  }
};

class client {
 public:
  unsigned dur;
  seastar::ipv4_addr server;
  bool timer_done;
  seastar::timer<> run_timer;
  unsigned num_conn;
  std::vector<seastar::connected_socket> sockets;
  uint64_t ops = 0;

  client(unsigned dur, unsigned total_conn, seastar::ipv4_addr server) :
    dur(dur),
    server(server),
    timer_done(false),
    run_timer([this] { timer_done = true; })
  {
    num_conn = total_conn / seastar::smp::count;
    auto rem = total_conn % seastar::smp::count;
    if (rem && seastar::engine().cpu_id() < rem) {
      num_conn++;
    }
  }

  seastar::future<> connect() {
    return seastar::parallel_for_each(boost::irange(0u, num_conn), [this] (auto i) {
      auto addr = seastar::make_ipv4_address(server);
      return seastar::engine().net().connect(addr).then([this] (seastar::connected_socket fd) {
        sockets.push_back(std::move(fd));
      });
    });
  }

  seastar::future<> run() {
    run_timer.arm(std::chrono::seconds(dur));
    return seastar::parallel_for_each(std::begin(sockets), std::end(sockets), [this] (auto& fd) {
      return seastar::do_with(connection(std::move(fd), this->timer_done, ops), [this] (auto& conn) {
        return conn.launch_workload();
      });
    });
  }

  uint64_t total_ops() {
    return ops;
  }

  seastar::future<> stop() {
    return seastar::make_ready_future();
  }
};

int main(int ac, char **av)
{
  seastar::sharded<client> sharded_client;

  seastar::app_template app;
  app.add_options()
    ("server", po::value<std::string>()->required(), "server address")
    ("conn", po::value<unsigned>()->required(), "total connections")
    ("dur", po::value<unsigned>()->required(), "duration of the test in seconds)");

  auto start = seastar::steady_clock_type::now();
  return app.run(ac, av, [&] () -> seastar::future<int> {
    seastar::engine().at_exit([&sharded_client] { return sharded_client.stop(); });

    auto& config = app.configuration();
    auto server = config["server"].as<std::string>();
    auto conn = config["conn"].as<unsigned>();
    auto dur = config["dur"].as<unsigned>();

    auto addr = seastar::ipv4_addr{server};
    return sharded_client.start(dur, conn, addr).then([&] {
      return sharded_client.invoke_on_all(&client::connect);
    }).then([&] {
      start = seastar::steady_clock_type::now();
      return sharded_client.invoke_on_all(&client::run);
    }).then([&] {
      return sharded_client.map_reduce(seastar::adder<uint64_t>(), &client::total_ops);
    }).then([&] (uint64_t total_ops) {
      auto elapsed = seastar::steady_clock_type::now() - start;
      auto secs = static_cast<double>(elapsed.count() / 1000000000.0);
      auto rate = (double)total_ops / secs;
      std::cout << "rate " << (uint64_t)rate << std::endl;
    }).then([&] {
      return sharded_client.stop();
    }).then([&] {
      return seastar::make_ready_future<int>(0);
    });
  });
}
