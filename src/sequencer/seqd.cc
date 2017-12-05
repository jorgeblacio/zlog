#include <core/app-template.hh>
#include <core/sharded.hh>
#include <net/api.hh>
#include <core/reactor.hh>

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

class counter_state {
 public:
  counter_state() :
    counter(0) {
  }

  seastar::future<> stop() {
    return seastar::make_ready_future<>();
  }

  uint64_t next() {
    return counter++;
  }

  uint64_t curr() {
    return counter;
  }

  uint64_t counter;
};

class connection {
 public:
  connection(seastar::connected_socket&& socket, seastar::socket_address addr,
      uint64_t& counter, seastar::sharded<counter_state>& counters) :
    socket(std::move(socket)),
    addr(addr),
    in(this->socket.input()),
    out(this->socket.output()),
    counter(counter),
    counters(counters)
  {}

  seastar::future<> process() {
    return in.read_exactly(1).then([this] (auto&& data) mutable {
      if (!data.empty()) {
	auto cpu = seastar::smp::count / 2;
        return counters.invoke_on(cpu, &counter_state::next).then([&] (uint64_t c) {
          counter++;
          return out.write(seastar::sstring{(char*)&c, sizeof(c)});
        });
      }
      return in.close();
    });
  }

  seastar::connected_socket socket;
  seastar::socket_address addr;
  seastar::input_stream<char> in;
  seastar::output_stream<char> out;
  uint64_t& counter;
  seastar::sharded<counter_state>& counters;
};

class server {
 public:
  server(uint16_t port, seastar::sharded<counter_state>& counters) :
    port(port),
    counters(counters)
  {
    startns = getns();
    counter = 0;
  }

  void start() {
    seastar::listen_options lopts;
    lopts.reuse_address = true;

    listener = seastar::engine().listen(
      seastar::make_ipv4_address({port}), lopts);

    seastar::keep_doing([this] {
      return listener.accept().then([this] (seastar::connected_socket fd,
            seastar::socket_address addr) mutable {
        seastar::do_with(connection(std::move(fd), addr, counter, counters), [] (auto& conn) {
          return seastar::do_until([&conn] { return conn.in.eof(); }, [&conn] {
            return conn.process().then([&conn] {
              return conn.out.flush();
            });
          }).finally([&conn] {
            return conn.out.close();
          });
        });
      });
    }).or_terminate();
  }

  uint64_t rate() {
    uint64_t count = counter;
    uint64_t durns = getns() - startns;
    startns = getns();
    counter = 0;;
    return (count * (uint64_t)1000000000) / durns;
  }

  seastar::future<> stop() {
    return seastar::make_ready_future<>();
  }

  seastar::server_socket listener;
  uint16_t port;
  uint64_t counter;
  uint64_t startns;
  seastar::sharded<counter_state>& counters;
};

class stats_printer {
 public:
  stats_printer(seastar::sharded<server>& s) :
    s(s)
  {}

  void start() {
    t.set_callback([this] {
      s.map_reduce(seastar::adder<uint64_t>(), &server::rate)
      .then([] (uint64_t rate) {
        std::cout << "rate " << rate << std::endl;
      });
    });
    t.arm_periodic(std::chrono::seconds(1));
  }

  seastar::future<> stop() {
    return seastar::make_ready_future<>();
  }

  seastar::timer<> t;
  seastar::sharded<server>& s;
};

int main(int argc, char **argv)
{
  seastar::sharded<counter_state> counters;
  seastar::sharded<server> sharded_server;
  stats_printer stats(sharded_server);

  seastar::app_template app;
  try {
    return app.run_deprecated(argc, argv, [&] {
      seastar::engine().at_exit([&sharded_server] { return sharded_server.stop(); });
      seastar::engine().at_exit([&counters] { return counters.stop(); });
      return counters.start().then([&] {
        return sharded_server.start(5678, std::ref(counters));
      }).then([&] {
        return sharded_server.invoke_on_all(&server::start);
      }).then([&] {
        stats.start();
      });
    });
  } catch (...) {
    std::cerr << std::current_exception() << std::endl;
    return 1;
  }
}
