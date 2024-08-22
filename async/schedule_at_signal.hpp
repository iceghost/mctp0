#pragma once

// IWYU pragma: private

#include <sys/signalfd.h>
#include <systemd/sd-event.h>

#include <cassert>
#include <exec/async_scope.hpp>
#include <exec/sequence_senders.hpp>
#include <optional>
#include <stdexec/execution.hpp>
#include <system_error>
#include <utility>

#include "async/scheduler.hpp"
#include "async/sd_event_generic_handler.hpp"
#include "async/stop_token.hpp"

struct signal_once {
  using sender_concept = stdexec::sender_t;
  using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(const signalfd_siginfo &),
      stdexec::set_error_t(std::error_code), stdexec::set_stopped_t()>;

  template <typename R>
  struct operation {
    using operation_state_concept = stdexec::operation_state_t;

    operation(R r, sd_event *event, int signal)
        : r(r),
          event(event),
          signal(signal),
          stop_callback{std::in_place,
                        stdexec::get_stop_token(stdexec::get_env(r)), *this} {}

    ~operation() { sd_event_source_disable_unref(source); }

    void start() noexcept {
      if (!stop_callback.has_value()) {
        return;
      }

      if (auto ret = sd_event_add_signal(
              event, &source, signal,
              sd_event_generic_handler<&operation::handle>::handle, this);
          ret < 0) {
        return stdexec::set_error(
            std::move(r), std::error_code(-ret, std::generic_category()));
      };
      sd_event_source_set_enabled(source, SD_EVENT_ONESHOT);
    }

    R r;
    sd_event *event;
    int signal;

   private:
    inline void stop() noexcept {
      stop_callback.reset();
      sd_event_source_set_enabled(source, SD_EVENT_OFF);
      return stdexec::set_stopped(std::move(r));
    }

    inline void handle(const signalfd_siginfo *si) noexcept {
      return stdexec::set_value(std::move(r), *si);
    }

    sd_event_source *source{};
    std::optional<typename stdexec::stop_token_of_t<
        stdexec::env_of_t<R>>::template callback_type<mem_fn<&operation::stop>>>
        stop_callback{};
  };

  auto connect(stdexec::receiver auto r) const && noexcept {
    return operation{std::move(r), event, signal};
  }

  sd_event *event;
  int signal;
};

auto schedule_at_signal(scheduler sch, int signal) noexcept {
  return signal_once{sch.event, signal};
}
