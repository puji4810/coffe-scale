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

TEST_CASE("flow_kf: parity with python reference output") {
    // Prefer the hold_s=1.0, ms-quantised regeneration (matches this
    // config/API); fall back to the original hold_s=0.6 reference.
    std::FILE* f = std::fopen("/tmp/est/ref_flowkf_h10_ms.csv", "r");
    if (!f) f = std::fopen("/tmp/est/ref_flowkf.csv", "r");
    if (!f) {
        MESSAGE("ref_flowkf.csv not present — parity test skipped");
        return;
    }
    scale::flow_kf kf;
    char   line[256];
    REQUIRE(std::fgets(line, sizeof line, f));   // header
    double max_f = 0, max_w = 0;
    int    mism = 0, n = 0;
    std::vector<double> fdiff;
    while (std::fgets(line, sizeof line, f)) {
        double t, g, ef, ew;
        int    eg;
        if (std::sscanf(line, "%lf,%lf,%lf,%lf,%d", &t, &g, &ef, &ew, &eg)
            != 5) {
            continue;
        }
        feed_t(kf, static_cast<float>(t), static_cast<float>(g));
        const double df = std::fabs(kf.rate() - ef);
        const double dw = std::fabs(kf.weight() - ew);
        fdiff.push_back(df);
        if (df > max_f) max_f = df;
        if (dw > max_w) max_w = dw;
        if (kf.disturbed() != (eg != 0)) ++mism;
        ++n;
    }
    std::fclose(f);
    std::sort(fdiff.begin(), fdiff.end());
    const double p99 = fdiff[static_cast<std::size_t>(n * 0.99)];
    MESSAGE("parity: n=", n, " max|flow|=", max_f, " p99|flow|=", p99,
            " max|w|=", max_w, " gated mismatch=", mism);
    CHECK(n > 40000);
    // ms-quantised timestamps shift the rewind's >=rewind_s history pick
    // by one sample at boundaries — the held flow then differs for the
    // gate window. Transition times still match to the sample.
    CHECK(p99 < 0.15);
    CHECK(mism <= 10);
}

TEST_CASE("flow_kf: ramp latency and stop") {
    scale::flow_kf kf;
    std::vector<float> r;
    for (int i = 0; i < static_cast<int>(10.0f * kFs); ++i) {
        const float t = i * kDt;
        feed_t(kf, t, ramp_w(t));
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
    CHECK(t_rise >= 0.60f);
    CHECK(t_rise <= 0.72f);
    CHECK(overshoot < 0.3f);
    CHECK(t_fall >= 0.0f);
    CHECK(t_fall <= 0.72f);
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
    CHECK(t_rise >= 0.60f);
    CHECK(t_rise <= 0.72f);
    CHECK(overshoot < 0.3f);
    CHECK(t_fall >= 0.0f);
    CHECK(t_fall <= 0.72f);
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
        if (t >= 3.0f && t <= 10.0f) {   // 1 s after ramp start to end
            CHECK(std::fabs(kf.rate() - 4.0f) < 1.5f);
        }
    }
    CHECK(!kf.disturbed());
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
