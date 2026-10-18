#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "scale/app.hpp"

namespace {
scale::app brew_app() {
    scale::app app;
    scale::calibration c;
    c.counts_per_gram = 10000.0f;
    app.load_calibration(c);
    app.next_mode();
    return app;
}

void feed(scale::app& app, double t, double grams) {
    app.feed(static_cast<std::int32_t>(std::lround(grams * 10000.0)),
             scale::clock_ms{static_cast<std::int64_t>(std::llround(t * 1000.0))});
}
} // namespace

TEST_CASE("flow: a continuous pour with weight ripple must not snap to zero") {
    // Physical mass grows at 3 g/s throughout the pour. A bounded 0.2 g
    // measurement ripple creates brief flat short fits, not real stops.
    auto app = brew_app();
    int zeros = 0, n = 0;
    double sum = 0.0, sum2 = 0.0;
    for (int i = 0; i <= 12 * 80; ++i) {
        const double t = i / 80.0;
        const double grams = t < 2.0 ? 0.0 : t < 10.0
            ? 3.0 * (t - 2.0) +
              0.2 * std::sin(2.0 * 3.141592653589793 * 3.0 * (t - 2.0))
            : 24.0;
        feed(app, t, grams);
        const float r = app.state().flow_gps;
        if (t >= 3.0 && t < 9.5) {
            zeros += r == 0.0f;
            sum += r;
            sum2 += r * r;
            ++n;
        }
        if (t >= 11.3) CHECK(r == 0.0f);
    }
    const double mean = sum / n;
    const double sd = std::sqrt(sum2 / n - mean * mean);
    MESSAGE("continuous pour: zeros=", zeros, " mean=", mean, " sd=", sd);
    CHECK(zeros == 0);
    CHECK(std::fabs(mean - 3.0) < 0.3);
    CHECK(sd < 0.6);
}

TEST_CASE("flow: recorded continuous brew growth cannot be snapped to zero") {
    // This fixture is a 10 Hz exported weight curve, not raw ADC data.
    // Linear interpolation at 80 Hz is an explicit diagnostic proxy;
    // it exercises the production app but cannot recreate unseen noise.
    std::ifstream in(std::string(SCALE_TEST_FIXTURE_DIR) +
                     "/brew-continuous-weight.csv");
    REQUIRE(in.is_open());
    struct point { double t, w; };
    std::vector<point> points;
    std::string line;
    while (std::getline(in, line)) {
        point p;
        std::istringstream fields(line);
        char comma;
        if (fields >> p.t >> comma >> p.w && comma == ',')
            points.push_back(p);
    }
    REQUIRE(points.size() > 900);
    REQUIRE(points.front().t == 0.0);
    REQUIRE(points.back().t == 103.6);
    auto app = brew_app();
    std::size_t j = 1;
    int zeros = 0, n = 0;
    double sum = 0.0;
    for (int i = static_cast<int>(std::ceil(points.front().t * 80));
         i <= static_cast<int>(points.back().t * 80); ++i) {
        const double t = i / 80.0;
        while (j + 1 < points.size() && points[j].t < t) ++j;
        const auto a = points[j - 1], b = points[j];
        REQUIRE(b.t > a.t);
        const double w = a.w + (b.w - a.w) * (t - a.t) / (b.t - a.t);
        feed(app, t, w);
        if (t >= 56.0 && t <= 75.0) {
            const float r = app.state().flow_gps;
            zeros += r == 0.0f;
            sum += r;
            ++n;
            CHECK(!app.inner().last_diag().disturbed);
        }
    }
    REQUIRE(n == 1521);
    MESSAGE("recorded growth proxy: zeros=", zeros, " mean=", sum / n);
    CHECK(zeros == 0);
    CHECK(sum / n > 2.0);
    CHECK(sum / n < 7.0);
}

TEST_CASE("flow: steady 6 g/s pouring stays smooth through measurement ripple") {
    auto app = brew_app();
    int zeros = 0, n = 0;
    double sum = 0.0, sum2 = 0.0, peak = 0.0;
    for (int i = 0; i <= 12 * 80; ++i) {
        const double t = i / 80.0;
        const double grams = t < 2.0 ? 0.0 : t < 10.0
            ? 6.0 * (t - 2.0) +
              0.4 * std::sin(2.0 * 3.141592653589793 * 3.0 * (t - 2.0))
            : 48.0;
        feed(app, t, grams);
        const float r = app.state().flow_gps;
        if (t >= 3.0 && t < 9.5) {
            zeros += r == 0.0f;
            sum += r;
            sum2 += r * r;
            peak = std::max(peak, static_cast<double>(r));
            ++n;
        }
        if (t >= 11.3) CHECK(r == 0.0f);
    }
    const double mean = sum / n;
    const double sd = std::sqrt(sum2 / n - mean * mean);
    MESSAGE("6 g/s pour: zeros=", zeros, " mean=", mean,
            " sd=", sd, " peak=", peak);
    CHECK(zeros == 0);
    CHECK(std::fabs(mean - 6.0) < 0.4);
    CHECK(sd < 0.2);
    CHECK(peak < 6.5);
}

TEST_CASE("flow: October 3 brew proxy preserves continuous pouring without spikes") {
    // Exported filtered weight at ~10 Hz is a proxy, not raw ADC/IMU.
    // Repeated rounded timer stamps retain their last weight. Exclude
    // placement/removal, the actual pause, and onset/stop transients.
    std::ifstream in(std::string(SCALE_TEST_FIXTURE_DIR) +
                     "/brew-20261003-weight.csv");
    REQUIRE(in.is_open());
    struct point { double t, w; };
    std::vector<point> points;
    std::string line;
    while (std::getline(in, line)) {
        point p;
        std::istringstream fields(line);
        char comma;
        if (fields >> p.t >> comma >> p.w && comma == ',')
            points.push_back(p);
    }
    REQUIRE(points.size() == 995);
    REQUIRE(points.front().t == 0.0);
    REQUIRE(points.back().t == 107.9);
    auto app = brew_app();
    std::size_t j = 1;
    int zeros = 0, n = 0;
    double sum = 0.0, sum2 = 0.0, peak = 0.0;
    for (int i = 0; i <= static_cast<int>(points.back().t * 80); ++i) {
        const double t = i / 80.0;
        while (j + 1 < points.size() && points[j].t < t) ++j;
        const auto a = points[j - 1], b = points[j];
        REQUIRE(b.t > a.t);
        feed(app, t, a.w + (b.w - a.w) * (t - a.t) / (b.t - a.t));
        if ((t >= 32.0 && t <= 49.4) || (t >= 60.0 && t <= 86.0)) {
            const float r = app.state().flow_gps;
            zeros += r == 0.0f;
            sum += r;
            sum2 += r * r;
            peak = std::max(peak, static_cast<double>(r));
            ++n;
        }
    }
    REQUIRE(n > 3300);
    const double mean = sum / n;
    const double sd = std::sqrt(sum2 / n - mean * mean);
    MESSAGE("October 3 proxy: zeros=", zeros, " mean=", mean,
            " sd=", sd, " peak=", peak);
    CHECK(zeros == 0);
    CHECK(mean > 3.0);
    CHECK(mean < 5.5);
    CHECK(sd < 1.2);
    CHECK(peak < 8.0);
}

TEST_CASE("flow: published flow responds to pour changes, stops and restarts") {
    auto app = brew_app();
    std::vector<float> values;
    for (int i = 0; i <= 17 * 80; ++i) {
        const double t = i / 80.0;
        const double w = t < 2.0 ? 0.0 : t < 6.0 ? 6.0 * (t - 2.0)
            : t < 9.0 ? 24.0 + 8.0 * (t - 6.0)
            : t < 12.0 ? 48.0 + 5.0 * (t - 9.0)
            : t < 12.5 ? 63.0 : t < 15.0 ? 63.0 + 6.0 * (t - 12.5)
            : 78.0;
        feed(app, t, w);
        values.push_back(app.state().flow_gps);
        // A half-second pause is shorter than the fixed fit window; it must
        // not latch zero and suppress the following restart.
        if (t >= 16.3) CHECK(values.back() == 0.0f);
    }
    const auto held_crossing = [&](double start, float threshold, bool rising) {
        for (int i = static_cast<int>(start * 80); i < (start + 1.3) * 80; ++i) {
            bool held = true;
            for (int j = i; j <= i + 12; ++j) {
                if (rising ? values[j] < threshold : values[j] > threshold)
                    held = false;
            }
            if (held) return i / 80.0 - start;
        }
        return -1.0;
    };
    const double onset = held_crossing(2.0, 4.2f, true);
    const double faster = held_crossing(6.0, 7.4f, true);
    const double slower = held_crossing(9.0, 5.9f, false);
    const double restart = held_crossing(12.5, 4.2f, true);
    MESSAGE("published response: onset=", onset, " faster=", faster,
            " slower=", slower, " restart=", restart);
    for (double delay : {onset, faster, slower, restart}) {
        CHECK(delay >= 0.0);
        CHECK(delay <= 1.2 + 1e-9);
    }
}

TEST_CASE("flow: smoothing changes published flow without changing weight") {
    scale::scale_config raw_config;
    raw_config.flow.smoothing_s = 0.0f;
    scale::scale smooth, raw{raw_config};
    scale::calibration c;
    c.counts_per_gram = 10000.0f;
    smooth.load_calibration(c);
    raw.load_calibration(c);
    int different = 0;
    for (int i = 0; i <= 12 * 80; ++i) {
        const double t = i / 80.0;
        const double w = t < 2.0 ? 0.0 : t < 10.0
            ? 6.0 * (t - 2.0) + 0.4 * std::sin(2.0 * 3.141592653589793 * 3.0 * t)
            : 48.0;
        const auto counts = static_cast<std::int32_t>(std::lround(w * 10000.0));
        const auto now = scale::clock_ms{static_cast<std::int64_t>(std::llround(t * 1000.0))};
        smooth.push(counts, now);
        raw.push(counts, now);
        CHECK(smooth.grams() == raw.grams());
        CHECK(smooth.display_grams() == raw.display_grams());
        CHECK(smooth.flow_state_gps() == raw.flow_state_gps());
        different += std::fabs(smooth.flow_gps() - raw.flow_gps()) > 0.1f;
    }
    CHECK(different > 100);
}

TEST_CASE("flow: stateless display deadband preserves real slow pours") {
    for (double rate : {0.35, 0.6, 1.0}) {
        CAPTURE(rate);
        auto app = brew_app();
        double sum = 0.0;
        int n = 0, zeros = 0;
        for (int i = 0; i <= 10 * 80; ++i) {
            const double t = i / 80.0;
            const double w = t < 1.0 ? 0.0 : rate * std::min(t - 1.0, 7.0);
            feed(app, t, w);
            if (t >= 6.0 && t < 7.5) {
                const float f = app.state().flow_gps;
                sum += f;
                zeros += f == 0.0f;
                ++n;
            }
            if (t >= 9.3) CHECK(app.state().flow_gps == 0.0f);
        }
        CHECK(zeros == 0);
        CHECK(std::fabs(sum / n - rate) < 0.05);
    }
}

TEST_CASE("flow: October 15 and 17 exported weight proxies never latch low during growth") {
    for (int day : {15, 17}) {
        CAPTURE(day);
        std::ifstream in(std::string(SCALE_TEST_FIXTURE_DIR) +
            "/brew-202610" + std::to_string(day) + "-weight.csv");
        REQUIRE(in.is_open());
        struct point { double t, w; };
        std::vector<point> points;
        std::string line;
        while (std::getline(in,line)) {
            point p; char comma;
            std::istringstream fields(line);
            if (fields >> p.t >> comma >> p.w && comma == ',') points.push_back(p);
        }
        REQUIRE(points.size() > 1100);
        const double start = day == 15 ? 85 : 45, end = day == 15 ? 115 : 65;
        auto app = brew_app();
        std::size_t j = 1;
        int zeros = 0, n = 0;
        float minimum = 30;
        double sum = 0;
        for (int tick = 0; tick <= points.back().t*80; ++tick) {
            const double t = tick/80.;
            while (j+1 < points.size() && points[j].t < t) ++j;
            const auto a = points[j-1], b = points[j];
            REQUIRE(b.t > a.t);
            feed(app,t,a.w+(b.w-a.w)*(t-a.t)/(b.t-a.t));
            if (t >= start && t <= end) {
                const float f = app.state().flow_gps;
                zeros += f == 0;
                minimum = std::min(minimum,f); sum += f; ++n;
            }
        }
        REQUIRE(n > 1500);
        MESSAGE("October ",day,": zeros=",zeros," min=",minimum," mean=",sum/n);
        CHECK(zeros == 0);
        // Physical measurement ripple is retained, rather than clamped to an
        // invented pour floor. Reject a persistent/near-zero suppression.
        CHECK(minimum >= 1.5f);
        const auto first = std::ranges::lower_bound(points,start,{},&point::t);
        const auto last = std::ranges::lower_bound(points,end,{},&point::t);
        REQUIRE(last != points.end());
        const double weight_rate = (last->w-first->w)/(last->t-first->t);
        CHECK(std::fabs(sum/n-weight_rate) < .2);
    }
}
