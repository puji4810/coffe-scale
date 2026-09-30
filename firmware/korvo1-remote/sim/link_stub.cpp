// Host-side stub of link.cpp — a synthetic scale: a repeating bloom+pour
// cycle with jitter, a working timer, draining battery, wandering level.
// send_cmd() mutates the sim state so the touch buttons do real things.

#include "link.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>

namespace {

net::snapshot s;
std::uint32_t  s_seq = 0;
bool           s_offline = false;
std::mutex     s_mtx;

using clk = std::chrono::steady_clock;
const auto t0 = clk::now();

long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clk::now() - t0).count();
}

void step() {
    static long last = now_ms();
    const long now = now_ms();
    const float dt = (now - last) / 1000.f;
    last = now;

    std::lock_guard lk(s_mtx);
    // pour cycle: bloom 0-5 s, pour to ~220 g over ~25 s, rest 10 s, repeat
    if (s.timer_state == 1) s.timer_ms += static_cast<long>(dt * 1000);

    const float cycle = std::fmod(now / 1000.f, 45.f);
    float target_flow = 0.f;
    if (s.mode == 1) {                      // brew: scripted pour profile
        const float t = s.timer_ms / 1000.f;
        if (t > 5 && t < 30) target_flow = 4.f + 1.5f * std::sin(t * 0.9f);
        if (t > 45) { s.timer_state = 0; s.timer_ms = 0; }
    } else {                                // weigh: slow drift + steps
        target_flow = (cycle < 20) ? 0.f : 0.f;
    }
    s.flow_gps += (target_flow - s.flow_gps) * std::min(1.f, dt * 6.f);
    s.grams    += s.flow_gps * dt;
    if (s.grams < 0) s.grams = 0;
    s.grams += 0.002f * std::sin(now / 37.f);          // idle jitter
    s.display_value = s.unit == 0 ? s.grams : s.grams / 28.3495f;

    s.stable    = std::fabs(s.flow_gps) < 0.3f;
    s.pitch_deg = 1.2f * std::sin(now / 9000.f);
    s.roll_deg  = 1.2f * std::cos(now / 11000.f);
    static int tick = 0;
    if (++tick % 80 == 0 && s.battery_pct > 5) s.battery_pct--;
    ++s_seq;
}

} // namespace

void net::start() {
    s.battery_pct = 87;
    s.calibrated  = true;
    s.tared       = true;
}

bool net::send_cmd(const char* cmd) {
    std::lock_guard lk(s_mtx);
    std::printf("[stub] cmd '%s'\n", cmd);
    const std::string c = cmd;
    if (c == "tare")            { s.grams = 0; s.tared = true; }
    else if (c == "long")       { s.timer_state = s.timer_state == 1 ? 0 : 1;
                                  if (s.timer_state == 1) s.timer_ms = 0; }
    else if (c == "reset")      { s.timer_state = 0; s.timer_ms = 0; }
    else if (c == "mode")       { s.mode ^= 1; }
    else if (c == "unit0")      { s.unit = 0; }
    else if (c == "unit1")      { s.unit = 1; }
    else if (c == "sleep")      { s_offline = true; }
    return true;
}

net::snapshot net::latest(std::uint32_t* seq) {
    step();
    if (seq) *seq = s_seq;
    return s;
}

bool net::online() { return !s_offline; }

void net::sim_toggle_offline() { s_offline = !s_offline; }
