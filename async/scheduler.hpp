#pragma once

// IWYU pragma: private

#include <systemd/sd-event.h>

#include <exec/env.hpp>
#include <exec/inline_scheduler.hpp>
#include <exec/sequence_senders.hpp>
#include <stdexec/execution.hpp>

struct scheduler {
  struct sender {
    using sender_concept = stdexec::sender_t;
    using completion_signatures =
        stdexec::completion_signatures<stdexec::set_value_t()>;

    template <stdexec::receiver R>
    struct operation {
      using operation_state_concept = stdexec::operation_state_t;

      void start() noexcept { stdexec::set_value(std::move(r)); }

      R r;
    };

    struct env {
      [[nodiscard]]
      auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>)
          const noexcept {
        return scheduler{event};
      }

      sd_event *event;
    };

    [[nodiscard]]
    auto get_env() const noexcept {
      return env{e};
    }
    auto connect(stdexec::receiver auto r) const noexcept {
      return operation{std::move(r)};
    }

    sd_event *e;
  };

  auto schedule() noexcept { return sender{event}; };
  auto operator==(const scheduler &) const -> bool = default;

  sd_event *event;
};

static_assert(stdexec::scheduler<scheduler>);
