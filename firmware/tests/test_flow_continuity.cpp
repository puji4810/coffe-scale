#include <doctest/doctest.h>

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
        if (t >= 10.5) CHECK(r == 0.0f);
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
