#include <doctest/doctest.h>

#include "scale/app.hpp"
#include "scale/button_press.hpp"

using namespace scale;

TEST_CASE("button: a release without a press cannot start a brew during sleep") {
    app a;
    a.next_mode();
    a.feed(0, clock_ms{1000});
    button_press tare;
    // A release edge at the sleep boundary, with no preceding press.
    const auto action = tare.push(false, clock_ms{1000});
    if (action == button_press::action::long_press) a.tare_long();
    // The app receives no ADC samples for half an hour.
    a.feed(0, clock_ms{1'801'000});
    CHECK(action == button_press::action::none);
    CHECK(a.state().timer_state == brew_timer::state::idle);
    CHECK(a.state().timer_elapsed == clock_ms{0});
}

TEST_CASE("button: completed presses cannot be replayed by duplicate releases") {
    button_press key;
    CHECK(key.push(true, clock_ms{1000}) == button_press::action::none);
    CHECK(key.push(false, clock_ms{1200}) == button_press::action::short_press);
    CHECK(key.push(false, clock_ms{1300}) == button_press::action::none);
    CHECK(key.push(true, clock_ms{2000}) == button_press::action::none);
    CHECK(key.push(false, clock_ms{2800}) == button_press::action::long_press);
    CHECK(key.push(false, clock_ms{2900}) == button_press::action::none);
}

TEST_CASE("button: bounce and duplicate downs preserve the original press") {
    button_press key;
    CHECK(key.push(true, clock_ms{0}) == button_press::action::none);
    CHECK(key.push(false, clock_ms{10}) == button_press::action::none);
    CHECK(key.push(true, clock_ms{200}) == button_press::action::none);
    CHECK(key.push(false, clock_ms{800}) == button_press::action::long_press);
}

TEST_CASE("button: sleep discards old presses and wake consumes held keys") {
    button_press key;
    key.push(true, clock_ms{1000});
    key.reset();
    CHECK(key.push(false, clock_ms{1'801'000}) == button_press::action::none);
    key.reset(true);                 // the MODE wake button is still down
    CHECK(key.push(true, clock_ms{1'802'000}) == button_press::action::none);
    CHECK(key.push(false, clock_ms{1'803'000}) == button_press::action::none);
    CHECK(key.push(true, clock_ms{1'804'000}) == button_press::action::none);
    CHECK(key.push(false, clock_ms{1'804'100}) == button_press::action::short_press);
}
