#pragma once

// IWYU pragma: private

#include <systemd/sd-event.h>

#include <cassert>
#include <exec/scope.hpp>
#include <stdexec/execution.hpp>
#include <system_error>

#include "async/sd_event_generic_handler.hpp"

template <typename CallbackFn>
struct stop_callback;

struct stop_token {
 public:
  template <typename CallbackFn>
  using callback_type = stop_callback<CallbackFn>;

  auto operator==(const stop_token other) const noexcept {
    return event == other.event;
  }

  [[nodiscard]]
  auto stop_requested() const noexcept {
    return sd_event_get_state(event) == SD_EVENT_EXITING;
  }

  [[nodiscard]]
  auto stop_possible() const noexcept {
    return true;
  }

  sd_event *event{};
};

template <auto handle>
struct mem_fn;

template <typename T, void (T:: *func)() noexcept>
struct mem_fn<func> {
  T &userdata;
  void operator()() { std::invoke(func, userdata); }
};

template <typename CallbackFn>
struct stop_callback;

template <typename T, void (T:: *func)() noexcept>
struct stop_callback<mem_fn<func>> {
  stop_callback(stop_token t, T &userdata) {
    int ret = sd_event_add_exit(
        t.event, &source, sd_event_generic_handler<func>::handle, &userdata);
    if (ret < 0) throw std::error_code(-ret, std::generic_category());
  }

  ~stop_callback() noexcept { sd_event_source_disable_unref(source); }

  sd_event_source *source{};
};
