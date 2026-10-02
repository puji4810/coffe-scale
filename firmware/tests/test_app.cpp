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

TEST_CASE("app: sleeping idle brew rejects late starts and wakes at zero") {
    app a;
    a.next_mode();
    a.feed(0, clock_ms{1000});
    a.prepare_sleep(clock_ms{2000});
    CHECK(a.sleeping());
    // A queued web auto-timer or button event arrives during teardown.
    a.tare_long();
    a.tare_long(clock_ms{2100});
    a.feed(0, clock_ms{1'802'000});
    CHECK(a.state().timer_state == brew_timer::state::idle);
    CHECK(a.state().timer_elapsed == clock_ms{0});
    a.wake(clock_ms{1'802'000});
    CHECK(!a.sleeping());
    CHECK(a.state().m == app::mode::brew);
    CHECK(a.state().timer_state == brew_timer::state::idle);
    CHECK(a.state().timer_elapsed == clock_ms{0});
    // A genuine post-wake command, before the first new ADC sample,
    // starts at wake time instead of the last pre-sleep sample's time.
    a.tare_long();
    a.feed(0, clock_ms{1'803'000});
    CHECK(a.state().timer_elapsed == clock_ms{1000});
}

TEST_CASE("app: sleep freezes a running brew and requires explicit resume") {
    app a;
    a.next_mode();
    a.feed(0, clock_ms{1000});
    a.tare_long();
    a.feed(0, clock_ms{4000});
    a.prepare_sleep(clock_ms{5000});
    CHECK(a.state().timer_state == brew_timer::state::paused);
    CHECK(a.state().timer_elapsed == clock_ms{4000});
    a.prepare_sleep(clock_ms{5100});  // actual entry after the request
    a.tare_long(clock_ms{5200});       // cannot resume during teardown
    a.wake(clock_ms{605000});
    a.feed(0, clock_ms{605500});
    CHECK(a.state().timer_state == brew_timer::state::paused);
    CHECK(a.state().timer_elapsed == clock_ms{4000});
    a.tare_long(clock_ms{606000});
    a.feed(0, clock_ms{608000});
    CHECK(a.state().timer_elapsed == clock_ms{6000});
    a.prepare_sleep(clock_ms{608000});
    a.wake(clock_ms{1'208'000});
    CHECK(a.state().timer_state == brew_timer::state::paused);
    CHECK(a.state().timer_elapsed == clock_ms{6000});
}

TEST_CASE("app: already paused and weigh timers survive sleep unchanged") {
    app a;
    a.next_mode();
    a.feed(0, clock_ms{1000});
    a.tare_long();
    a.feed(0, clock_ms{2000});
    a.tare_long();
    a.prepare_sleep(clock_ms{3000});
    a.wake(clock_ms{903000});
    CHECK(a.state().timer_state == brew_timer::state::paused);
    CHECK(a.state().timer_elapsed == clock_ms{1000});
    a.next_mode();
    a.prepare_sleep(clock_ms{904000});
    a.wake(clock_ms{1'804'000});
    CHECK(a.state().m == app::mode::weigh);
    CHECK(a.state().timer_state == brew_timer::state::idle);
    CHECK(a.state().timer_elapsed == clock_ms{0});
}

TEST_CASE("app: timer commands use arrival time independently of ADC time") {
    app a;
    a.next_mode();
    a.feed(0, clock_ms{1000});
    a.tare_long(clock_ms{1'801'000});
    CHECK(a.state().timer_elapsed == clock_ms{0});
    a.feed(0, clock_ms{1'801'500});
    CHECK(a.state().timer_elapsed == clock_ms{500});
    a.tare_long(clock_ms{1'802'000});
    CHECK(a.state().timer_state == brew_timer::state::paused);
    CHECK(a.state().timer_elapsed == clock_ms{1000});
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
