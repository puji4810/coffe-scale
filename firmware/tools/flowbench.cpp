/// flowbench — synthetic + capture benchmarks for the flow_kf estimator.
///
///   flowbench            all synthetic scenarios
///   flowbench cap <file> replay a tools/capture.py raw file (W/A lines)
///                        and print the named-window metrics
///
/// The feed path mirrors scale::push: median_filter<3> on counts, then
/// grams -> flow_kf (calibration constants come from the capture header).
/// Metrics are printed as `name metric=value` lines for scripting.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "scale/flow_kf.hpp"

namespace {

constexpr float kFs = 80.0f;
constexpr float kDt = 1.0f / kFs;

struct probe {
    scale::flow_kf        kf;
    scale::median_filter<3> med;
    float t   = 0.0f;
    int   gated = 0;      // samples spent gated
    int   boosted = 0;    // samples spent boosted
    int   trips = 0;      // rising edges of disturbed()
    int   boosts = 0;     // rising edges of boosting()
    bool  was_g = false, was_b = false;

    float feed(double tt, float g) {
        const auto raw = static_cast<std::int32_t>(std::lround(g * 10000.0));
        const float mg = static_cast<float>(med.push(raw)) / 10000.0f;
        kf.push(scale::clock_ms{static_cast<std::int64_t>(std::llround(tt * 1000.0))}, mg);
        t = static_cast<float>(tt);
        if (kf.disturbed()) { ++gated; if (!was_g) ++trips; }
        if (kf.boosting())  { ++boosted; if (!was_b) ++boosts; }
        was_g = kf.disturbed();
        was_b = kf.boosting();
        return kf.rate();
    }
};

// Ideal 0 -> 4 g/s ramp at t=2, hold to t=8, flat after.
void bench_ramp() {
    probe p;
    float t10 = -1, t50 = -1, t90 = -1, tstop = -1, over = 0;
    float settle = -1, ssettle = -1, under = 0;
    for (int i = 0; i < static_cast<int>(10 * kFs); ++i) {
        const double t = i * kDt;
        const float  g = t < 2 ? 0 : t < 8 ? 4.0f * (t - 2) : 24.0f;
        const float  r = p.feed(t, g);
        if (t10 < 0 && t >= 2 && r >= 0.4f) t10 = static_cast<float>(t) - 2;
        if (t50 < 0 && t >= 2 && r >= 2.0f) t50 = static_cast<float>(t) - 2;
        if (t90 < 0 && t >= 2 && r >= 3.6f) t90 = static_cast<float>(t) - 2;
        if (t >= 2 && t < 8) {
            if (r - 4.0f > over) over = r - 4.0f;
            if (std::fabs(r - 4.0f) > 0.4f) settle = static_cast<float>(t) - 2;
        }
        if (tstop < 0 && t >= 8 && r <= 0.4f) tstop = static_cast<float>(t) - 8;
        if (t >= 8) {
            if (r < under) under = r;
            if (std::fabs(r) > 0.4f) ssettle = static_cast<float>(t) - 8;
        }
    }
    std::printf("ramp t10=%.3f t50=%.3f t90=%.3f settle=%.3f over=%.3f "
                "stop90=%.3f ssettle=%.3f under=%.3f "
                "trips=%d boosts=%d boost_ms=%d\n",
                t10, t50, t90, settle, over, tstop, ssettle, under,
                p.trips, p.boosts,
                static_cast<int>(p.boosted * 1000 / kFs));
}

// Mass step of `a` grams at t=5; report displayed-flow damage.
void bench_step(float a) {
    probe p;
    float peak = 0, last = -1;
    for (int i = 0; i < static_cast<int>(12 * kFs); ++i) {
        const double t = i * kDt;
        const float  r = p.feed(t, t < 5 ? 0.0f : a);
        if (t >= 5) {
            peak = std::max(peak, std::fabs(r));
            if (std::fabs(r) >= 0.3f) last = static_cast<float>(t) - 5;
        }
    }
    std::printf("step%.0f peak=%.3f trips=%d boosts=%d last=%.3f\n",
                a, peak, p.trips, p.boosts, last);
}

// Sustained pour at s g/s from t=5; the gate must not fire on a clean
// ramp (except maybe for s past realistic hand pours).
void bench_pour(float s) {
    probe p;
    float peak_err = 0, fin = 0;
    for (int i = 0; i < static_cast<int>(10 * kFs); ++i) {
        const double t = i * kDt;
        const float  r = p.feed(t, t < 5 ? 0.0f : s * (t - 5));
        if (t >= 6) peak_err = std::max(peak_err, std::fabs(r - s));
        fin = r;
    }
    std::printf("pour%.0f err=%.3f trips=%d boosts=%d fin=%.2f\n",
                s, peak_err, p.trips, p.boosts, fin);
}

// Pour 4 g/s from t=2, stop at t=5 while a +-40 g 12 Hz burst lasts
// 0.4 s — how long until the readout is quiet again.
void bench_stop_impact() {
    probe p;
    float peak = 0, last = -1;
    for (int i = 0; i < static_cast<int>(15 * kFs); ++i) {
        const double t = i * kDt;
        double       g = t < 2 ? 0 : t < 5 ? 4 * (t - 2) : 12;
        const double u = t - 5;
        if (u >= 0 && u < 0.4) g += 40 * std::sin(2 * 3.14159265 * 12 * u);
        const float r = p.feed(t, static_cast<float>(g));
        if (t >= 5) {
            peak = std::max(peak, std::fabs(r));
            if (std::fabs(r) >= 0.3f) last = static_cast<float>(t) - 5;
        }
    }
    std::printf("stopimpact peak=%.3f trips=%d boosts=%d last=%.3f\n",
                peak, p.trips, p.boosts, last);
}

// Same burst mid-pour: tracking must stay near the pour and recover.
void bench_mid_impact() {
    probe p;
    float worst = 0, last = -1;
    for (int i = 0; i < static_cast<int>(10 * kFs); ++i) {
        const double t   = i * kDt;
        double       g   = t < 2 ? 0 : 4 * (t - 2);
        const double tau = t - 5;
        if (tau >= 0 && tau < 0.4) g += 40 * std::sin(2 * 3.14159265 * 12 * tau);
        const float r = p.feed(t, static_cast<float>(g));
        if (t >= 3) worst = std::max(worst, std::fabs(r - 4.0f));
        if (t >= 5 && std::fabs(r - 4.0f) >= 1.5f) last = static_cast<float>(t) - 5;
    }
    std::printf("midimpact worst=%.3f trips=%d last_dev=%.3f fin_ok=%d\n",
                worst, p.trips, last,
                std::fabs(p.kf.rate() - 4.0f) < 1.5f ? 1 : 0);
}

// Slow cup placement (a gentle 0.3 s ramp-down of 3 g) — the ambiguous
// case between a step and a pour; damage should stay bounded.
void bench_softstep() {
    probe p;
    float peak = 0;
    for (int i = 0; i < static_cast<int>(12 * kFs); ++i) {
        const double t = i * kDt;
        const double u = t - 5;
        float g = u < 0 ? 0.0f : u < 0.3f ? 10.0f * u : 3.0f;
        const float r = p.feed(t, g);
        if (t >= 5) peak = std::max(peak, std::fabs(r));
    }
    std::printf("softstep3g peak=%.3f trips=%d boosts=%d\n",
                peak, p.trips, p.boosts);
}

// Bench wiggle: +-2 g, 1.5 Hz handling sine — no boost should latch.
void bench_wiggle() {
    probe p;
    float peak = 0;
    for (int i = 0; i < static_cast<int>(12 * kFs); ++i) {
        const double t = i * kDt;
        const float  g = t < 3 ? 0.0f
                               : 2.0f * std::sin(2 * 3.14159265f * 1.5f * (t - 3));
        const float r = p.feed(t, g);
        if (t >= 3) peak = std::max(peak, std::fabs(r));
    }
    std::printf("wiggle peak=%.3f trips=%d boosts=%d\n",
                peak, p.trips, p.boosts);
}

// +-3 g, 2 Hz sine for 10 s — a strong sustained wobble that DOES trip
// the gate; resume must not adopt the sine's instantaneous slope.
void bench_wiggle2() {
    probe p;
    float peak = 0;
    for (int i = 0; i < static_cast<int>(14 * kFs); ++i) {
        const double t = i * kDt;
        const float  g = t < 3 ? 0.0f
                               : 3.0f * std::sin(2 * 3.14159265f * 2.0f * (t - 3));
        const float r = p.feed(t, g);
        if (t >= 3) peak = std::max(peak, std::fabs(r));
    }
    std::printf("wiggle2 peak=%.3f trips=%d boosts=%d\n",
                peak, p.trips, p.boosts);
}

// A 200 ms hole in an otherwise steady 80 Hz stream while pouring —
// the gap re-prime should keep the estimator sane.
void bench_gap() {
    probe p;
    float peak = 0;
    for (int i = 0; i < static_cast<int>(10 * kFs); ++i) {
        double t = i * kDt;
        if (t >= 5.0 && t < 5.2) continue;           // dropped samples
        const float g = t < 2 ? 0.0f : 4.0f * (t - 2);
        const float r = p.feed(t, g);
        if (t >= 5.2) peak = std::max(peak, std::fabs(r - 4.0f));
    }
    std::printf("gap err=%.3f trips=%d\n", peak, p.trips);
}

// Capture replay: median -> grams -> flow_kf on each W line; named
// window stats plus gate/boost totals.
void bench_capture(const char* path, bool loud = false) {
    std::ifstream in(path);
    if (!in) { std::printf("cap cannot open %s\n", path); return; }
    probe      p;
    std::string s;
    struct win { const char* name; double a, b; float peak; double sum, sum2; int n; };
    win wins[] = {{"rest", 140, 170, 0, 0, 0, 0}, {"bloom", 335, 349, 0, 0, 0, 0},
                  {"lift", 433, 441, 0, 0, 0, 0}, {"cups", 450, 490, 0, 0, 0, 0},
                  {"taps", 492, 514, 0, 0, 0, 0}, {"all", 0, 1e9, 0, 0, 0, 0}};
    double cpg = 1262.910034, zero = 3613397.0;   // capture header
    int    acc_decim = 0;
    while (std::getline(in, s)) {
        if (s[0] == '#') {
            std::sscanf(s.c_str(), "# coffee-scale raw v1 cpg=%lf", &cpg);
            std::string z = " zero=";
            if (const auto pos = s.find(z); pos != std::string::npos)
                zero = std::atof(s.c_str() + pos + z.size());
            continue;
        }
        long long us;
        if (s.rfind("A,", 0) == 0) {
            float x, y, z;
            // Firmware polls the IMU at 100 Hz but only forwards every
            // 5th sample — match that or the motion EMA's time constant
            // runs 5x fast here.
            if (std::sscanf(s.c_str(), "A,%lld,%f,%f,%f", &us, &x, &y, &z)
                    == 4 &&
                acc_decim++ % 5 == 0) {
                p.kf.feed_accel(x, y, z);
            }
            continue;
        }
        int raw;
        if (std::sscanf(s.c_str(), "W,%lld,%d", &us, &raw) != 2) continue;
        const double t = (us - 826944) / 1e6;
        const float  g = (static_cast<float>(p.med.push(raw)) -
                          static_cast<float>(zero)) /
                         static_cast<float>(cpg);
        p.kf.push(scale::clock_ms{us / 1000}, g);
        const float r = p.kf.rate();
        if (p.kf.disturbed()) ++p.gated;
        if (p.kf.boosting())  ++p.boosted;
        if (loud && std::fabs(r) >= 10.0f && !p.kf.disturbed())
            std::printf("loud t=%.2f r=%.1f boost=%d inn=%.2f fast=%.2f\n",
                        t, r, p.kf.boosting() ? 1 : 0, p.kf.innovation(),
                        p.kf.fast_level());
        for (auto& w : wins) {
            if (t < w.a || t >= w.b) continue;
            const float d = std::fabs(r) < 0.3f ? 0.0f : std::clamp(r, -30.f, 30.f);
            ++w.n;
            w.sum  += d;
            w.sum2 += d * d;
            w.peak = std::max(w.peak, std::fabs(d));
        }
    }
    for (const auto& w : wins) {
        const double mean = w.n ? w.sum / w.n : 0.0;
        const double var  = w.n ? w.sum2 / w.n - mean * mean : 0.0;
        std::printf("cap %s disp_std=%.3f disp_peak=%.3f n=%d\n", w.name,
                    std::sqrt(std::max(var, 0.0)), w.peak, w.n);
    }
    std::printf("cap gate_samples=%d boost_samples=%d\n", p.gated, p.boosted);
}

// Per-sample trace for debugging: t, rate, disturbed, boosting, inn.
void trace(const char* scen, double a) {
    probe p;
    std::printf("t,rate,gate,boost,inn,fast,jump,weight\n");
    for (int i = 0; i < static_cast<int>(15 * kFs); ++i) {
        const double t = i * kDt;
        double       g = 0;
        if (std::string(scen) == "ramp")
            g = t < 2 ? 0 : t < 8 ? 4 * (t - 2) : 24;
        else if (std::string(scen) == "pour")
            g = t < 5 ? 0 : a * (t - 5);
        else if (std::string(scen) == "step")
            g = t < 5 ? 0 : a;
        else if (std::string(scen) == "wiggle")
            g = t < 3 ? 0 : a * std::sin(2 * 3.14159265 * 1.5 * (t - 3));
        else if (std::string(scen) == "stopimpact") {
            g = t < 2 ? 0 : t < 5 ? 4 * (t - 2) : 12;
            const double u = t - 5;
            if (u >= 0 && u < 0.4) g += 40 * std::sin(2 * 3.14159265 * 12 * u);
        }
        else if (std::string(scen) == "midimpact") {
            g = t < 2 ? 0 : 4 * (t - 2);
            const double u = t - 5;
            if (u >= 0 && u < 0.4) g += 40 * std::sin(2 * 3.14159265 * 12 * u);
        }
        else if (std::string(scen) == "softstep") {
            const double u = t - 5;
            g = u < 0 ? 0 : u < 0.3 ? (a / 0.3) * u : a;
        }
        else if (std::string(scen) == "gap") {
            g = t < 2 ? 0 : 4.0 * (t - 2);
            if (t >= 5.0 && t < 5.2) continue;   // dropped samples
        }
        p.feed(t, static_cast<float>(g));
        std::printf("%.3f,%.3f,%d,%d,%.3f,%.3f,%.3f,%.3f\n", t, p.kf.rate(),
                    p.kf.disturbed() ? 1 : 0, p.kf.boosting() ? 1 : 0,
                    p.kf.innovation(), p.kf.fast_level(),
                    p.kf.jump_g_diff(), p.kf.weight());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "cap") {
        bench_capture(argc > 2 ? argv[2]
                               : "captures/raw-20260930-204052.txt");
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "caploud") {
        bench_capture(argc > 2 ? argv[2]
                               : "captures/raw-20260930-204052.txt", true);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "trace") {
        trace(argc > 2 ? argv[2] : "ramp", argc > 3 ? std::atof(argv[3]) : 3.0);
        return 0;
    }
    bench_ramp();
    for (float a : {1.f, 2.f, 3.f, 5.f, 10.f, 20.f}) bench_step(a);
    for (float s : {4.f, 10.f, 20.f, 30.f})          bench_pour(s);
    bench_stop_impact();
    bench_mid_impact();
    bench_softstep();
    bench_wiggle();
    bench_wiggle2();
    bench_gap();
    return 0;
}
