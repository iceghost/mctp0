#include <bits/chrono.h>
#include <bits/time.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sd/binding.h>
#include <systemd/sd-event.h>

using namespace std::literals;

struct ctx {
    sd::event_t event{};
    sd::source_t source{};

    ~ctx() {
        sd_event_source_disable_unref(source);
        sd_event_unref(event);
    }

    void handle(uint64_t) noexcept {
        std::printf("what the fuck\n");
        sd_event_exit(event, 1);
    }
};

int main() {
    ctx ctx{};
    sd_event_default(&ctx.event);

    ctx.source = sd::add_time_relative<&ctx::handle>(ctx.event, CLOCK_MONOTONIC,
                                                     1s, 0s, &ctx);

    return sd_event_loop(ctx.event);
}
