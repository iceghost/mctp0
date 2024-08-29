#pragma once

#include <bits/time.h>
#include <sys/signalfd.h>

#include <cstdint>
#include <exec/async_scope.hpp>
#include <exec/env.hpp>
#include <exec/sequence_senders.hpp>
#include <exec/timed_scheduler.hpp>
#include <optional>
#include <stdexec/execution.hpp>
#include <stdexec/stop_token.hpp>
#include <utility>

#include "binding.h"

namespace sd {

namespace ex = stdexec;

template <auto handle>
struct mem_fn;
template <typename T, void (T:: *func)() noexcept>
struct mem_fn<func> {
    T *userdata;
    void operator()() { std::invoke(func, *userdata); }
};

template <ex::receiver R, auto handle>
using callback_t = typename ex::stop_token_of_t<
  ex::env_of_t<R>>::template callback_type<mem_fn<handle>>;

struct scheduler {
    struct schedule_sender {
        using sender_concept = ex::sender_t;
        using completion_signatures
          = ex::completion_signatures<ex::set_value_t()>;
        template <ex::receiver R>
        struct operation {
            using operation_state_concept = ex::operation_state_t;
            void start() noexcept { ex::set_value(std::move(r)); }
            R r;
        };
        struct env {
            [[nodiscard]]
            auto query(
              ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept {
                return scheduler{event};
            }
            sd_event *event;
        };
        [[nodiscard]]
        auto get_env() const noexcept {
            return env{e};
        }
        auto connect(ex::receiver auto r) const noexcept {
            return operation{std::move(r)};
        }
        sd_event *e;
    };
    struct schedule_every_epoll_sender {
        using sender_concept = exec::sequence_sender_t;
        using completion_signatures
          = ex::completion_signatures<ex::set_error_t(sd::exception_t),
                                      ex::set_stopped_t()>;
        using item_types = exec::item_types<>;
        template <ex::receiver R>
        struct operation {
            using operation_state_concept = ex::operation_state_t;
            operation(R r, event_t event, int fd, unsigned int events)
                : r(r), event(event), fd(fd), events(events) {}
            ~operation() { sd_event_source_disable_unref(source); }
            void start() noexcept try {
                source
                  = sd::add_io<&operation::handle>(event, fd, events, this);
                stop_callback.emplace(ex::get_stop_token(ex::get_env(r)), this);
            } catch (const sd::exception_t &exception) {
                sd_event_source_set_enabled(source, SD_EVENT_OFF);
                stop_callback.reset();
                return ex::set_error(std::move(r), exception);
            }

            R r;
            event_t event;
            int fd;
            unsigned int events;

           private:
            inline void handle(unsigned int revents) noexcept {
                scope.spawn(exec::set_next(r, ex::just(revents)),
                            ex::get_env(r));
            }
            inline void stop() noexcept {
                sd_event_source_set_enabled(source, SD_EVENT_OFF);
                return ex::set_stopped(std::move(r));
            }
            std::optional<callback_t<R, &operation::stop>> stop_callback{};
            source_t source{};
            exec::async_scope scope{};
        };
        friend auto tag_invoke(exec::subscribe_t,
                               schedule_every_epoll_sender &&self,
                               ex::receiver auto r) noexcept {
            return operation{std::move(r), self.event, self.fd, self.events};
        }
        event_t event;
        int fd;
        unsigned int events;
    };
    struct schedule_at_signal_sender {
        using sender_concept = ex::sender_t;
        using completion_signatures
          = ex::completion_signatures<ex::set_value_t(const signalfd_siginfo &),
                                      ex::set_error_t(sd::exception_t),
                                      ex::set_stopped_t()>;
        template <typename R>
        struct operation {
            using operation_state_concept = ex::operation_state_t;
            operation(R r, sd_event *event, int signal)
                : r{r}, event{event}, signal{signal} {}
            ~operation() { sd_event_source_disable_unref(source); }
            void start() noexcept try {
                source
                  = sd::add_signal<&operation::handle>(event, signal, this);
                sd_event_source_set_enabled(source, SD_EVENT_ONESHOT);
                stop_callback.emplace(ex::get_stop_token(ex::get_env(r)), this);
            } catch (const sd::exception_t &exception) {
                sd_event_source_set_enabled(source, SD_EVENT_OFF);
                stop_callback.reset();
                return ex::set_error(std::move(r), exception);
            }
            R r;
            event_t event;
            int signal;

           private:
            inline void stop() noexcept {
                sd_event_source_set_enabled(source, SD_EVENT_OFF);
                return ex::set_stopped(std::move(r));
            }
            inline void handle(const signalfd_siginfo &si) noexcept {
                stop_callback.reset();
                return ex::set_value(std::move(r), si);
            }
            source_t source{};
            std::optional<callback_t<R, &operation::stop>> stop_callback{};
        };
        auto connect(ex::receiver auto r) const && noexcept {
            return operation{std::move(r), event, signal};
        }

        sd_event *event;
        int signal;
    };
    struct schedule_after_sender {
        using sender_concept = ex::sender_t;
        using completion_signatures
          = ex::completion_signatures<ex::set_value_t(),
                                      ex::set_error_t(sd::exception_t),
                                      ex::set_stopped_t()>;
        template <typename R>
        struct operation {
            using operation_state_concept = ex::operation_state_t;
            operation(R r, sd::event_t event, sd::clock_t clock,
                      sd::duration_t time, sd::duration_t accuracy)
                : r{r},
                  event{event},
                  clock{clock},
                  time{time},
                  accuracy{accuracy} {}
            ~operation() { sd_event_source_disable_unref(source); }
            void start() noexcept try {
                source = sd::add_time_relative<&operation::handle>(
                  event, clock, time, accuracy, this);
                stop_callback.emplace(ex::get_stop_token(ex::get_env(r)), this);
            } catch (const sd::exception_t &exception) {
                sd_event_source_set_enabled(source, SD_EVENT_OFF);
                stop_callback.reset();
                return ex::set_error(std::move(r), exception);
            }
            R r;
            event_t event;
            sd::clock_t clock;
            sd::duration_t time;
            sd::duration_t accuracy;

           private:
            inline void stop() noexcept {
                sd_event_source_set_enabled(source, SD_EVENT_OFF);
                return ex::set_stopped(std::move(r));
            }
            inline void handle(uint64_t) noexcept {
                stop_callback.reset();
                return ex::set_value(std::move(r));
            }
            source_t source{};
            std::optional<callback_t<R, &operation::stop>> stop_callback{};
        };
        auto connect(ex::receiver auto r) const && noexcept {
            return operation{std::move(r), event, clock, time, accuracy};
        }

        sd::event_t event;
        sd::clock_t clock;
        sd::duration_t time;
        sd::duration_t accuracy;
    };
    struct schedule_at_sender {
        using sender_concept = ex::sender_t;
        using completion_signatures
          = ex::completion_signatures<ex::set_value_t(),
                                      ex::set_error_t(sd::exception_t),
                                      ex::set_stopped_t()>;
        template <typename R>
        struct operation {
            using operation_state_concept = ex::operation_state_t;
            operation(R r, sd::event_t event, sd::clock_t clock,
                      sd::duration_t time, sd::duration_t accuracy)
                : r{r},
                  event{event},
                  clock{clock},
                  time{time},
                  accuracy{accuracy} {}
            ~operation() { sd_event_source_disable_unref(source); }
            void start() noexcept try {
                source = sd::add_time<&operation::handle>(event, clock, time,
                                                          accuracy, this);
                stop_callback.emplace(ex::get_stop_token(ex::get_env(r)), this);
            } catch (const sd::exception_t &exception) {
                sd_event_source_set_enabled(source, SD_EVENT_OFF);
                stop_callback.reset();
                return ex::set_error(std::move(r), exception);
            }
            R r;
            event_t event;
            sd::clock_t clock;
            sd::duration_t time;
            sd::duration_t accuracy;

           private:
            inline void stop() noexcept {
                sd_event_source_set_enabled(source, SD_EVENT_OFF);
                return ex::set_stopped(std::move(r));
            }
            inline void handle(uint64_t) noexcept {
                stop_callback.reset();
                return ex::set_value(std::move(r));
            }
            source_t source{};
            std::optional<callback_t<R, &operation::stop>> stop_callback{};
        };
        auto connect(ex::receiver auto r) const && noexcept {
            return operation{std::move(r), event, clock, time, accuracy};
        }

        sd::event_t event;
        sd::clock_t clock;
        sd::duration_t time;
        sd::duration_t accuracy;
    };

    ex::sender auto schedule() noexcept { return schedule_sender{event}; }
    ex::sender auto schedule_every_epoll(int fd, unsigned int events) noexcept {
        return schedule_every_epoll_sender{event, fd, events};
    }
    ex::sender auto schedule_at_signal(int signal) noexcept {
        return schedule_at_signal_sender{event, signal};
    }
    friend auto tag_invoke(exec::schedule_after_t, const scheduler &sch,
                           sd::duration_t dur) noexcept {
        return schedule_after_sender{sch.event, sch.clock, dur, sch.accuracy};
    }
    friend auto tag_invoke(exec::schedule_at_t, const scheduler &sch,
                           sd::time_point_t time) noexcept {
        return schedule_at_sender{sch.event, sch.clock, time, sch.accuracy};
    }
    friend auto tag_invoke(exec::now_t, const scheduler &sch) noexcept {
        uint64_t now;
        sd_event_now(sch.event, sch.clock, &now);
        return sd::time_point_t{sd::duration_t{now}};
    }
    bool operator==(const scheduler &) const = default;

    event_t event;
    sd::clock_t clock = CLOCK_MONOTONIC;
    sd::duration_t accuracy = sd::duration_t{0};
};

static_assert(ex::scheduler<scheduler>);
static_assert(exec::timed_scheduler<scheduler>);

template <typename CallbackFn>
struct stop_callback;
struct stop_token {
   public:
    template <typename CallbackFn>
    using callback_type = stop_callback<CallbackFn>;
    bool operator==(const stop_token other) const noexcept {
        return event == other.event;
    }
    [[nodiscard]]
    bool stop_requested() const noexcept {
        return sd_event_get_state(event) == SD_EVENT_EXITING;
    }
    [[nodiscard]]
    bool stop_possible() const noexcept {
        return sd_event_get_state(event) != SD_EVENT_FINISHED;
    }
    sd_event *event;
};

template <typename CallbackFn>
struct stop_callback {
    stop_callback(stop_token t, CallbackFn cb)
        : source(sd::add_exit<&stop_callback::handle>(t.event, this)), cb(cb) {}
    stop_callback(const stop_callback &) = delete;
    stop_callback(const stop_callback &&) = delete;
    auto operator=(const stop_callback &) const & = delete;
    auto operator=(const stop_callback &&) const & = delete;
    ~stop_callback() noexcept { sd_event_source_disable_unref(source); }
    void handle() noexcept { cb(); }
    source_t source;
    CallbackFn cb;
};
template <typename T, void (T:: *func)() noexcept>
struct stop_callback<mem_fn<func>> {
    stop_callback(stop_token t, T *userdata)
        : source(sd::add_exit<func>(t.event, userdata)) {}
    stop_callback(const stop_callback &) = delete;
    stop_callback(const stop_callback &&) = delete;
    auto operator=(const stop_callback &) const & = delete;
    auto operator=(const stop_callback &&) const & = delete;
    ~stop_callback() noexcept { sd_event_source_disable_unref(source); }
    source_t source;
};

}  // namespace sd
