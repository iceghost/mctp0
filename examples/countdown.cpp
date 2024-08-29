#include <bits/chrono.h>
#include <bits/types/sigset_t.h>
#include <sd/binding.h>
#include <sd/sender.h>
#include <signal.h>
#include <unistd.h>

#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exec/async_scope.hpp>
#include <exec/sequence/ignore_all_values.hpp>
#include <exec/sequence/transform_each.hpp>
#include <exec/timed_scheduler.hpp>
#include <exec/variant_sender.hpp>
#include <iostream>
#include <stdexec/execution.hpp>
#include <string>
#include <system_error>

using namespace std::literals;
namespace ex = stdexec;

ex::sender auto listen_stdin(sd::scheduler sch) {
    return ex::let_value(ex::just(), [time = 0, sch] mutable noexcept {
        return sch.schedule_every_epoll(STDIN_FILENO, EPOLLIN)
               | exec::transform_each(
                 ex::then([&time](unsigned int) mutable noexcept {
                     std::string s;
                     std::getline(std::cin, s);
                     std::printf("%d... %.*s\n", time++, (int)s.length(),
                                 s.data());
                 }))
               | exec::ignore_all_values() | ex::upon_stopped([]() noexcept {
                     std::printf("stopping counting\n");
                 });
    });
}

ex::sender auto watch_signal(sd::scheduler sch) {
    return sch.schedule_at_signal(SIGINT) | ex::let_value([](auto) noexcept {
               std::printf("SIGINT\n");
               return ex::just_stopped();
           });
}

ex::sender auto self_destroy(exec::timed_scheduler auto sch) {
    return exec::schedule_after(sch, 5s) | ex::let_value([] noexcept {
               std::printf("your life trial has ended\n");
               return ex::just_stopped();
           });
}

struct env {
    auto query(ex::get_stop_token_t) const noexcept {
        return sd::stop_token{event};
    }
    sd::event_t event;
};

struct receiver {
    using receiver_concept = ex::receiver_t;
    void set_value() noexcept {}
    void set_error(sd::exception_t exception) noexcept {
        std::fprintf(stderr, "error: %s (%d)", exception.what(),
                     exception.code().value());
        sd_event_exit(event, EXIT_FAILURE);
    }
    void set_stopped() noexcept {
        // oops
        sd_event_exit(event, 0);
    };
    auto get_env() const noexcept { return env{event}; }
    sd::event_t event;
};

int main() {
    [[gnu::cleanup(sd_event_unrefp)]]
    sd::event_t event{};
    exec::async_scope scope;

    sigset_t s{};
    sigaddset(&s, SIGINT);
    sigprocmask(SIG_BLOCK, &s, nullptr);

    assert(sd_event_default(&event) >= 0);

    auto op1 = ex::connect(listen_stdin(sd::scheduler{event}), receiver{event});
    ex::start(op1);

    auto op2 = ex::connect(watch_signal(sd::scheduler{event}), receiver{event});
    ex::start(op2);

    auto op3 = ex::connect(self_destroy(sd::scheduler{event}), receiver{event});
    ex::start(op3);

    return sd_event_loop(event);
}
