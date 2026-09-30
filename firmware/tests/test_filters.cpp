#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "scale/filters.hpp"
#include "scale/stability.hpp"

using namespace scale;

TEST_CASE("median_filter kills single-sample spikes") {
    median_filter<5> f;
    for (int i = 0; i < 4; ++i) CHECK(f.push(100) == 100);
    CHECK(!f.primed());                 // only 4 of 5 slots filled
    CHECK(f.push(100) == 100);
    CHECK(f.primed());
    // one huge spike inside the window: median stays 100
    CHECK(f.push(50'000) == 100);
    CHECK(f.value() == 100);
}

TEST_CASE("median_filter tracks a real step") {
    median_filter<5> f;
    for (int i = 0; i < 5; ++i) (void)f.push(100);
    for (int i = 0; i < 5; ++i) (void)f.push(200);
    CHECK(f.value() == 200);
}

TEST_CASE("ema converges and primes on first sample") {
    ema e{0.5f};
    CHECK(e.push(10.0f) == doctest::Approx(10.0f));   // primes to first sample
    CHECK(e.push(20.0f) == doctest::Approx(15.0f));
    CHECK(e.push(20.0f) == doctest::Approx(17.5f));
}

TEST_CASE("window ordering and last-k stats") {
    window<float, 8> w;
    for (float v : {1.f, 5.f, 2.f, 8.f}) w.push(v);
    CHECK(w.at(0) == 1.f);
    CHECK(w.at(3) == 8.f);
    CHECK(w.latest() == 8.f);
    CHECK(w.spread_last(4) == doctest::Approx(7.f));
    CHECK(w.spread_last(2) == doctest::Approx(6.f));
    CHECK(w.mean_last(4) == doctest::Approx(4.f));
}

TEST_CASE("window wraps") {
    window<int, 3> w;
    w.push(1); w.push(2); w.push(3); w.push(4);
    CHECK(w.full());
    CHECK(w.at(0) == 2);
    CHECK(w.at(2) == 4);
}

TEST_CASE("stability_detector: stable after a quiet window, unstable on jump") {
    stability_detector<8> d{4, 0.5f};
    CHECK(!d.stable());
    for (int i = 0; i < 3; ++i) CHECK(!d.push(100.f));
    CHECK(d.push(100.f));                 // window full & quiet -> stable
    CHECK(d.stable());
    CHECK(!d.push(103.f));                // jump breaks stability
    CHECK(!d.push(103.f));                // window still mixed
    CHECK(!d.push(103.f));
    CHECK(d.push(103.f));                 // window refilled at new level -> stable
}

TEST_CASE("adaptive_ema picks the alpha band by error magnitude") {
    adaptive_ema e{{{5.f, 0.6f}, {0.5f, 0.25f}, {0.f, 0.08f}}};
    e.set_counts_per_gram(100.f);
    CHECK(e.push(0.f) == doctest::Approx(0.f));          // primes
    // quiet input: err 0.1 g < 0.5 -> resting alpha 0.08
    CHECK(e.push(10.f) == doctest::Approx(0.8f));
    // big step: err ~20 g >= 5 -> fast band 0.6
    const float y = e.push(2010.f);
    CHECK(y == doctest::Approx(0.8f + 0.6f * (2010.f - 0.8f)));
}

TEST_CASE("adaptive_ema converges fast on a real step, tight when still") {
    adaptive_ema e{{{5.f, 0.6f}, {0.5f, 0.25f}, {0.f, 0.08f}}};
    e.set_counts_per_gram(1600.f);
    for (int i = 0; i < 40; ++i) (void)e.push(0.f);
    for (int i = 0; i < 40; ++i) (void)e.push(160'000);  // +100 g step
    CHECK(e.value() == doctest::Approx(160'000.f).epsilon(0.001));
}

TEST_CASE("bessel2_lpf: unity DC gain, settles without overshoot") {
    bessel2_lpf f{2.0f, 80.0f};
    for (int i = 0; i < 60; ++i) (void)f.push(0.0f);
    float peak = 0.0f;
    for (int i = 0; i < 200; ++i) {
        peak = std::max(peak, f.push(100.0f));
    }
    CHECK(f.value() == doctest::Approx(100.0f).epsilon(0.001));
    CHECK(peak <= 100.5f);      // Bessel step response: ~no overshoot
}

TEST_CASE("bessel2_lpf attenuates fast noise more than slow signal") {
    bessel2_lpf f{2.0f, 80.0f};
    // 20 Hz sine (well above the 2 Hz cutoff) should be crushed
    float amp = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float v = 50.0f * std::sin(2.0f * 3.14159265f * 20.0f * i / 80.0f);
        const float y = f.push(v);
        if (i > 200) amp = std::max(amp, std::fabs(y));
    }
    CHECK(amp < 2.0f);          // >25 dB down at 20 Hz
}

TEST_CASE("savitzky_golay: 5-pt quadratic centre coefficients") {
    savitzky_golay<5, 2> sg{0.0f};    // evaluate at window centre
    const float inv35 = 1.0f / 35.0f; // canonical set {-3,12,17,12,-3}/35
    CHECK(sg.coef(0) == doctest::Approx(-3.f * inv35));
    CHECK(sg.coef(1) == doctest::Approx(12.f * inv35));
    CHECK(sg.coef(2) == doctest::Approx(17.f * inv35));
    CHECK(sg.coef(3) == doctest::Approx(12.f * inv35));
    CHECK(sg.coef(4) == doctest::Approx(-3.f * inv35));
}

TEST_CASE("savitzky_golay: reproduces polynomials exactly at eval point") {
    savitzky_golay<5, 2> sg;          // default eval = newest sample
    for (float u : {-2.f, -1.f, 0.f, 1.f, 2.f}) (void)sg.push(u * u);
    CHECK(sg.primed());
    CHECK(sg.value() == doctest::Approx(4.f).epsilon(0.001));  // u^2 @ u=2
    savitzky_golay<5, 2> ramp;
    for (float u : {-2.f, -1.f, 0.f, 1.f, 2.f}) (void)ramp.push(u);
    CHECK(ramp.value() == doctest::Approx(2.f).epsilon(0.001));
}

TEST_CASE("savitzky_golay passes through until the window is full") {
    savitzky_golay<7, 2> sg;
    for (int i = 0; i < 6; ++i) {
        CHECK(sg.push(10.f * i) == doctest::Approx(10.f * i));
    }
    CHECK(!sg.primed());
}
