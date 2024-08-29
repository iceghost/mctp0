#pragma once

#include <bits/time.h>
#include <sys/signalfd.h>
#include <systemd/sd-event.h>  // IWYU pragma: export

#include <chrono>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <expected>
#include <functional>
#include <ratio>
#include <system_error>
#include <type_traits>

namespace sd {

using event_t = sd_event*;
using source_t = sd_event_source*;
using exception_t = std::system_error;
using clock_t = std::clock_t;
using duration_t = std::chrono::duration<uint64_t, std::micro>;
struct time_point_t : duration_t {
    using duration = duration_t;
};

inline void throw_error_if(bool cond, int ec) {
    if (cond) throw std::system_error(ec, std::generic_category());
}

namespace detail {
template <auto handle>
struct add_signal_t;
template <typename T, typename R, typename... Arg,
          R (T::* handle)(Arg...) noexcept>
struct add_signal_t<handle> {
    static_assert(std::is_invocable_r<void, decltype(handle), T*,
                                      const signalfd_siginfo&>());
    inline static source_t operator()(event_t e, int signal, T* userdata) {
        source_t source{};
        int ret = sd_event_add_signal(e, &source, signal, callback, userdata);
        throw_error_if(ret < 0, -ret);
        return source;
    }

   private:
    static int callback(source_t, const signalfd_siginfo* info,
                        void* userdata) noexcept {
        std::invoke(handle, static_cast<T*>(userdata), *info);
        return 0;
    }
};

template <auto handle>
struct add_io_t;
template <typename T, typename R, typename... Arg,
          R (T::* handle)(Arg...) noexcept>
struct add_io_t<handle> {
    static_assert(
      std::is_invocable_r<void, decltype(handle), T*, unsigned int>());
    inline static source_t operator()(event_t e, int fd, int events,
                                      T* userdata) {
        source_t source{};
        int ret = sd_event_add_io(e, &source, fd, events, callback, userdata);
        throw_error_if(ret < 0, -ret);
        return source;
    }

   private:
    static int callback(source_t, int, unsigned int revents,
                        void* userdata) noexcept {
        std::invoke(handle, static_cast<T*>(userdata), revents);
        return 0;
    }
};

template <auto handle>
struct add_time_relative_t;
template <typename T, typename R, typename... Arg,
          R (T::* handle)(Arg...) noexcept>
struct add_time_relative_t<handle> {
    static_assert(std::is_invocable_r<void, decltype(handle), T*, uint64_t>());
    inline static source_t operator()(event_t e, clock_t clock, duration_t time,
                                      duration_t accuracy, T* userdata) {
        source_t source{};
        int ret
          = sd_event_add_time_relative(e, &source, clock, time.count(),
                                       accuracy.count(), callback, userdata);
        throw_error_if(ret < 0, -ret);
        return source;
    }

   private:
    static int callback(source_t, uint64_t time, void* userdata) noexcept {
        std::invoke(handle, static_cast<T*>(userdata), time);
        return 0;
    }
};

template <auto handle>
struct add_time_t;
template <typename T, typename R, typename... Arg,
          R (T::* handle)(Arg...) noexcept>
struct add_time_t<handle> {
    static_assert(std::is_invocable_r<void, decltype(handle), T*, uint64_t>());
    inline static source_t operator()(event_t e, clock_t clock, duration_t time,
                                      duration_t accuracy, T* userdata) {
        source_t source{};
        int ret = sd_event_add_time(e, &source, clock, time.count(),
                                    accuracy.count(), callback, userdata);
        throw_error_if(ret < 0, -ret);
        return source;
    }

   private:
    static int callback(source_t, uint64_t time, void* userdata) noexcept {
        std::invoke(handle, static_cast<T*>(userdata), time);
        return 0;
    }
};

template <auto handle>
struct add_exit_t;
template <typename T, typename R, typename... Arg,
          R (T::* handle)(Arg...) noexcept>
struct add_exit_t<handle> {
    static_assert(std::is_invocable_r<void, decltype(handle), T*>());

    inline static source_t operator()(event_t e, T* userdata) {
        source_t source{};
        int ret = sd_event_add_exit(e, &source, callback, userdata);
        throw_error_if(ret < 0, -ret);
        return source;
    }

   private:
    static int callback(source_t, void* userdata) noexcept {
        std::invoke(handle, static_cast<T*>(userdata));
        return 0;
    }
};
}  // namespace detail

// clang-format off
template <auto handle>
inline constexpr detail::add_time_relative_t<handle> add_time_relative;
template <auto handle>
inline constexpr detail::add_time_t<handle>          add_time;
template <auto handle>
inline constexpr detail::add_signal_t<handle>        add_signal;
template <auto handle>
inline constexpr detail::add_exit_t<handle>          add_exit;
template <auto handle>
inline constexpr detail::add_io_t<handle>            add_io;
// clang-format on

};  // namespace sd
