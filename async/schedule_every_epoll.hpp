#pragma once

// IWYU pragma: private

#include <systemd/sd-event.h>

#include <exec/async_scope.hpp>
#include <exec/sequence_senders.hpp>
#include <optional>
#include <stdexec/execution.hpp>
#include <system_error>
#include <utility>

#include "async/scheduler.hpp"
#include "async/sd_event_generic_handler.hpp"
#include "async/stop_token.hpp"

struct io_listen {
  using sender_concept = exec::sequence_sender_t;
  using completion_signatures =
      stdexec::completion_signatures<stdexec::set_error_t(std::error_code),
                                     stdexec::set_stopped_t()>;
  using item_types = exec::item_types<>;

  template <stdexec::receiver R>
  struct operation;

  friend auto tag_invoke(exec::subscribe_t, io_listen &&self,
                         stdexec::receiver auto r) noexcept {
    return operation{std::move(r), self.event, self.fd, self.events};
  }

  sd_event *event;
  int fd;
  unsigned int events;
};

template <stdexec::receiver R>
struct io_listen::operation {
  using operation_state_concept = stdexec::operation_state_t;

  operation(R r, sd_event *event, int fd, unsigned int events)
      : r(r),
        event(event),
        fd(fd),
        events(events),
        stop_callback(std::in_place,
                      stdexec::get_stop_token(stdexec::get_env(r)), *this) {}

  ~operation() { sd_event_source_disable_unref(source); }

  void start() noexcept {
    if (!stop_callback.has_value()) return;

    if (auto ret = sd_event_add_io(
            event, &source, fd, events,
            &sd_event_generic_handler<&operation::handle>::handle, this);
        ret < 0) {
      stop_callback.reset();
      return stdexec::set_error(std::move(r),
                                std::error_code(-ret, std::generic_category()));
    }

    if (auto ret = sd_event_source_set_floating(source, true); ret < 0) {
      stop_callback.reset();
      return stdexec::set_error(std::move(r),
                                std::error_code(-ret, std::generic_category()));
    }
  }

  R r;
  sd_event *event;
  int fd;
  unsigned int events;

 private:
  inline void handle(int fd, unsigned int revents) noexcept {
    scope.spawn(exec::set_next(r, stdexec::just(revents)), stdexec::get_env(r));
  }

  inline void stop() noexcept {
    stop_callback.reset();
    sd_event_source_set_enabled(source, SD_EVENT_OFF);
    return stdexec::set_stopped(std::move(r));
  }

  std::optional<typename stdexec::stop_token_of_t<
      stdexec::env_of_t<R>>::template callback_type<mem_fn<&operation::stop>>>
      stop_callback;
  sd_event_source *source{};
  exec::async_scope scope;
};

inline auto schedule_every_epoll(scheduler sch, int fd,
                                 unsigned int events) noexcept {
  return io_listen{sch.event, fd, events};
}
