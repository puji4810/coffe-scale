// Replay driver: a synthetic capture string through the shared parser +
/// scale::app feed path — grams must converge on the ramp target and the
/// derived flow must sit at the ramp slope. The fixture tests replay
/// real captures from tests/fixtures (SCALE_TEST_FIXTURE_DIR) — a
/// missing fixture is a failure, never a skip.

#include <doctest/doctest.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../tools/replay_core.hpp"

namespace {

struct replay_row { double t, g, f; };

// '# coffee-scale raw v1 cpg=.. zero=.. tare=.. fw=..' header tokens —
// mirrors tools/replay.cpp's parse (kept local so the tool's production
// semantics are untouched).
struct cap_header { double cpg = 0, zero = 0, tare = 0; };

cap_header parse_header(std::string_view line) {
    cap_header h;
    for (std::string_view rest = line;;) {
        const auto sp  = rest.find(' ');
        const auto tok = rest.substr(0, sp);
        rest = sp == std::string_view::npos ? "" : rest.substr(sp + 1);
        if (const auto eq = tok.find('='); eq != std::string_view::npos) {
            double v = 0;
            const auto vs = tok.substr(eq + 1);
            std::from_chars(vs.data(), vs.data() + vs.size(), v);
            if (tok.substr(0, eq) == "cpg") h.cpg = v;
            else if (tok.substr(0, eq) == "zero") h.zero = v;
            else if (tok.substr(0, eq) == "tare") h.tare = v;
        }
        if (rest.empty()) break;
    }
    return h;
}

// Feed one capture stream through the replay driver and load the CSV
// it emits: (t_s, grams, flow_gps) per W line.
// The driver retains both fitted slope and published EMA columns. Normal
// replay assertions use the same published flow as the LCD and BLE.
std::vector<replay_row> run_capture(std::istream& in, bool readout = true) {
    std::string line;
    REQUIRE(std::getline(in, line));
    const cap_header h = parse_header(line);
    REQUIRE(h.cpg > 0);
    std::ostringstream csv;
    replay::driver     drv(csv);
    drv.header(h.cpg, h.zero, h.tare);
    while (std::getline(in, line)) drv.line(line);

    std::vector<replay_row> rows;
    std::istringstream      rs(csv.str());
    std::string             r;
    std::getline(rs, r);                 // header
    while (std::getline(rs, r)) {
        if (r.empty() || r[0] == '#') continue;
        replay_row w{};
        double published;
        if (std::sscanf(r.c_str(), "%lf,%*d,%lf,%*f,%lf,%*d,%*d,%*d,%*d,%*d,%*f,%lf",
                        &w.t, &w.g, &published, &w.f) == 4) {
            if (readout) w.f = published;
            w.t /= 1000.0;
            rows.push_back(w);
        }
    }
    return rows;
}

std::vector<replay_row> run_readout_fixture(const char* name) {
    std::ifstream in(std::string(SCALE_TEST_FIXTURE_DIR) + "/" + name);
    REQUIRE(in.is_open());
    return run_capture(in, true);
}

// First index with rows[i].t >= t (rows are time-ordered).
std::size_t idx_at(const std::vector<replay_row>& rows, double t) {
    return static_cast<std::size_t>(
        std::ranges::lower_bound(rows, t, {}, &replay_row::t) -
        rows.begin());
}

// First index k in [a, b) whose contiguous run of flow >= thr covers at
// least hold_s, the whole hold lying before b. Returns b when absent —
// callers REQUIRE the result, never a silent zero delay.
std::size_t held_ge(const std::vector<replay_row>& rows, std::size_t a,
                    std::size_t b, double thr, double hold_s) {
    for (std::size_t k = a; k < b; ++k) {
        if (rows[k].f < thr) continue;
        std::size_t m = k + 1;
        while (m < b && rows[m].f >= thr) ++m;
        if (rows[m - 1].t - rows[k].t >= hold_s) return k;
        k = m;      // starts inside the same run reach hold_s no sooner
    }
    return b;
}

// Same as held_ge but for |flow| <= bound — the settle-to-zero hold.
std::size_t held_le(const std::vector<replay_row>& rows, std::size_t a,
                    std::size_t b, double bound, double hold_s) {
    for (std::size_t k = a; k < b; ++k) {
        if (std::fabs(rows[k].f) > bound) continue;
        std::size_t m = k + 1;
        while (m < b && std::fabs(rows[m].f) <= bound) ++m;
        if (rows[m - 1].t - rows[k].t >= hold_s) return k;
        k = m;
    }
    return b;
}

} // namespace

TEST_CASE("replay: ramp reproduces grams and flow") {
    std::ostringstream cap;
    cap << "# coffee-scale raw v1 cpg=1000.000000 zero=100000.0 "
           "tare=0.0 fw=test\n";
    // 0 g for 1 s, then a 5 g/s ramp for 10 s (50 g), then rest.
    // 80 Hz: t_us steps of 12500.
    std::int64_t us = 1'000'000;
    auto wline = [&](double grams) {
        cap << "W," << us << ',' << static_cast<long>(100000 + grams * 1000)
            << '\n';
        us += 12'500;
    };
    for (int i = 0; i < 80; ++i) wline(0.0);
    for (int i = 0; i < 800; ++i) wline(5.0 * (i * 0.0125));
    for (int i = 0; i < 240; ++i) wline(50.0);
    // a few accel lines (driver feeds every 5th — like firmware)
    for (int i = 0; i < 40; ++i) {
        cap << "A," << us << ",0.0,0.0,1000.0\n";
        us += 10'000;
    }
    cap << "T," << us << ",24.375\n";   // TMP102 -> set_temperature
    cap << "C," << us << ",31.50\n";    // chip temp — ignored
    cap << "E," << us << ",tare\n";
    cap << "M," << us << ",check-point\n";

    std::istringstream in(cap.str());
    const auto rows = run_capture(in);

    // scan the CSV: mid-ramp flow ≈ 5 g/s, tail grams ≈ 50
    REQUIRE(rows.size() > 500);
    CHECK(rows[500].f == doctest::Approx(5.0).epsilon(0.12));
    CHECK(rows.size() > 1000);
    CHECK(rows.back().g == doctest::Approx(50.0).epsilon(0.02));
}

// The old per-event sub-0.4 s gate/boost timing was a contract of the removed
// state machine. The fixed 1.2 s fit now has an explicit <=1.2 s response budget.
// Frozen event times and weight-derived rates remain, so no pulse is skipped.
TEST_CASE("replay: every captured pulse produces sustained positive flow") {
    struct event { double a, b, seventy_percent_rate; };
    const event pulsed[] = {
        {27.576, 28.870, 3.542},
        {32.562, 34.220, 3.990},
        {35.450, 37.196, 5.299},
        {38.741, 41.064, 4.221},
    };
    const event latest[] = {
        {7.946, 10.571, 4.956},
        {12.229, 15.054, 4.570},
        {16.926, 19.915, 5.335},
        {21.723, 24.033, 4.282},
        {25.866, 28.165, 3.568},
        {30.350, 32.635, 3.257},
        {34.155, 36.226, 5.027},
        {37.959, 39.931, 4.372},
    };
    const event rapid[] = {
        {6.691876, 7.973098, 4.610032},
        {8.789513, 9.894822, 4.680270},
        {10.761517, 12.055241, 5.207105},
        {13.499710, 14.906549, 4.568945},
        {15.936541, 17.129699, 5.438716},
        {18.084248, 19.704572, 5.639955},
        {20.646557, 21.864847, 6.203942},
        {22.957557, 24.226250, 5.585694},
        {25.168264, 26.612720, 6.518087},
        {27.755741, 28.948949, 6.400014},
        {29.777890, 30.317976, 6.635998},
        {30.556619, 31.272593, 4.217892},
        {31.925756, 32.704538, 5.115105},
        {33.520931, 35.291926, 5.596205},
        {35.907353, 36.598131, 3.642600},
        {37.439616, 38.306239, 5.385278},
    };
    const event noisy[] = {
        {6.139989, 6.906185, 3.684408},
        {7.873320, 9.330246, 3.947412},
        {11.000869, 12.533215, 4.482328},
        {15.635705, 16.728461, 3.500435},
        {18.386346, 19.855875, 4.270138},
        {20.772778, 21.890611, 4.960877},
        {22.920497, 24.050904, 5.059759},
        {25.645900, 27.027472, 6.477352},
    };
    const auto check = [](const char* fixture, std::size_t count, const auto& events) {
        INFO("fixture: ", std::string(fixture));
        const auto rows = run_readout_fixture(fixture);
        REQUIRE(rows.size() == count);
        for (std::size_t i = 0; i < std::size(events); ++i) {
            const auto& e = events[i];
            CAPTURE(e.a);
            const auto a = idx_at(rows,e.a);
            const double end = std::min(e.b+.5,
                i+1 < std::size(events) ? events[i+1].a : rows.back().t);
            const auto b = idx_at(rows,end);
            // A windowed estimate need not reach 70% during a <1 s pulse.
            // Its smoothed response may extend past the physical stop, but
            // must be established before the next pour and within 1.2 s.
            const double threshold = e.seventy_percent_rate * (3.0/7.0);
            const auto k = held_ge(rows,a,b,threshold,.15);
            REQUIRE(k < b);
            CHECK(rows[k].t-e.a <= 1.2);
        }
    };
    check("pulsed-pour.txt",3508,pulsed);
    check("latest-pulses.txt",3327,latest);
    check("rapid-pulses.txt",3138,rapid);
    check("noisy-pulses.txt",2269,noisy);
}

TEST_CASE("replay: real splash restart converges directly to the measured pour") {
    const auto rows = run_readout_fixture("splash-restart.txt");
    REQUIRE(rows.size() == 412);
    const auto a = idx_at(rows,34.155), b = idx_at(rows,36.226);
    const auto k = held_ge(rows,a,b,5.027,.15);
    REQUIRE(k < b);
    CHECK(rows[k].t-34.155 <= 1.2);
    double sum = 0, peak = 0;
    std::size_t n = 0;
    for (auto i = idx_at(rows,35.4); i < idx_at(rows,36.0); ++i) {
        CHECK(rows[i].f > 5.0);
        sum += rows[i].f; peak = std::max(peak,rows[i].f); ++n;
    }
    REQUIRE(n > 0);
    CHECK(sum/n >= 6.5);
    CHECK(sum/n <= 8.0);
    CHECK(peak <= 9.0);
    const auto z = held_le(rows,idx_at(rows,36.226),rows.size(),.30,.15);
    REQUIRE(z < rows.size());
    CHECK(rows[z].t-36.226 <= 1.4);
}

TEST_CASE("replay: October 3 continuous pouring cannot be gated to a low resume rate") {
    const auto rows = run_readout_fixture("pour-disturbance-20261003.txt");
    REQUIRE(rows.size() == 312);
    const auto a = idx_at(rows,19.821407+63.70), b = idx_at(rows,19.821407+65.05);
    REQUIRE(b > a);
    double minimum = 30, sum = 0;
    for (auto i = a; i < b; ++i) {
        CHECK(rows[i].f >= 2.0);
        minimum = std::min(minimum,rows[i].f); sum += rows[i].f;
    }
    MESSAGE("October 3 sustained pour: min=", minimum, " mean=", sum/(b-a));
    CHECK(sum/(b-a) > 3.0);
    CHECK(sum/(b-a) < 4.5);
}

TEST_CASE("replay: October 3 idle weight motion stays bounded without a zero latch") {
    const auto rows = run_readout_fixture("idle-disturbance-20261003.txt");
    REQUIRE(rows.size() == 307);
    const auto a = idx_at(rows,19.821407+101.50), b = idx_at(rows,19.821407+103.50);
    REQUIRE(b > a);
    double peak = 0, sum = 0;
    for (auto i = a; i < b; ++i) { peak = std::max(peak,std::fabs(rows[i].f)); sum += rows[i].f; }
    MESSAGE("idle: peak=", peak, " mean=", sum/(b-a));
    CHECK(peak <= 1.0);
    CHECK(std::fabs(sum/(b-a)) < .1);
}

TEST_CASE("replay: October 4 noisy restart does not need clean-window activation") {
    const auto rows = run_readout_fixture("restart-disturbance-20261004.txt");
    REQUIRE(rows.size() == 796);
    double stop_peak = 0, minimum = 30;
    int restart_samples = 0;
    for (const auto& row : rows) {
        const double t = row.t-17.006696;
        if (t >= 63.2 && t <= 64.2) stop_peak = std::max(stop_peak,std::fabs(row.f));
        if (t >= 68.1 && t <= 68.65) {
            CHECK(row.f >= 2.0);
            minimum = std::min(minimum,row.f); ++restart_samples;
        }
    }
    REQUIRE(restart_samples > 40);
    MESSAGE("October 4: stop peak=",stop_peak," restart min=",minimum);
    CHECK(stop_peak < 1.0);
}

TEST_CASE("replay: every original capture remains mandatory and produces finite bounded flow") {
    struct fixture { const char* name; std::size_t count; };
    for (const auto& f : {fixture{"pour-onset.txt",3539},
        fixture{"pulsed-pour.txt",3508},fixture{"latest-pulses.txt",3327},
        fixture{"rapid-pulses.txt",3138},fixture{"noisy-pulses.txt",2269}}) {
        INFO("fixture: ", std::string(f.name));
        const auto rows = run_readout_fixture(f.name);
        REQUIRE(rows.size() == f.count);
        for (const auto& r : rows) {
            CHECK(std::isfinite(r.g)); CHECK(std::isfinite(r.f));
            CHECK(std::fabs(r.f) <= 30.0);
        }
    }
}
