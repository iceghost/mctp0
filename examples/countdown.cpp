#include <bits/types/sigset_t.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include <cassert>
#include <csignal>
#include <exec/sequence/ignore_all_values.hpp>
#include <exec/sequence/transform_each.hpp>
#include <exec/variant_sender.hpp>
#include <iostream>
#include <ostream>
#include <stdexec/execution.hpp>
#include <string>
#include <system_error>

#include "async.hpp"

using namespace std::literals;

auto listen_stdin(stdexec::scheduler auto sch) -> stdexec::sender auto {
  return stdexec::let_value(stdexec::just(), [time = 0, sch] mutable noexcept {
    return schedule_every_epoll(sch, STDIN_FILENO, EPOLLIN) |
           exec::transform_each(
               stdexec::then([&time](unsigned int) mutable noexcept {
                 std::string s;
                 std::getline(std::cin, s);
                 std::println(std::cout, "{}", s);
                 std::println(std::cout, "{}...", time++);
               })) |
           exec::ignore_all_values() | stdexec::upon_stopped([]() noexcept {
             std::println(std::cout, "stopping counting");
           });
  });
}

auto watch_signal(stdexec::scheduler auto sch) -> stdexec::sender auto {
  return schedule_at_signal(sch, SIGINT) |
         stdexec::let_value([](auto) noexcept {
           std::println(std::cout, "SIGINT");
           return stdexec::just_stopped();
         });
}

struct receiver {
  using receiver_concept = stdexec::receiver_t;

  struct env {
    [[nodiscard]]
    auto query(stdexec::get_stop_token_t) const noexcept {
      return stop_token{event};
    }

    sd_event *event;
  };

  void set_value() noexcept {}

  void set_error(std::error_code code) noexcept {
    std::println(std::cerr, "error: {} ({})", code.message(), code.value());
    sd_event_exit(event, 0);
  }

  void set_stopped() noexcept {
    // oops
    sd_event_exit(event, 0);
  };

  [[nodiscard]]
  auto get_env() const noexcept {
    return env{event};
  }

  sd_event *event;
};

auto main() -> int {
  [[gnu::cleanup(sd_event_unrefp)]]
  sd_event *event{};

  sigset_t s{};
  sigaddset(&s, SIGINT);
  sigprocmask(SIG_BLOCK, &s, nullptr);

  assert(sd_event_default(&event) >= 0);

  auto op1 = stdexec::connect(listen_stdin(scheduler{event}), receiver{event});
  stdexec::start(op1);

  auto op2 = stdexec::connect(watch_signal(scheduler{event}), receiver{event});
  stdexec::start(op2);

  sd_event_loop(event);

  return 0;
}
