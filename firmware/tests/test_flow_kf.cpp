// flow_kf: parity against the Python reference output (skipped when the
// file is absent — it lives on the developer's machine, not in CI) plus
// synthetic behaviour tests: ramp latency, step/impact rejection, reset,
// long-uptime precision, rebase boundary.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "scale/flow_kf.hpp"

namespace {

constexpr float kFs = 80.0f;
constexpr float kDt = 1.0f / kFs;

// Weight at time t (seconds since test start): rest 2 s, 4 g/s ramp to
// t=8, hold at 24 g.
float ramp_w(float t) {
    if (t < 2.0f) return 0.0f;
    if (t < 8.0f) return 4.0f * (t - 2.0f);
    return 24.0f;
}

// Push a seconds-valued test time as an integer ms timestamp (the
// firmware clock is ms anyway), offset by `base`.
void feed_t(scale::flow_kf& kf, float t_s, float g,
            std::int64_t base_ms = 0) {
    kf.push(scale::clock_ms{static_cast<std::int64_t>(std::llround(
                            static_cast<double>(t_s) * 1000.0)) +
                            base_ms},
            g);
}

} // namespace

TEST_CASE("flow_kf: capture regression bounds") {
    // The old parity golden froze the previous estimator's outputs;
    // what matters on the same capture is the disturbance behaviour —
    // quiet at rest, small residuals around cup/tap events. The file
    // lives on the developer's machine; skip cleanly when absent.
    std::FILE* f = std::fopen("/tmp/est/ref_flowkf_h10_ms.csv", "r");
    if (!f) f = std::fopen("/tmp/est/ref_flowkf.csv", "r");
    if (!f) {
        MESSAGE("ref_flowkf.csv not present — capture test skipped");
        return;
    }
    scale::flow_kf kf;
    char           line[256];
    REQUIRE(std::fgets(line, sizeof line, f));   // header
    struct win { double a, b, sum, sum2, peak; int n; };
    win rest{140, 170, 0, 0, 0, 0}, cups{450, 490, 0, 0, 0, 0},
        taps{492, 514, 0, 0, 0, 0};
    int n = 0;
    while (std::fgets(line, sizeof line, f)) {
        double t, g, ef, ew;
        int    eg;
        if (std::sscanf(line, "%lf,%lf,%lf,%lf,%d", &t, &g, &ef, &ew, &eg)
            != 5) {
            continue;
        }
        feed_t(kf, static_cast<float>(t), static_cast<float>(g));
        const double r = std::fabs(kf.rate()) < 0.3 ? 0.0 : kf.rate();
        for (win* w : {&rest, &cups, &taps}) {
            if (t >= w->a && t < w->b) {
                const double d = std::clamp(r, -30.0, 30.0);
                w->sum  += d;
                w->sum2 += d * d;
                w->peak = std::max(w->peak, std::fabs(d));
                ++w->n;
            }
        }
        ++n;
    }
    std::fclose(f);
    CHECK(n > 40000);
    const double rest_mean = rest.sum / rest.n;
    const double rest_std =
        std::sqrt(rest.sum2 / rest.n - rest_mean * rest_mean);
    MESSAGE("capture: rest std=", rest_std, " cups peak=", cups.peak,
            " taps peak=", taps.peak);
    CHECK(rest_std < 0.15);   // old baseline measured ~0.06 g/s
    CHECK(cups.peak < 4.0);   // was ~8.25 g/s on the old estimator
    CHECK(taps.peak < 3.0);   // was ~1.05 g/s
}

TEST_CASE("flow_kf: ramp latency and stop") {
    scale::flow_kf kf;
    std::vector<float> r;
    for (int i = 0; i < static_cast<int>(10.0f * kFs); ++i) {
        const float t = i * kDt;
        feed_t(kf, t, ramp_w(t));
        r.push_back(kf.rate());
    }
    float t_rise = -1, t_fall = -1, overshoot = 0, undershoot = 0;
    float settle = -1, ssettle = -1;
    for (std::size_t i = 0; i < r.size(); ++i) {
        const float t = i * kDt;
        if (t_rise < 0 && t >= 2.0f && r[i] >= 3.6f) t_rise = t - 2.0f;
        if (t >= 2.0f && t < 8.0f) {
            if (r[i] - 4.0f > overshoot) overshoot = r[i] - 4.0f;
            if (std::fabs(r[i] - 4.0f) > 0.4f) settle = t - 2.0f;
        }
        if (t_fall < 0 && t >= 8.0f && r[i] <= 0.4f) t_fall = t - 8.0f;
        if (t >= 8.0f) {
            if (r[i] < undershoot) undershoot = r[i];
            if (std::fabs(r[i]) > 0.4f) ssettle = t - 8.0f;
        }
    }
    CHECK(t_rise >= 0.10f);
    CHECK(t_rise <= 0.45f);   // dynamic-q target: well under the old 0.66
    // First-crossing t90 is not the whole story — the ±10% settle time
    // and the overshoot are asserted too, so speed may not be bought
    // with a ringing readout.
    CHECK(settle >= 0.0f);
    CHECK(settle <= 0.50f);
    CHECK(overshoot < 0.5f);
    CHECK(t_fall >= 0.0f);
    CHECK(t_fall <= 0.45f);
    CHECK(ssettle <= 0.50f);
    CHECK(undershoot > -0.6f);   // stop may not bounce back hard
    CHECK(!kf.disturbed());
}

TEST_CASE("flow_kf: same latency after 10 days of uptime") {
    // The estimator takes integer ms — a float of seconds since boot
    // would have a ~60 ms ulp here.
    constexpr std::int64_t kBase = 864'000'000LL;   // 10 days in ms
    scale::flow_kf         kf;
    std::vector<float>     r;
    for (int i = 0; i < static_cast<int>(10.0f * kFs); ++i) {
        const float t = i * kDt;
        feed_t(kf, t, ramp_w(t), kBase);
        r.push_back(kf.rate());
    }
    float t_rise = -1, t_fall = -1, overshoot = 0;
    for (std::size_t i = 0; i < r.size(); ++i) {
        const float t = i * kDt;
        if (t_rise < 0 && t >= 2.0f && r[i] >= 3.6f) t_rise = t - 2.0f;
        if (t >= 2.0f && t < 8.0f && r[i] - 4.0f > overshoot)
            overshoot = r[i] - 4.0f;
        if (t_fall < 0 && t >= 8.0f && r[i] <= 0.4f) t_fall = t - 8.0f;
    }
    CHECK(t_rise >= 0.10f);
    CHECK(t_rise <= 0.45f);
    CHECK(overshoot < 0.5f);
    CHECK(t_fall >= 0.0f);
    CHECK(t_fall <= 0.45f);
}

TEST_CASE("flow_kf: step load is rejected, weight still lands") {
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(5.0f * kFs); ++i) {
        feed_t(kf, i * kDt, 0.0f);
    }
    float max_rate = 0;
    bool  saw_gate = false;
    float t_ungate = -1;
    for (int i = 0; i < static_cast<int>(3.0f * kFs); ++i) {
        const float t   = 5.0f + i * kDt;
        const float tau = t - 5.0f;
        float       g   = 300.0f;
        if (tau < 0.6f) {
            g += 40.0f * std::sin(2 * 3.14159265f * 12.0f * tau)
                 * std::exp(-6.0f * tau);
        }
        feed_t(kf, t, g);
        max_rate = std::max(max_rate, std::fabs(kf.rate()));
        if (kf.disturbed()) {
            saw_gate = true;
        } else if (saw_gate && t_ungate < 0) {
            t_ungate = tau;
        }
    }
    CHECK(max_rate < 0.5f);
    CHECK(saw_gate);
    CHECK(t_ungate >= 0.0f);
    CHECK(t_ungate <= 1.0f);
    CHECK(std::fabs(kf.weight() - 300.0f) < 0.5f);
}

TEST_CASE("flow_kf: impact burst mid-pour is gated out") {
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(2.0f * kFs); ++i) {
        feed_t(kf, i * kDt, 0.0f);
    }
    // 4 g/s ramp for 8 s; 0.4 s of +-40 g 12 Hz burst mid-ramp (t=5..5.4)
    for (int i = 0; i < static_cast<int>(8.0f * kFs); ++i) {
        const float t   = 2.0f + i * kDt;
        float       g   = 4.0f * (t - 2.0f);
        const float tau = t - 5.0f;
        if (tau >= 0.0f && tau < 0.4f) {
            g += 40.0f * std::sin(2 * 3.14159265f * 12.0f * tau);
        }
        feed_t(kf, t, g);
        // During the gate the held flow decays toward 0 by design (a
        // stop mid-burst shouldn't look frozen) — bound the dip, then
        // require the pour back after recovery below.
        if (t >= 3.0f && t <= 10.0f) {
            CHECK(std::fabs(kf.rate() - 4.0f) < 3.5f);
        }
    }
    CHECK(!kf.disturbed());
    CHECK(std::fabs(kf.rate() - 4.0f) < 0.5f);
}

TEST_CASE("flow_kf: gate trip just after a rebase still rewinds") {
    // Rebase happens when the anchored offset crosses 120 s. Run a pour
    // across that boundary and trip the gate right after it — history
    // must still be there to rewind to. Slow 2 g/s pour from t=118 on
    // (ramp_w holds at 24 g after t=8).
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(121.0f * kFs); ++i) {
        const float t = i * kDt;
        const float g = t < 118.0f ? ramp_w(t)
                                   : 24.0f + 2.0f * (t - 118.0f);
        feed_t(kf, t, g);
    }
    const float pre_rate = kf.rate();
    CHECK(pre_rate > 1.5f);
    // burst for 30 ms right across/just after the rebase
    for (int i = static_cast<int>(121.0f * kFs);
         i < static_cast<int>(121.1f * kFs); ++i) {
        const float t = i * kDt;
        feed_t(kf, t, 24.0f + 2.0f * (t - 118.0f) +
                          40.0f * std::sin(2 * 3.14159265f * 12.0f *
                                           (t - 121.0f)));
    }
    // gate must be engaged and the held rate near the pour rate —
    // a lost history would show the burst or 0 immediately
    CHECK(kf.disturbed());
    CHECK(std::fabs(kf.rate() - pre_rate) < 1.5f);
}

TEST_CASE("flow_kf: reset re-primes on the next push") {
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(4.0f * kFs); ++i) {
        feed_t(kf, i * kDt, ramp_w(i * kDt));
    }
    CHECK(kf.rate() > 3.0f);
    kf.reset();
    kf.push(scale::clock_ms{100'000}, 42.0f);
    CHECK(kf.rate() == 0.0f);
    CHECK(kf.weight() == doctest::Approx(42.0f));
}

TEST_CASE("flow_kf: a confirmed pour engages dynamic tracking") {
    // The 4 g/s ramp must at some point switch to fast tracking —
    // that's what buys the sub-0.3 s t90. boost may blink in/out, so
    // only require it was seen; and the estimator must settle on 4.
    scale::flow_kf kf;
    bool           boosted = false;
    for (int i = 0; i < static_cast<int>(10.0f * kFs); ++i) {
        const float t = i * kDt;
        feed_t(kf, t, ramp_w(t));
        boosted = boosted || kf.boosting();
    }
    CHECK(boosted);
}

TEST_CASE("flow_kf: 3 g step is gated, no flow tail") {
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(5.0f * kFs); ++i) {
        feed_t(kf, i * kDt, 0.0f);
    }
    float peak = 0;
    bool  gated = false;
    for (int i = 0; i < static_cast<int>(3.0f * kFs); ++i) {
        const float t = 5.0f + i * kDt;
        feed_t(kf, t, 3.0f);
        peak = std::max(peak, std::fabs(kf.rate()));
        gated = gated || kf.disturbed();
    }
    CHECK(gated);
    CHECK(peak < 1.0f);            // old code read ~6 g/s here
    CHECK(kf.rate() < 0.5f);       // nothing survives after the gate
    CHECK(std::fabs(kf.weight() - 3.0f) < 0.5f);
}

TEST_CASE("flow_kf: sustained 30 g/s pour reaches the rate") {
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(2.0f * kFs); ++i) {
        feed_t(kf, i * kDt, 0.0f);
    }
    for (int i = 0; i < static_cast<int>(4.0f * kFs); ++i) {
        const float t = 2.0f + i * kDt;
        feed_t(kf, t, 30.0f * (t - 2.0f));
    }
    // The onset may gate once (it outruns the boost proof), but the
    // un-gate resume must land on the real slope, not get stuck.
    CHECK(std::fabs(kf.rate() - 30.0f) < 3.0f);
}

TEST_CASE("flow_kf: oscillating handling is not a pour") {
    // +-2 g at 1.5 Hz for 5 s — violent handling. Some short leakage is
    // unavoidable, but it must never lock onto the ~19 g/s fitted
    // slope or cycle the gate open and shut forever.
    scale::flow_kf kf;
    float          peak = 0;
    int            boosts = 0;
    for (int i = 0; i < static_cast<int>(8.0f * kFs); ++i) {
        const float t = i * kDt;
        const float g = t < 3.0f ? 0.0f
                        : 2.0f * std::sin(2 * 3.14159265f * 1.5f *
                                          (t - 3.0f));
        feed_t(kf, t, g);
        if (t >= 3.0f) {
            peak = std::max(peak, std::fabs(kf.rate()));
            if (kf.boosting()) ++boosts;
        }
    }
    CHECK(peak < 8.0f);
    CHECK(boosts <= 4);
}

TEST_CASE("flow_kf: 200 ms sample hole mid-pour recovers") {
    scale::flow_kf kf;
    float peak_err = 0;
    for (int i = 0; i < static_cast<int>(10.0f * kFs); ++i) {
        const float t = i * kDt;
        if (t >= 5.0f && t < 5.2f) continue;   // dropped samples
        const float g = t < 2.0f ? 0.0f : 4.0f * (t - 2.0f);
        feed_t(kf, t, g);
        if (t >= 5.2f) peak_err = std::max(peak_err,
                                           std::fabs(kf.rate() - 4.0f));
    }
    CHECK(peak_err < 2.0f);
    CHECK(std::fabs(kf.rate() - 4.0f) < 0.5f);
}

TEST_CASE("flow_kf: gate ending near a stop leaves no stale flow") {
    // 4 g/s pour that stops flat at t=5 while an impact burst rings —
    // the recovery must see the flat half-window and not restore the
    // old 4 g/s for a long tail.
    scale::flow_kf kf;
    float t_tail = -1.0f;
    for (int i = 0; i < static_cast<int>(9.0f * kFs); ++i) {
        const float t   = i * kDt;
        float       g   = t < 2.0f ? 0.0f : t < 5.0f ? 4.0f * (t - 2.0f)
                                                     : 12.0f;
        const float tau = t - 5.0f;
        if (tau >= 0.0f && tau < 0.4f) {
            g += 40.0f * std::sin(2 * 3.14159265f * 12.0f * tau);
        }
        feed_t(kf, t, g);
        if (t > 5.4f && t_tail < 0.0f && std::fabs(kf.rate()) < 0.5f) {
            t_tail = t - 5.0f;
        }
    }
    CHECK(t_tail > 0.0f);
    CHECK(t_tail < 1.2f);   // was ~1.8 s on the first recovery design
}

TEST_CASE("flow_kf: a hard step holds the gate, never same-sample open") {
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(5.0f * kFs); ++i) {
        feed_t(kf, i * kDt, 0.0f);
    }
    int   gate_run = 0, max_run = 0;
    float t_un = -1.0f;
    for (int i = 0; i < static_cast<int>(3.0f * kFs); ++i) {
        const float t = 5.0f + i * kDt;
        feed_t(kf, t, 10.0f);
        if (kf.disturbed()) {
            ++gate_run;
            max_run = std::max(max_run, gate_run);
        } else {
            if (gate_run > 0 && t_un < 0.0f) t_un = t - 5.0f;
            gate_run = 0;
        }
    }
    // A level jump can neither trip-and-release in one sample nor stay
    // locked for seconds — it resolves on the calm trailing window.
    CHECK(max_run >= 4);          // >= 50 ms contiguous
    CHECK(t_un > 0.15f);
    CHECK(t_un < 1.0f);
    CHECK(std::fabs(kf.weight() - 10.0f) < 0.5f);
}

TEST_CASE("flow_kf: sustained oscillation never resumes as flow") {
    // +-3 g at 2 Hz for 10 s with no pour: the sine's slope reaches
    // ~37 g/s, which blind tracking would follow to ~24 g/s and an
    // un-gated fit could adopt. The steep-slope gate plus the
    // oscillation bookkeeping must keep the readout near zero.
    scale::flow_kf kf;
    float peak = 0;
    for (int i = 0; i < static_cast<int>(14.0f * kFs); ++i) {
        const float t = i * kDt;
        const float g = t < 3.0f
            ? 0.0f
            : 3.0f * std::sin(2.0f * 3.14159265f * 2.0f * (t - 3.0f));
        feed_t(kf, t, g);
        if (t >= 3.5f) peak = std::max(peak, std::fabs(kf.rate()));
    }
    CHECK(peak < 2.0f);   // was ~24 on the slope-adoption hole
}

TEST_CASE("flow_kf: rebase keeps gate policy and live timers valid") {
    // ONE estimator fed continuously past the ~120 s rebase — a fresh
    // estimator per offset (the old version of this test) never crosses
    // it, since tp_ restarts near 0. Here a step gates just before the
    // boundary, a 4 g/s pour crosses it with tracking state live, and a
    // second step gates just after: stored times that cross zero must
    // stay valid (elapsed math), sentinels never become "now".
    scale::flow_kf kf;
    const auto level = [](float t) {
        float g = 0.0f;
        if (t >= 118.5f) g += 2.0f;                 // pre-rebase step
        if (t >= 119.3f) g += 4.0f * (t - 119.3f);  // pour across 120
        if (t >= 121.8f) g += 6.0f;                 // post-rebase step
        return g;
    };
    bool  gated_pre = false, gated_post = false;
    float pour_err = 0.0f;
    for (int i = 0; i < static_cast<int>(123.0f * kFs); ++i) {
        const float t = i * kDt;
        feed_t(kf, t, level(t));
        if (t >= 118.6f && t < 119.2f && kf.disturbed()) gated_pre = true;
        if (t >= 120.05f && t < 121.75f)
            pour_err = std::max(pour_err, std::fabs(kf.rate() - 4.0f));
        if (t >= 121.85f && kf.disturbed()) gated_post = true;
    }
    CHECK(gated_pre);
    CHECK(gated_post);
    CHECK(pour_err < 1.0f);
}

TEST_CASE("flow_kf: oscillation then quiet then a real pour") {
    // +-4 g at 1.5 Hz for 6 s, 1.5 s quiet, then a genuine 20 g/s pour:
    // the oscillation streak must expire during the quiet gap so the
    // pour resumes as flow promptly instead of staying suppressed.
    scale::flow_kf kf;
    float peak_osc = 0, t_half = -1, fin_err = 0;
    for (int i = 0; i < static_cast<int>(16.0f * kFs); ++i) {
        const float t = i * kDt;
        float g = 0.0f;
        if (t < 9.0f)
            g = 4.0f * std::sin(2.0f * 3.14159265f * 1.5f * (t - 3.0f));
        else if (t >= 10.5f)
            g = 20.0f * (t - 10.5f);   // pour (quiet gap 9.0..10.5)
        feed_t(kf, t, g);
        if (t >= 3.0f && t < 10.0f)
            peak_osc = std::max(peak_osc, std::fabs(kf.rate()));
        if (t_half < 0 && t >= 10.5f && kf.rate() >= 10.0f)
            t_half = t - 10.5f;
        if (t >= 13.0f) fin_err = std::fabs(kf.rate() - 20.0f);
    }
    CHECK(peak_osc < 2.0f);      // oscillation never reads as flow
    CHECK(t_half > 0.0f);        // pour actually acquired
    CHECK(t_half < 1.5f);        // ...within a sane response window
    CHECK(fin_err < 1.0f);
}
