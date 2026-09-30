#include <doctest/doctest.h>

#include "scale/app.hpp"
#include "scale/units.hpp"

using namespace scale;

namespace {
void feed(app& a, int32_t counts, clock_ms t, int n = 1) {
    for (int i = 0; i < n; ++i) a.feed(counts, t);
}
}

TEST_CASE("app: snapshot reflects feed + tare") {
    app a;
    a.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    feed(a, 5'000, clock_ms{0}, 20);
    auto s = a.state();
    CHECK(s.grams == doctest::Approx(50.f));
    CHECK(!s.tared);
    a.tare();
    CHECK(a.state().tared);
    CHECK(a.state().grams == doctest::Approx(0.f).epsilon(0.001));
}

TEST_CASE("app: mode switching resets timer") {
    app a;
    CHECK(a.state().m == app::mode::weigh);
    a.next_mode();
    CHECK(a.state().m == app::mode::brew);
    a.tare_long();                       // brew mode -> starts timer
    CHECK(a.state().timer_state == brew_timer::state::running);
    a.next_mode();                       // back to weigh -> timer reset
    CHECK(a.state().m == app::mode::weigh);
    CHECK(a.state().timer_state == brew_timer::state::idle);
}

TEST_CASE("app: tare_long only acts in brew mode") {
    app a;   // weigh mode
    a.tare_long();
    CHECK(a.state().timer_state == brew_timer::state::idle);
}

TEST_CASE("brew_timer: start/pause/resume/elapsed") {
    brew_timer t;
    t.toggle(clock_ms{1000});            // idle -> start
    CHECK(t.elapsed(clock_ms{2500}) == clock_ms{1500});
    t.toggle(clock_ms{2500});            // running -> pause
    CHECK(t.elapsed(clock_ms{9000}) == clock_ms{1500});   // frozen
    t.toggle(clock_ms{9000});            // paused -> resume
    CHECK(t.elapsed(clock_ms{9500}) == clock_ms{2000});
    t.reset();
    CHECK(t.current() == brew_timer::state::idle);
    CHECK(t.elapsed(clock_ms{99999}) == clock_ms{0});
}

TEST_CASE("units: ounce conversion") {
    CHECK(convert(28.349523125f, unit::ounce) == doctest::Approx(1.0f));
    CHECK(label(unit::gram) == doctest::Contains("g"));
    app a;
    a.load_calibration({.zero_counts = 0.f, .counts_per_gram = 100.f});
    feed(a, 10'000, clock_ms{0}, 20);    // 100 g
    a.set_unit(unit::ounce);
    CHECK(a.display_value() == doctest::Approx(3.527f).epsilon(0.01));
}
