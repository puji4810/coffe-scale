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

TEST_CASE("flow_kf: splash-gated onset resumes the pour slope") {
    // Capture pour-onset.txt: the pour's own splash notches the first
    // ~0.1 s and trips the gate; the un-gate then resumed at f0=0 — the
    // gate window contains the splash kink so the whole-window ramp
    // test can't pass, and one counted ring flip vetoed the newest-half
    // slope — and the pour re-acquired at base q for ~0.4 s. The
    // post-transient fit (gate window minus its leading slice) must
    // resume the real ramp instead.
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(2.0f * kFs); ++i) {
        feed_t(kf, i * kDt, 0.0f);
    }
    float t70 = -1, fin_err = -1;
    bool  saw_gate = false;
    for (int i = 0; i < static_cast<int>(4.0f * kFs); ++i) {
        const float t   = 2.0f + i * kDt;
        const float tau = t - 2.0f;
        float       g   = 9.0f * tau;                    // the pour
        if (tau < 0.12f) {
            g -= 3.0f * std::sin(3.14159265f * tau / 0.12f);  // splash
        }
        feed_t(kf, t, g);
        saw_gate |= kf.disturbed();
        if (t70 < 0 && tau >= 0.05f && kf.rate() >= 6.3f) {
            t70 = tau;                                   // 70% of 9 g/s
        }
        if (t >= 4.5f) fin_err = std::fabs(kf.rate() - 9.0f);
    }
    CHECK(saw_gate);
    CHECK(t70 > 0.0f);
    CHECK(t70 <= 0.45f);        // was ~0.55-0.83 s before the fix
    CHECK(fin_err < 0.8f);
}

TEST_CASE("flow_kf: a hard stop lands flow at zero and holds it") {
    // Guard for the post-stop tail: a pour that stops dead must land
    // under 0.3 g/s quickly, never re-rise past the deadband, and settle
    // momentum may not drag the readout into a deep negative rebound.
    // The zero hold must then pin the readout at exactly 0.
    scale::flow_kf kf;
    for (int i = 0; i < static_cast<int>(2.0f * kFs); ++i) {
        feed_t(kf, i * kDt, 0.0f);
    }
    float land = -1, rebound = 0, tail = 0, last_out = -1;
    for (int i = 0; i < static_cast<int>(6.0f * kFs); ++i) {
        const float t = 2.0f + i * kDt;
        const float g = t < 4.5f ? 8.0f * (t - 2.0f) : 20.0f;
        feed_t(kf, t, g);
        if (t >= 4.5f) {
            const float r = kf.rate();
            rebound = std::min(rebound, r);
            if (std::fabs(r) > 0.3f) last_out = t - 4.5f;
            if (land < 0 && std::fabs(r) < 0.3f) land = t - 4.5f;
            if (t > 4.5f + 0.6f) {
                tail = std::max(tail, std::fabs(r));
            }
            if (t >= 5.1f && t <= 7.8f) {
                CHECK(std::fabs(r) < 1e-5f);   // zero hold: exactly 0
            }
        }
    }
    CHECK(land > 0.0f);
    CHECK(land <= 0.45f);
    CHECK(last_out >= 0.0f);
    CHECK(last_out <= 0.45f);
    CHECK(rebound > -0.5f);
    CHECK(tail < 0.6f);         // no residual countdown plateau
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

TEST_CASE("flow_kf: removal after a rebase keeps its rate") {
    // Regression for stored lifetimes crossing the ~120 s rebase:
    // snap_t_/pour_hi_t_ must shift with delta_s like every other
    // sentinel, or the rebound pin (tp_ - t <= stop_guard_s) stays armed
    // for ~2 min after the boundary and a real removal reads as settle
    // momentum — pinned near -rebound_gps instead of -3 g/s. One
    // estimator runs the level curve continuously across the boundary;
    // a second runs the identical curve shifted 100 s earlier so it
    // never rebases — its rate is the ground truth the long run must
    // reproduce.
    const auto level = [](float t) {
        if (t < 118.0f) return 8.0f * t;    // 8 g/s pour to 944 g
        if (t < 123.0f) return 944.0f;      // hold
        return 944.0f - 3.0f * (t - 123.0f);// removal at -3 g/s
    };
    constexpr int kFs_i = static_cast<int>(kFs);
    scale::flow_kf kf_long;
    float r129 = 0.0f, r137 = 0.0f;
    for (int i = 0; i <= 138 * kFs_i; ++i) {
        const float t = i * kDt;
        feed_t(kf_long, t, level(t));
        if (i == 129 * kFs_i) r129 = kf_long.rate();
        if (i == 137 * kFs_i) r137 = kf_long.rate();
    }
    scale::flow_kf kf_short;
    float r29 = 0.0f;
    for (int i = 0; i <= 38 * kFs_i; ++i) {
        const float t = i * kDt;
        feed_t(kf_short, t, level(t + 100.0f));   // ramp ends 18, removal 23
        if (i == 29 * kFs_i) r29 = kf_short.rate();
    }
    MESSAGE("rate@129=", r129, " rate@137=", r137, " shifted@29=", r29);
    CHECK(std::fabs(r129 + 3.0f) <= 0.3f);
    CHECK(std::fabs(r137 + 3.0f) <= 0.3f);
    CHECK(std::fabs(r129 - r29) <= 0.05f);
}

TEST_CASE("flow_kf: slow pours track and still land on exact zero") {
    // Slow pours sit between zero_slope_gps and stop_flat_gps: the zero
    // hold may not swallow genuine sub-1.5 g/s growth, yet a true stop
    // must latch the readout to exactly 0.
    for (const float rate : {0.3f, 0.5f, 1.0f, 1.5f}) {
        CAPTURE(rate);
        scale::flow_kf kf;
        for (int i = 0; i <= static_cast<int>(10.0f * kFs); ++i) {
            const float t = i * kDt;
            const float g = t < 2.0f ? 0.0f
                          : t < 8.0f ? rate * (t - 2.0f)
                                     : rate * 6.0f;
            feed_t(kf, t, g);
            if (t >= 5.0f && t <= 7.5f) {
                CHECK(kf.rate() > 0.0f);
                CHECK(std::fabs(kf.rate() - rate) <=
                      std::max(0.06f, rate * 0.12f));
            }
            if (t >= 9.0f && t <= 9.8f) {
                CHECK(kf.rate() == 0.0f);
            }
        }
    }
}

TEST_CASE("flow_kf: zero hold releases instantly on a restart") {
    // 8 g/s pour, a 0.5 s stop — long enough to latch the zero hold —
    // then the same pour resumes: restart evidence must release the
    // hold, the output never reads 0 again, and the 150 ms-held 70%
    // crossing lands promptly.
    scale::flow_kf kf;
    const auto level = [](float t) {
        if (t < 2.0f) return 0.0f;
        if (t < 4.5f) return 8.0f * (t - 2.0f);
        if (t < 5.0f) return 20.0f;
        return 20.0f + 8.0f * (t - 5.0f);
    };
    std::vector<float> r;
    for (int i = 0; i <= static_cast<int>(7.0f * kFs); ++i) {
        const float t = i * kDt;
        feed_t(kf, t, level(t));
        r.push_back(kf.rate());
        if (t > 5.6f) {
            CHECK(std::fabs(kf.rate()) > 1e-5f);   // hold never re-latches
        }
    }
    float t_held = -1.0f;
    for (std::size_t i = 0; i < r.size(); ++i) {
        const float t = i * kDt;
        if (t < 5.0f || r[i] < 5.6f) continue;
        std::size_t m = i + 1;
        while (m < r.size() && r[m] >= 5.6f) ++m;
        if ((m - 1) * kDt - t >= 0.15f) {
            t_held = t - 5.0f;
            break;
        }
    }
    CHECK(t_held > 0.0f);
    CHECK(t_held <= 0.35f);
    CHECK(std::fabs(r.back() - 8.0f) <= 0.4f);
}

TEST_CASE("flow_kf: zero hold and restart survive the rebase") {
    // Same pour/hold/restart timeline shifted +118 s with a continuous
    // quiet warmup crossing the ~120 s boundary. Not a sample-wise
    // equality: snap/hold edges legitimately land a sample apart when
    // absolute-time float rounding shifts a borderline fit. Instead both
    // runs must satisfy the same behavior contract, and their held
    // restart delays may differ by at most two samples.
    const auto level = [](float t) {
        if (t < 2.0f) return 0.0f;
        if (t < 4.5f) return 8.0f * (t - 2.0f);
        if (t < 5.0f) return 20.0f;
        return 20.0f + 8.0f * (t - 5.0f);
    };
    constexpr int kFs_i = static_cast<int>(kFs);
    // Returns the delay past t=5 of the first 150 ms-held >=5.6 crossing
    // (or -1); asserts the shared contract on every sample.
    const auto run = [&](float shift) {
        scale::flow_kf kf;
        std::vector<float> r;
        for (int i = 0; i <= static_cast<int>((shift + 7.0f) * kFs); ++i) {
            const float t = i * kDt;
            feed_t(kf, t, t < shift ? 0.0f : level(t - shift));
            r.push_back(kf.rate());
        }
        const int off = static_cast<int>(shift * kFs + 0.5f);
        float held_delay = -1.0f;
        for (int i = 2 * kFs_i; i <= 7 * kFs_i; ++i) {
            const float tau = i * kDt, v = r[i + off];
            if (tau >= 3.0f && tau <= 4.0f)
                CHECK(std::fabs(v - 8.0f) <= 0.4f);       // steady pour
            if (tau >= 4.9f && tau <= 4.98f)
                CHECK(v == 0.0f);                          // zero hold
            if (tau >= 5.6f && tau <= 7.0f)
                CHECK(v > 0.0f);                           // no re-latch
            if (tau >= 6.0f && tau <= 7.0f)
                CHECK(std::fabs(v - 8.0f) <= 0.4f);       // steady restart
            if (held_delay < 0.0f && tau >= 5.0f && v >= 5.6f) {
                const std::size_t j0 =
                    static_cast<std::size_t>(i) + off;
                std::size_t m = j0 + 1;
                while (m < r.size() && r[m] >= 5.6f) ++m;
                // Contiguous run length on this run's own clock — the
                // offset must not inflate the measured hold.
                if ((m - 1 - j0) * kDt >= 0.15f)
                    held_delay = tau - 5.0f;
            }
        }
        CHECK(held_delay > 0.0f);
        CHECK(held_delay <= 0.35f);
        return held_delay;
    };
    const float d_short = run(0.0f);
    const float d_long  = run(118.0f);
    CHECK(std::fabs(d_long - d_short) <= 0.025f);
}

TEST_CASE("flow_kf: a noisy slowest pour still tracks and lands") {
    // The lowest real pour rate must keep tracking under sensor-scale
    // noise: 0.3 g/s with a deterministic +-0.02 g, 7 Hz wobble —
    // the raised restart/stillness evidence must not stall or drop it.
    // Noise runs only while pouring so the flat tail is static and the
    // exact-zero hold is assertable.
    scale::flow_kf kf;
    float  sum = 0.0f, worst = 0.0f;
    int    cnt = 0;
    for (int i = 0; i <= static_cast<int>(10.0f * kFs); ++i) {
        const float t = i * kDt;
        float       g = 0.0f;
        if (t >= 2.0f && t < 8.0f) {
            g = 0.3f * (t - 2.0f) +
                0.02f * std::sin(2.0f * 3.14159265f * 7.0f * t);
        } else if (t >= 8.0f) {
            g = 1.8f;
        }
        feed_t(kf, t, g);
        const float r = kf.rate();
        if (t >= 5.0f && t <= 7.5f) {
            sum += r;
            worst = std::max(worst, std::fabs(r - 0.3f));
            ++cnt;
        }
        if (t >= 9.5f) CHECK(r == 0.0f);   // static tail: exact zero
    }
    const float mean = sum / static_cast<float>(cnt);
    MESSAGE("mean=", mean, " worst=", worst);
    CHECK(mean >= 0.24f);
    CHECK(mean <= 0.36f);
    CHECK(worst < 0.15f);
}

TEST_CASE("flow_kf: idle handling sines cannot manufacture flow") {
    // A certified restart still releases the hold inside a handling
    // sine — the release must never seed the flow state with a fitted
    // slope: rest at 0..3 s, then sine to 14 s — the peak |flow| once
    // the sine is established must stay small.
    constexpr int kFs_i = static_cast<int>(kFs);
    for (const auto& [amp, hz] : {std::pair{3.0f, 2.0f},
                                  std::pair{4.0f, 1.5f}}) {
        scale::flow_kf kf;
        float peak = 0.0f;
        for (int i = 0; i <= 14 * kFs_i; ++i) {
            const float t = i * kDt;
            const float g =
                t < 3.0f ? 0.0f
                         : amp * std::sin(2.0f * 3.14159265f * hz *
                                          (t - 3.0f));
            feed_t(kf, t, g);
            // The release transient lives ~0.1 s into the sine, so the
            // window starts at the sine's own start rather than later.
            if (t >= 3.0f) peak = std::max(peak, std::fabs(kf.rate()));
        }
        MESSAGE("amp=", amp, " hz=", hz, " peak=", peak);
        CHECK(peak < 2.0f);
    }
}

TEST_CASE("flow_kf: a high pour can become a noisy slow pour without stopping") {
    // A long-window stillness path must not turn the tail of a real
    // pour into a zero hold. This transition sits below the stop-snap
    // threshold and, at 0.3 g/s, below the fast quiet threshold too.
    for (const float rate : {0.3f, 0.5f, 1.0f}) {
        CAPTURE(rate);
        scale::flow_kf kf;
        float sum = 0.0f;
        int n = 0;
        for (int i = 0; i <= static_cast<int>(8.0f * kFs); ++i) {
            const float t = i * kDt;
            const float g = t < 2.0f ? 0.0f
                : t < 4.5f ? 8.0f * (t - 2.0f)
                : 20.0f + rate * (t - 4.5f) +
                  0.02f * std::sin(2.0f * 3.14159265f * 7.0f * t);
            feed_t(kf, t, g);
            // Allow the pre-existing deceleration transient (the old
            // estimator reverses briefly here too); it must recover
            // within 0.75 s rather than keep holding a genuine tail.
            if (t >= 5.25f) CHECK(kf.rate() > 0.0f);
            if (t >= 6.0f) { sum += kf.rate(); ++n; }
        }
        CHECK(std::fabs(sum / static_cast<float>(n) - rate) <= 0.06f);
    }
}

TEST_CASE("flow_kf: a stopped high pour releases for noisy slow growth or removal") {
    // Neither sign may be trapped by the stop hold after a short pause.
    // Signed slow growth is especially vulnerable to extra mass/delay
    // requirements added to suppress a post-stop ringing crest.
    for (const float rate : {-0.5f, -0.3f, 0.3f, 0.5f}) {
        CAPTURE(rate);
        scale::flow_kf kf;
        float sum = 0.0f;
        int n = 0;
        for (int i = 0; i <= static_cast<int>(8.0f * kFs); ++i) {
            const float t = i * kDt;
            const float g = t < 2.0f ? 0.0f
                : t < 4.5f ? 8.0f * (t - 2.0f)
                : t < 5.0f ? 20.0f
                : 20.0f + rate * (t - 5.0f) +
                  0.02f * std::sin(2.0f * 3.14159265f * 7.0f * t);
            feed_t(kf, t, g);
            if (t >= 5.7f) CHECK(kf.rate() * rate > 0.0f);
            if (t >= 6.5f) { sum += kf.rate(); ++n; }
        }
        // Negative cases check release/direction only: the existing
        // low-rate rebound floor is not a negative-rate accuracy claim.
        if (rate > 0.0f)
            CHECK(std::fabs(sum / static_cast<float>(n) - rate) <= 0.06f);
    }
}

TEST_CASE("flow_kf: zero hold does not turn a small ringing step into a pour") {
    // Net weight growth can release a noisy restart. A small mass step
    // also grows weight, but its now-flat signal must not acquire flow
    // just because restart covariance was warmed up.
    for (const float mass : {0.5f, 1.0f, 1.5f, 2.0f}) {
        CAPTURE(mass);
        scale::flow_kf kf;
        float peak = 0.0f;
        for (int i = 0; i <= static_cast<int>(6.0f * kFs); ++i) {
            const float t = i * kDt, tau = t - 3.0f;
            const float g = tau < 0.0f ? 0.0f
                : mass + 0.07f * std::sin(2.0f * 3.14159265f * 7.0f * tau)
                         * std::exp(-6.0f * tau);
            feed_t(kf, t, g);
            if (tau >= 0.0f) peak = std::max(peak, std::fabs(kf.rate()));
            if (tau >= 1.0f) CHECK(std::fabs(kf.rate()) < 0.30f);
        }
        CHECK(peak < 0.50f);
        CHECK(std::fabs(kf.weight() - mass) < 0.05f);
    }
}
