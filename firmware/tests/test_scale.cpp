#include <doctest/doctest.h>

#include <cmath>

#include "scale/app.hpp"
#include "scale/scale.hpp"
#include "scale/telemetry.hpp"

using namespace scale;

namespace {
/// Feed `n` pushes on a virtual 10 ms grid, advancing `t`.
void feed_n(scale::scale& s, std::int32_t counts, int n, clock_ms& t) {
    for (int i = 0; i < n; ++i) {
        s.push(counts, t);
        t += clock_ms{10};
    }
}
} // namespace

TEST_CASE("scale: calibrated grams from counts") {
    scale::scale s;
    s.load_calibration({.zero_counts = 1'000.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 1'100, 20, t);
    CHECK(s.grams() == doctest::Approx(1.0f));
}

TEST_CASE("scale: tare zeroes the display") {
    scale::scale s;
    s.load_calibration({.zero_counts = 1'000.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 2'000, 20, t);
    s.tare();
    CHECK(s.tared());
    CHECK(s.grams() == doctest::Approx(0.0f).epsilon(0.001));
    feed_n(s, 2'050, 80, t);   // +0.5 g after tare; resting EMA needs ~1 s tail
    CHECK(s.grams() == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("scale: display deadband pins sub-division noise to 0") {
    scale::scale s{{.lpf = lpf_kind::none}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    // ±4 counts = ±0.04 g noise band, straddling zero -> display stays 0
    for (int i = 0; i < 60; ++i) {
        s.push(i % 2 ? 4 : -4, t);
        t += clock_ms{10};
    }
    CHECK(s.grams() == 0.0f);
    CHECK_FALSE(std::signbit(s.grams()));          // never "-0.0"
    // a real 0.2 g object is well above the 0.05 g band
    feed_n(s, 20, 20, t);
    CHECK(s.grams() == doctest::Approx(0.2f));
}

TEST_CASE("scale: two-point calibration roundtrip") {
    scale::scale s;
    clock_ms t{0};
    feed_n(s, 500, 100, t);    // enough pushes for the EMA to converge
    s.cal_zero();
    feed_n(s, 4'500, 100, t);
    CHECK(s.cal_span(100.0f));
    CHECK(s.calibration_data().counts_per_gram == doctest::Approx(40.f));

    feed_n(s, 2'500, 100, t);
    CHECK(s.grams() == doctest::Approx(50.0f));
}

TEST_CASE("scale: cal_span rejects nonsensical input") {
    scale::scale s;
    clock_ms t{0};
    feed_n(s, 500, 20, t);
    s.cal_zero();
    CHECK(!s.cal_span(0.0f));    // no mass
    CHECK(!s.cal_span(-10.f));   // negative mass
    CHECK(!s.cal_span(100.f));   // no delta from zero point
}

TEST_CASE("scale: plateau snap lands the display without the alpha tail") {
    scale::scale s;   // default adaptive EMA
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 0, 100, t);             // settle at 0
    feed_n(s, 10'000, 12, t);         // +100 g step; median quiet after ~3
    // snap fires once the 8-sample input window is quiet: exact plateau,
    // not the ~0.5 g residual the resting alpha would still be crawling.
    CHECK(s.grams() == doctest::Approx(100.f).epsilon(0.001));
}

TEST_CASE("scale: plateau snap does not fire mid-pour") {
    scale::scale s;
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 0, 100, t);
    // 10 g/s ramp: input spread over the snap window is ~0.8 g — never
    // quiet, so the EMA keeps its tracking lag (~0.4 g for alpha 0.25).
    for (int i = 1; i <= 150; ++i) {
        s.push(i * 10, t);
        t += clock_ms{10};
    }
    CHECK(s.grams() > 13.0f);         // tracking, not stuck
    CHECK(s.grams() < 14.8f);         // still trailing: no snap glued it
}

TEST_CASE("scale: plateau snap stays off during a slow pour") {
    scale::scale s;   // default adaptive EMA
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 0, 100, t);
    // Noiseless 1.5 g/s ramp: the input spread stays under snap_spread_g,
    // but the pour is real — the snap must not keep jumping the filter
    // onto the trailing mean (sawtooth). True step: 0.015 g/sample.
    float prev = s.grams();
    for (int i = 1; i <= 350; ++i) {
        s.push(i * 3 / 2, t);
        t += clock_ms{10};
        const float g = s.grams();
        if (i > 50) {
            CHECK(g - prev <= 0.04f);
        }
        prev = g;
    }
}

TEST_CASE("scale: display_grams hysteresis holds the digit near boundaries") {
    scale::scale s{{.lpf = lpf_kind::none, .latch_hold = clock_ms{0}}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 1'004, 30, t);          // 10.04 g -> shows 10.0
    CHECK(s.display_grams() == doctest::Approx(10.0f));
    // ±0.04 g wiggle straddling the boundary: digit must not move
    for (int i = 0; i < 40; ++i) {
        s.push(i % 2 ? 1'002 : 1'006, t);
        t += clock_ms{10};
        CHECK(s.display_grams() == doctest::Approx(10.0f));
    }
    // leaving the band re-quantises; re-entering it holds the new digit
    feed_n(s, 1'008, 5, t);           // 10.08 g -> shows 10.1
    CHECK(s.display_grams() == doctest::Approx(10.1f));
    feed_n(s, 1'004, 5, t);           // back to 10.04: still inside band
    CHECK(s.display_grams() == doctest::Approx(10.1f));
    feed_n(s, 1'002, 5, t);           // 10.02: past the band -> 10.0
    CHECK(s.display_grams() == doctest::Approx(10.0f));
    // a real step re-quantises immediately
    feed_n(s, 1'020, 5, t);
    CHECK(s.display_grams() == doctest::Approx(10.2f));
}

TEST_CASE("scale: lag compensation removes pour tracking error") {
    scale::scale s;   // default adaptive EMA; latch stays disarmed mid-pour
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 0, 100, t);
    for (int i = 1; i <= 150; ++i) {          // 10 g/s, still pouring
        s.push(i * 10, t);
        t += clock_ms{10};
    }
    CHECK(s.grams() < 14.8f);                 // the LPF still trails
    // display adds the measured lag back -> lands on the true weight;
    // the flow band caps the lead slightly (~0.1 g at 10 g/s)
    CHECK(s.display_grams() >= 14.3f);
    CHECK(s.display_grams() <= 15.1f);
}

TEST_CASE("scale: lag compensation stays off at rest") {
    scale::scale s;
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 5'000, 200, t);                 // quiet 50 g
    CHECK(s.display_grams() == doctest::Approx(50.f).epsilon(0.001));
}

TEST_CASE("scale: display latch freezes while stable and releases on drift") {
    scale::scale s{{.lpf = lpf_kind::none,
                    .stability_window = 8,
                    .latch_hold = clock_ms{100}}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 5'000, 40, t);                  // 400 ms still -> latched
    CHECK(s.display_grams() == doctest::Approx(50.0f));
    // +0.05 g: under the stability tol and the release threshold —
    // the display keeps holding the frozen value (dead-calm readout)
    feed_n(s, 5'005, 10, t);
    CHECK(s.display_grams() == doctest::Approx(50.0f));
    // drifting past release_g unlocks and re-quantises
    feed_n(s, 5'012, 10, t);
    CHECK(s.display_grams() == doctest::Approx(50.1f));
    // a real step breaks stability -> live again immediately
    feed_n(s, 6'000, 10, t);
    CHECK(s.display_grams() == doctest::Approx(60.0f));
}

TEST_CASE("scale: latch holds the digit at a rounding boundary") {
    // Weight parked on 10.05 g +/- 0.01: the hysteretic readout never
    // leaves 10.0, so the latch must freeze that digit — not chase the
    // plain-rounded value across the boundary.
    scale::scale s{{.lpf = lpf_kind::none, .stability_window = 8,
                    .latch_hold = clock_ms{100}}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    float held = 0.0f;
    for (int i = 0; i < 300; ++i) {
        s.push(i % 2 ? 1'006 : 1'004, t);
        t += clock_ms{10};
        if (i == 50) {
            held = s.display_grams();
        } else if (i > 50) {
            CHECK(s.display_grams() == held);
        }
    }
}

TEST_CASE("scale: latched digit releases when zero-track eats the residual") {
    // Removing a load can leave a small stable residual (+0.2 g). The latch
    // freezes it, the tracker then pulls grams() to 0 — the shown step must
    // follow instead of sitting stale until an instability releases it.
    scale::scale s{{.lpf = lpf_kind::none,
                    .stability_window = 8,
                    .latch_hold = clock_ms{100},
                    .zero_track = {.enabled = true, .band_g = 0.5f,
                                   .rate = 0.05f, .hold = clock_ms{200},
                                   .max_g = 2.0f}}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 20, 15, t);                  // 0.2 g residual, tracker arming
    CHECK(s.display_grams() == doctest::Approx(0.2f));
    feed_n(s, 20, 400, t);                 // hold + absorb -> back to 0
    CHECK(s.display_grams() == doctest::Approx(0.0f));
}

TEST_CASE("scale: flow settles after a pour stops") {
    scale::scale s{{.lpf = lpf_kind::none}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    for (int i = 1; i <= 150; ++i) {          // 10 g/s pour for 1.5 s
        s.push(i * 10, t);
        t += clock_ms{10};
    }
    CHECK(s.flow_gps() == doctest::Approx(10.f).epsilon(0.2));
    // fixed 1.0 s window: the ramp tail decays inside ~1 s (100 samples)
    int n = 0;
    while (n < 200 && s.flow_gps() != 0.0f) {
        s.push(1'500, t);
        t += clock_ms{10};
        ++n;
    }
    CHECK(n < 120);
}

TEST_CASE("scale: flow does not drop out mid slow pour") {
    scale::scale s;
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 0, 100, t);
    // 1.5 g/s pour (1.5 counts/sample) + a noise burst pattern: ten
    // near-quiet samples then ten alternating +/-18 counts (big enough to
    // survive the median-3). The median spread crosses the quiet
    // threshold every cycle, but the readout must keep reporting the
    // pour — never collapse to 0.
    static const int noise[20] = {+2, -1, +3, -2, +1, -3, +2, -1, +2, -3,
                                  +18, -18, +18, -18, +18,
                                  -18, +18, -18, +18, -18};
    for (int i = 1; i <= 450; ++i) {
        s.push(i * 3 / 2 + noise[i % 20], t);
        t += clock_ms{10};
        if (i > 150) {
            const float f = s.flow_gps();
            CHECK(f >= 1.0f);
            CHECK(f <= 2.0f);
        }
    }
}

TEST_CASE("scale: pour produces a positive flow rate") {
    scale::scale s;
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 0, 100, t);      // settle at 0
    // pour at 5 g/s: +500 counts/s -> +5 counts per 10 ms sample
    for (int i = 1; i <= 150; ++i) {
        s.push(i * 5, t);
        t += clock_ms{10};
    }
    CHECK(s.flow_gps() == doctest::Approx(5.f).epsilon(0.2));
    CHECK(!s.stable());        // pouring is not a stable weight
}

TEST_CASE("scale: dropped conversions don't inflate flow") {
    // 25 % of DRDY edges never reach push(). A virtual sample-index time
    // base would compress the axis and overestimate the slope by 1/0.75;
    // real timestamps keep the regression honest.
    scale::scale s{{.lpf = lpf_kind::none}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    for (int i = 0; i < 120; ++i) {
        if (i % 4 == 3) {              // dropped conversion
            t += clock_ms{10};
            continue;
        }
        s.push(i * 5, t);              // still a 5 g/s ramp in real time
        t += clock_ms{10};
    }
    CHECK(s.flow_gps() == doctest::Approx(5.f).epsilon(0.05));
}

TEST_CASE("scale: quiet weight shows ~0 flow") {
    scale::scale s;
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 10'000, 200, t);
    CHECK(std::fabs(s.flow_gps()) < 0.05f);
}

TEST_CASE("scale: flow deadband pins resting creep to 0") {
    scale::scale s{{.lpf = lpf_kind::none}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    // 10 g base + ~0.05 g/s creep (1 count / 200 ms) — under the 0.1 g/s
    // deadband, reads as exactly 0
    for (int i = 0; i < 2000; ++i) {
        s.push(1'000 + i / 20, t);
        t += clock_ms{10};
    }
    CHECK(s.flow_gps() == 0.0f);
}

TEST_CASE("scale: flow above deadband still reports") {
    scale::scale s{{.lpf = lpf_kind::none}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    // ~0.5 g/s trickle (1 count / 20 ms) — above the 0.3 g/s deadband
    for (int i = 0; i < 2000; ++i) {
        s.push(1'000 + i / 2, t);
        t += clock_ms{10};
    }
    CHECK(s.flow_gps() == doctest::Approx(0.5f).epsilon(0.5));
}

TEST_CASE("scale: unprimed pipeline reads 0, not a phantom tare's worth") {
    scale::scale s;
    s.load_calibration({.zero_counts = 80'000.f, .counts_per_gram = 1'600.f});
    // filtered_ starts at 0: without a fed_ gate the readout would be -50 g
    CHECK(s.grams() == 0.0f);
    CHECK(s.flow_gps() == 0.0f);
    clock_ms t{0};
    s.push(80'000, t);
    CHECK(s.grams() == doctest::Approx(0.f).epsilon(0.01));
}

TEST_CASE("scale: step load is rejected by the impact gate") {
    scale::scale s{{.lpf = lpf_kind::none}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 10'000, 100, t);
    // drop a 100 g cup instantly: the Kalman innovation gate rejects it —
    // the readout never reports the step as flow
    float peak = 0.0f;
    bool  gated = false;
    for (int i = 0; i < 80; ++i) {
        s.push(20'000, t);
        t += clock_ms{10};
        peak  = std::max(peak, s.flow_gps());
        gated = gated || s.disturbed();
    }
    CHECK(peak <= 30.0f);
    CHECK(gated);                 // the gate must have caught the step
}

TEST_CASE("scale: zero tracking absorbs drift, not a placed object") {
    scale::scale s{{.lpf = lpf_kind::none,
                    .zero_track = {.enabled = true, .band_g = 0.5f,
                                   .rate = 0.01f, .hold = clock_ms{500},
                                   .max_g = 2.0f}}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 0, 100, t);              // 1 s at 0 -> armed, nothing to eat
    CHECK(s.zero_offset_g() == doctest::Approx(0.f));
    // zero drifts +30 counts = +0.3 g, stays in band -> absorbed
    feed_n(s, 30, 400, t);             // 4 s > hold
    CHECK(s.zero_offset_g() == doctest::Approx(0.3f).epsilon(0.1));
    CHECK(s.grams() == doctest::Approx(0.f).epsilon(0.2));
    // place a 50 g object: tracker freezes, offset stays applied
    feed_n(s, 5'030, 100, t);
    CHECK(s.zero_offset_g() == doctest::Approx(0.3f).epsilon(0.1));
    CHECK(s.grams() == doctest::Approx(50.0f).epsilon(0.02));
}

TEST_CASE("scale: shaking freezes zero tracking") {
    scale::scale s{{.lpf = lpf_kind::none,
                    .zero_track = {.enabled = true, .band_g = 0.5f,
                                   .rate = 0.01f, .hold = clock_ms{500},
                                   .max_g = 2.0f}}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    auto step = [&](std::int32_t counts, float az) {
        s.feed_accel(0.f, 0.f, az);
        s.push(counts, t);
        t += clock_ms{10};
    };
    for (int i = 0; i < 100; ++i) step(0, 1'000.f);   // still, level, 1 s
    CHECK(s.system_stable());
    // Same +0.3 g in-band residual as the tracking test, but the IMU sees
    // vibration (|a| spread 400 mg) -> system_stable false -> frozen.
    for (int i = 0; i < 400; ++i) step(30, i % 2 ? 1'400.f : 1'000.f);
    CHECK(std::fabs(s.zero_offset_g()) < 0.001f);
    // Motion stops -> tracking resumes and absorbs the drift.
    for (int i = 0; i < 500; ++i) step(30, 1'000.f);
    CHECK(s.zero_offset_g() > 0.1f);
}

TEST_CASE("scale: brew mode freezes the zero tracker") {
    scale::app a;
    a.inner().configure({.zero_track = {.enabled = true, .band_g = 0.5f,
                                        .rate = 0.01f, .hold = clock_ms{500},
                                        .max_g = 2.0f}});
    a.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    for (int i = 0; i < 300; ++i) { a.feed(0, t); t += clock_ms{10}; }
    a.next_mode();                                   // -> brew: freeze
    for (int i = 0; i < 500; ++i) { a.feed(30, t); t += clock_ms{10}; }
    CHECK(std::fabs(a.inner().zero_offset_g()) < 0.001f);
    a.next_mode();                                   // -> weigh: resume
    for (int i = 0; i < 600; ++i) { a.feed(30, t); t += clock_ms{10}; }
    CHECK(a.inner().zero_offset_g() > 0.1f);
}

TEST_CASE("scale: thermal drift model compensates the zero") {
    scale::scale s{{.lpf = lpf_kind::none}};
    thermal_model m;
    const thermal_model::point pts[] = {{20.f, 0.f}, {30.f, 100.f}};
    REQUIRE(m.fit(pts, 20.f));         // +10 counts/degC, 0 at 20 C
    s.set_thermal_model(m);
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    s.set_temperature(20.f);
    clock_ms t{0};
    feed_n(s, 1'000, 40, t);           // 10 g mass at 20 C
    CHECK(s.grams() == doctest::Approx(10.f).epsilon(0.01));
    s.set_temperature(30.f);
    feed_n(s, 1'100, 40, t);           // same mass, ADC zero drifted +100
    CHECK(s.grams() == doctest::Approx(10.f).epsilon(0.05));
}

TEST_CASE("scale: stability follows filtered value") {
    scale::scale s{{.lpf = lpf_kind::none, .stability_window = 8,
                    .stability_tol_g = 0.3f}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 10'000, 8, t);
    CHECK(s.stable());
    s.push(10'000 + 500, t);           // single +5 g spike: median kills it
    CHECK(s.stable());
    feed_n(s, 10'100, 3, t);           // +1 g step: median flips -> spread
    CHECK(!s.stable());
    feed_n(s, 10'100, 6, t);           // window refills at the new level
    CHECK(s.stable());
}

TEST_CASE("scale: diag captures every pipeline stage") {
    scale::scale s{{.lpf = lpf_kind::none}};
    s.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    clock_ms t{0};
    feed_n(s, 5'000, 20, t);
    const diag& d = s.last_diag();
    CHECK(d.t == t - clock_ms{10});
    CHECK(d.raw_counts == 5'000);
    CHECK(d.median_counts == 5'000);
    CHECK(d.lpf_counts == doctest::Approx(5'000.f));
    CHECK(d.grams == doctest::Approx(50.f));
    CHECK(d.stable);
    CHECK(d.tilt_quiet);               // no accel fed -> assumed quiet
}

TEST_CASE("telemetry: csv row serialises every column") {
    char buf[256];
    diag d{};
    d.t          = clock_ms{1234};
    d.raw_counts = -5'000;
    d.accel_mg   = {0.f, 0.f, 1'000.f};
    const int n = diag_csv(buf, sizeof(buf), d);
    REQUIRE(n > 0);
    CHECK(buf[0] == '1');
    CHECK(buf[n - 1] == '\n');
    int commas = 0;
    for (const char* p = buf; *p; ++p) commas += (*p == ',');
    CHECK(commas == 19);               // 20 columns
}
