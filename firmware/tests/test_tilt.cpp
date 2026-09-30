#include <doctest/doctest.h>

#include <cmath>

#include "scale/tilt.hpp"

using namespace scale;

namespace {
void feed_n(tilt& tl, float x, float y, float z, int n) {
    for (int i = 0; i < n; ++i) tl.feed(x, y, z);
}
} // namespace

TEST_CASE("tilt: never-fed sensor is quiet and doesn't block") {
    tilt tl;
    CHECK(tl.quiet());                 // absent sensor must not stall gates
    CHECK(!tl.fed());
}

TEST_CASE("tilt: still flat unit reads level") {
    tilt tl;
    feed_n(tl, 0.f, 0.f, 1'000.f, 40);
    CHECK(tl.quiet());
    CHECK(tl.level());
    CHECK(tl.pitch_deg() == doctest::Approx(0.f).epsilon(0.5));
    CHECK(tl.roll_deg() == doctest::Approx(0.f).epsilon(0.5));
}

TEST_CASE("tilt: static tilt gives pitch, stays quiet") {
    tilt tl;
    // 10 deg pitch: a = g * (sin10, 0, cos10) — magnitude still ~1 g.
    feed_n(tl, 173.6f, 0.f, 984.8f, 40);
    CHECK(tl.quiet());
    CHECK(tl.pitch_deg() == doctest::Approx(10.f).epsilon(0.5));
    CHECK(!tl.level());
}

TEST_CASE("tilt: vibration breaks quiet even near 1 g mean") {
    tilt tl;
    feed_n(tl, 0.f, 0.f, 1'000.f, 40);
    REQUIRE(tl.quiet());
    // +-300 mg bobbing around g: mean |a| ~1 g but spread is huge.
    for (int i = 0; i < 20; ++i) tl.feed(0.f, 0.f, i % 2 ? 1'300.f : 700.f);
    CHECK(!tl.quiet());
}

TEST_CASE("tilt: sustained non-g magnitude is not trusted") {
    tilt tl;
    // |a| = 1.28 g steadily (e.g. being carried): norm check rejects it.
    feed_n(tl, 300.f, 0.f, 1'250.f, 40);
    CHECK(!tl.quiet());
}

TEST_CASE("tilt: orientation only updates while quiet") {
    tilt tl;
    feed_n(tl, 0.f, 0.f, 1'000.f, 40);
    const float before = tl.pitch_deg();
    // Violent shake: alternating acceleration, |a| swings 600..1400 mg.
    for (int i = 0; i < 20; ++i) tl.feed(0.f, 0.f, i % 2 ? 1'400.f : 600.f);
    CHECK(!tl.quiet());
    CHECK(tl.pitch_deg() == doctest::Approx(before));
}

TEST_CASE("tilt: a steady non-vertical vector is just a tilt") {
    // Sanity: a constant accel vector with |a| = 1 g is physically
    // indistinguishable from resting at an angle — the module must treat
    // it as quiet and report the angle. 800/600 -> pitch = atan2(800,600).
    tilt tl;
    feed_n(tl, 800.f, 0.f, 600.f, 40);
    CHECK(tl.quiet());
    CHECK(tl.pitch_deg() == doctest::Approx(53.13f).epsilon(0.02));
}
