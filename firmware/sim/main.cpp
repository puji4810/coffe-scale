/// Desktop preview: feeds a synthetic weigh profile + noise into scale::app
/// at 80 SPS and renders ui::model — the same code path the target uses.
///
/// Keys:  T = tare   L = long-tare (timer toggle in brew mode)   M = mode
///        Esc / close window = quit

#include <chrono>
#include <cstdio>
#include <random>

#include <SDL.h>

#include "lvgl.h"
#include "scale/app.hpp"
#include "sdl_port.hpp"
#include "ui/ui.hpp"

namespace {

scale::app g_app;

void refresh(void*) {
    const ui::model m{g_app.state(), g_app.display_value(), /*battery*/ 82,
                      /*charging*/ false};
    ui::update(m);
}

// grams on the pan at time t (cup lands ~2 s, pour 2..14 s)
float profile(float t) {
    if (t < 2.0f) return 0.0f;
    if (t < 4.0f) return 120.0f;
    if (t < 14.0f) return 120.0f + (t - 4.0f) * 15.0f;
    return 270.0f;
}

} // namespace

int main() {
    ui::port_init();
    ui::create();
    ui::set_refresh(refresh, nullptr);

    // Plausible counts for a 3 kg cell: ~1600 counts/g near zero offset.
    g_app.load_calibration({.zero_counts = 80'000.0f, .counts_per_gram = 1600.0f});

    std::mt19937                    rng{42};
    std::normal_distribution<float> noise{0.0f, 60.0f};   // ~0.04 g RMS @1600c/g

    std::puts("coffee-scale sim — T tare, L long-tare/timer, M mode, Esc quit");

    const auto t0   = std::chrono::steady_clock::now();
    auto       last = t0;
    float      feed_acc = 0.0f;

    for (;;) {
        const auto now  = std::chrono::steady_clock::now();
        const auto ms   = std::chrono::duration_cast<scale::clock_ms>(now - t0);
        const float dt  = std::chrono::duration<float>(now - last).count();
        last            = now;

        // 80 SPS synthetic feed; flat + still accelerometer alongside
        feed_acc += dt;
        while (feed_acc >= 1.0f / 80.0f) {
            feed_acc -= 1.0f / 80.0f;
            const float g     = profile(ms.count() / 1000.0f);
            const auto  raw   = static_cast<std::int32_t>(
                80'000.0f + g * 1600.0f + noise(rng));
            g_app.feed_accel(0.f, 0.f, 1'000.f + noise(rng) / 60.f);
            g_app.feed(raw, ms);
        }

        const std::uint32_t keys = sim::sdl_poll();
        if (keys & sim::key_quit) break;
        if (keys & sim::key_tare) g_app.tare();
        if (keys & sim::key_long) g_app.tare_long();
        if (keys & sim::key_mode) g_app.next_mode();

        ui::housekeeping();
        lv_timer_handler();
        SDL_Delay(5);
    }
    return 0;
}
