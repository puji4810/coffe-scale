#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include "scale/app.hpp"

namespace {
constexpr float pi = 3.14159265f;
struct bench {
    scale::app app;
    explicit bench(bool brew = false) {
        app.load_calibration({.zero_counts = 0.f, .counts_per_gram = 10'000.f});
        if (brew) app.next_mode();
    }
    float feed(int i, float grams) {
        app.feed(static_cast<int>(std::lround(grams * 10'000)),
                 scale::clock_ms{static_cast<long long>(std::llround(i * 12.5))});
        return app.display_value();
    }
};
}

TEST_CASE("weight display: a stationary noisy platform keeps its last digit") {
    for (const float hz : {.5f, .8f, 1.2f}) {
        for (const float phase : {0.f, pi*.5f, pi}) {
            CAPTURE(hz); CAPTURE(phase);
            bench b;
            int changes = 0;
            float prev = 0;
            for (int i = 0; i < 15 * 80; ++i) {
                const float t = i / 80.0f;
                const float g = t < 2 ? 0 : 100.05f + .09f * std::sin(2*pi*hz*(t-2)+phase);
                const float shown = b.feed(i, g);
                if (t >= 5) {
                    CHECK(std::fabs(shown - 100.05f) <= .101f);
                    if (t > 5 && shown != prev) ++changes;
                }
                prev = shown;
            }
            CAPTURE(changes);
            CHECK(changes <= 1);
        }
    }
}

TEST_CASE("weight display: placement lands after ringing and releases for a new load") {
    bench b;
    float prev = 0;
    int changes = 0;
    for (int i = 0; i < 7 * 80; ++i) {
        const float t = i / 80.0f, u = t - 2;
        float g = u < 0 ? 0 : 100 + .10f*std::sin(2*pi*7*u)*std::exp(-8*u);
        if (t >= 4) g += .1f;
        if (t >= 5) g += 10;
        const float shown = b.feed(i, g);
        if (t >= 2.5f && t < 4) CHECK(shown == doctest::Approx(100.f));
        if (t >= 4.3f && t < 5) CHECK(shown == doctest::Approx(100.1f));
        if (t >= 5.25f) CHECK(shown == doctest::Approx(110.1f));
        if (t >= 3 && t < 4 && shown != prev) ++changes;
        prev = shown;
    }
    CHECK(changes == 0);
}

TEST_CASE("weight display: real small objects survive auto-zero in both modes") {
    for (const bool brew : {false, true}) {
        for (const float mass : {.1f, .2f, .3f}) {
            CAPTURE(brew); CAPTURE(mass);
            bench b{brew};
            for (int i = 0; i < 17 * 80; ++i) {
                const float t = i / 80.0f;
                const float shown = b.feed(i, t < 2 ? 0 : mass);
                if (t >= 2.5f) CHECK(shown == doctest::Approx(mass));
            }
            CHECK(std::fabs(b.app.inner().zero_offset_g()) < .01f);
        }
    }
}

TEST_CASE("weight display: unload returns to held zero in both modes") {
    for (const bool brew : {false, true}) {
      for (const float hz : {.8f, 2.f}) {
        CAPTURE(brew);
        CAPTURE(hz);
        bench b{brew};
        float since = -1, first = -1;
        for (int i = 0; i < 14 * 80; ++i) {
            const float t = i / 80.0f;
            const float g = t < 2 ? 0 : t < 4 ? 100
                : .2f + .04f*std::sin(2*pi*hz*(t-4));
            const float shown = b.feed(i, g);
            if (t >= 4) {
                if (shown == 0) {
                    if (since < 0) since = t;
                    if (first < 0 && t - since >= .15f) first = since - 4;
                } else since = -1;
            }
            if (t >= 4.8f) CHECK(shown == 0.0f);
            if (t >= 5) CHECK(std::fabs(b.app.state().flow_gps) < .3f);
        }
        CAPTURE(first);
        CHECK(first >= 0);
        CHECK(first <= .5f);
      }
    }
}

TEST_CASE("weight display: tare on a container retains its negative removal weight") {
    bench b;
    for (int i = 0; i < 8 * 80; ++i) {
        const float t = i / 80.0f;
        b.feed(i, t < 2 ? 0 : t < 5 ? 100
               : .2f + .04f*std::sin(2*pi*2*(t-5)));
        if (i == 3 * 80) {
            b.app.tare();
            CHECK(b.app.display_value() == 0.0f);
        }
        if (t >= 3.5f && t < 5) CHECK(b.app.display_value() == 0.0f);
        if (t >= 5.6f) CHECK(b.app.display_value() == doctest::Approx(-100.f));
    }
}

TEST_CASE("weight display: a new small load after unload survives mode changes") {
    bench b{true};
    for (int i = 0; i < 12 * 80; ++i) {
        const float t = i / 80.0f;
        b.feed(i, t < 2 ? 0 : t < 4 ? 100 : t < 6 ? .2f : .3f);
        if (i == 8 * 80) b.app.next_mode();
        if (t >= 4.6f && t < 6) CHECK(b.app.display_value() == 0.f);
        if (t >= 6.5f) CHECK(b.app.display_value() == doctest::Approx(.1f));
    }
}

TEST_CASE("weight display: a residual outside the unload band remains a load") {
    bench b;
    for (int i = 0; i < 10 * 80; ++i) {
        const float t = i / 80.0f;
        b.feed(i, t < 2 ? 0 : t < 4 ? 100 : .3f);
        if (t >= 4.5f) CHECK(b.app.display_value() == doctest::Approx(.3f));
    }
    CHECK(std::fabs(b.app.inner().zero_offset_g()) < .01f);
}

TEST_CASE("weight display: real slow changes release the platform hold") {
    for (const float rate : {.1f, .3f, 1.f}) {
        CAPTURE(rate);
        bench b{true};
        for (int i = 0; i < 14 * 80; ++i) {
            const float t = i / 80.0f;
            const float g = t < 2 ? 0 : t < 4 ? 100 : 100 + rate*(t-4);
            const float shown = b.feed(i, g);
            if (t >= 5) CHECK(std::fabs(shown - g) <= .151f);
        }
    }
}

TEST_CASE("weight display: tare preserves an existing physical zero correction") {
    bench b;
    for (int i = 0; i < 12 * 80; ++i) {
        const float t = i / 80.0f;
        // After a large unload the physical empty reference is +0.2 g.
        // Tare a real +0.3 g object on that reference, then remove it.
        b.feed(i, t < 2 ? 0 : t < 4 ? 100 : t < 6 ? .2f
               : t < 9 ? .5f : .2f);
        if (i == 8 * 80) {
            b.app.tare();
            CHECK(b.app.display_value() == 0.f);
        }
        if (t >= 8.5f && t < 9) CHECK(b.app.display_value() == 0.f);
        if (t >= 9.6f) CHECK(b.app.display_value() == doctest::Approx(-.3f));
    }
    CHECK(b.app.inner().zero_offset_g() == doctest::Approx(.2f).epsilon(.001));
}

TEST_CASE("weight display: a sampling gap cannot certify an unseen unload") {
    bench b;
    for (int i = 0; i < 4 * 80; ++i) b.feed(i, i < 160 ? 0.f : 100.f);
    // Resume after missing the entire removal and placement. The 0.2 g
    // reading must be treated as a possible real load, not unload residue.
    for (int i = 8 * 80; i < 20 * 80; ++i) {
        b.feed(i, .2f);
        if (i >= 9 * 80) CHECK(b.app.display_value() == doctest::Approx(.2f));
    }
    CHECK(std::fabs(b.app.inner().zero_offset_g()) < .01f);
}
