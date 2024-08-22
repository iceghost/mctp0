#pragma once

// IWYU pragma: private

#include <systemd/sd-event.h>

#include <concepts>
#include <functional>

template <auto>
struct sd_event_generic_handler;

template <typename T, typename R, typename... Args,
          R (T:: *func)(Args...) noexcept>
struct sd_event_generic_handler<func> {
  static auto handle(sd_event_source *, Args... args, void *userdata) -> int {
    static_assert(std::same_as<R, void>);
    std::invoke(func, static_cast<T *>(userdata), args...);
    return 0;
  }
};
