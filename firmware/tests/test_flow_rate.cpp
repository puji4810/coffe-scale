#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include "scale/flow_rate.hpp"

namespace {
void feed(scale::flow_rate& flow, int tick, float grams, std::int64_t epoch = 0) {
    flow.push(scale::clock_ms{epoch + std::llround(tick * 12.5)}, grams);
}
}

TEST_CASE("flow rate: sustained slopes of either sign need no activation") {
    for (float rate : {.35f, .6f, 1.f, 3.f, 6.f, 15.f, 30.f, -.35f, -5.f}) {
        CAPTURE(rate);
        scale::flow_rate flow;
        for (int i = 0; i <= 8*80; ++i) {
            const float t = i / 80.f;
            const float g = 300 + rate * std::clamp(t - 2, 0.f, 4.f);
            feed(flow, i, g);
            if (t >= 3.5f && t < 6.f) {
                CHECK(flow.readout_rate() == doctest::Approx(rate).epsilon(.02));
                CHECK_FALSE(flow.discontinuity());
                CHECK(flow.weight() == doctest::Approx(g).epsilon(.001));
            }
            if (t >= 7.5f) CHECK(std::fabs(flow.readout_rate()) < .10f);
        }
    }
}

TEST_CASE("flow rate: rest and a previous high pour cannot lock a slow restart") {
    for (float slow : {.35f, .6f, 1.f, 3.f}) {
        for (float pause : {0.f, .25f, 2.f, 20.f}) {
            CAPTURE(slow); CAPTURE(pause);
            scale::flow_rate flow;
            float g = 0;
            for (int i = 0; i < (10+pause)*80; ++i) {
                const float t = i / 80.f;
                if (t >= 1 && t < 4) g += 15.f / 80;
                if (t >= 4+pause) g += slow / 80;
                feed(flow, i, g);
                if (t >= 5.5f+pause)
                    CHECK(flow.readout_rate() == doctest::Approx(slow).epsilon(.03));
            }
        }
    }
}

TEST_CASE("flow rate: a gross load step translates weight without resetting the pour") {
    for (float rate : {0.f, .6f, 4.f, 15.f}) {
        CAPTURE(rate);
        scale::flow_rate flow;
        int steps = 0;
        for (int i = 0; i < 10*80; ++i) {
            const float t = i / 80.f;
            const float g = rate*t + (t >= 4 && t < 7 ? 300.f : 0.f);
            feed(flow, i, g);
            steps += flow.discontinuity();
            if (t >= 2) {
                CHECK(flow.readout_rate() == doctest::Approx(rate).epsilon(.015));
                CHECK(std::fabs(flow.weight()-g) < .05f);
            }
        }
        CHECK(steps == 2);
    }
}

TEST_CASE("flow rate: changing measurement ripple cannot latch a low output") {
    scale::flow_rate flow;
    int n = 0, zeros = 0;
    double sum = 0, sum2 = 0;
    for (int i = 0; i < 18*80; ++i) {
        const float t = i / 80.f;
        const float noise = (t >= 5 && t < 12 ? .4f : .1f) *
            std::sin(2 * 3.14159265f * 3 * t);
        feed(flow, i, 6*t + noise);
        if (t >= 6.5f && t < 11.5f) {
            const auto f = flow.readout_rate();
            sum += f; sum2 += f*f; ++n;
            zeros += f <= .3f;
        }
        if (t >= 13.5f) CHECK(std::fabs(flow.readout_rate()-6) < .15f);
    }
    const double mean = sum/n;
    CHECK(zeros == 0);
    CHECK(std::fabs(mean-6) < .1);
    CHECK(std::sqrt(sum2/n-mean*mean) < .2);
}

TEST_CASE("flow rate: real timestamps and long uptime give the same slope") {
    scale::flow_rate a, b;
    std::int64_t ms = 0;
    for (int i = 0; i < 2000; ++i) {
        ms += i % 37 == 0 ? 80 : i % 2 ? 12 : 13;
        const float g = 500 + ms*.005f;
        a.push(scale::clock_ms{ms}, g);
        b.push(scale::clock_ms{ms+864'000'000LL}, g);
        CHECK(a.readout_rate() == b.readout_rate());
        CHECK(a.weight() == b.weight());
        if (i > 150) CHECK(a.readout_rate() == doctest::Approx(5).epsilon(.01));
    }
}

TEST_CASE("flow rate: invalid times do not alter evidence; long outages re-prime") {
    scale::flow_rate flow;
    for (int i = 0; i <= 3*80; ++i) feed(flow, i, i*.05f);
    const float f = flow.readout_rate(), w = flow.weight();
    flow.push(scale::clock_ms{3000}, 3000);
    flow.push(scale::clock_ms{2999}, -3000);
    flow.push(scale::clock_ms{3001}, std::numeric_limits<float>::quiet_NaN());
    CHECK(flow.readout_rate() == f);
    CHECK(flow.weight() == w);
    flow.push(scale::clock_ms{5000}, 300);
    CHECK(flow.readout_rate() == 0);
    CHECK(flow.weight() == 300);
    for (int i = 1; i <= 2*80; ++i) feed(flow, 400+i, 300+i*.05f);
    CHECK(flow.readout_rate() == doctest::Approx(4).epsilon(.01));
    flow.reset();
    CHECK_FALSE(flow.primed());
    CHECK(flow.readout_rate() == 0);
    CHECK(flow.weight() == 0);
}

TEST_CASE("flow rate: only the publication EMA depends on its smoothing setting") {
    scale::flow_rate smoothed, raw{{.smoothing_s=0}};
    for (int i = 0; i < 6*80; ++i) {
        const float t = i/80.f, g = t < 2 ? 0 : 4*(t-2);
        feed(smoothed, i, g); feed(raw, i, g);
        CHECK(smoothed.rate() == raw.rate());
        CHECK(smoothed.weight() == raw.weight());
        CHECK(raw.readout_rate() == raw.rate());
    }
}
