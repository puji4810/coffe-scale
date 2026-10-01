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
std::vector<replay_row> run_capture(std::istream& in) {
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
        if (std::sscanf(r.c_str(), "%lf,%*d,%lf,%*f,%lf",
                        &w.t, &w.g, &w.f) == 3) {
            w.t /= 1000.0;
            rows.push_back(w);
        }
    }
    return rows;
}

// A fixture is mandatory now — REQUIRE it opens, then replay it.
std::vector<replay_row> run_fixture(const char* name) {
    const std::string path =
        std::string(SCALE_TEST_FIXTURE_DIR) + "/" + name;
    std::ifstream f(path);
    REQUIRE(f.is_open());
    return run_capture(f);
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

TEST_CASE("replay: pour-onset capture bounds onset delay and rebound") {
    // tests/fixtures/pour-onset.txt is a real pulsed pour whose splash
    // used to gate and resume at f0=0 (~0.8 s onset lag) and whose stops
    // tailed down and rebounded to ~-1 g/s.
    const auto rows = run_fixture("pour-onset.txt");
    REQUIRE(rows.size() == 3539);        // every W line is a CSV row

    // smoothed grams slope over a 0.3 s centred window -> pour segments
    const std::size_t n = rows.size();
    std::vector<double> sl(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t i0 = i, i1 = i;
        while (i0 > 0 && rows[i].t - rows[i0].t < 0.15) --i0;
        while (i1 < n - 1 && rows[i1].t - rows[i].t < 0.15) ++i1;
        const std::size_t m = i1 - i0;
        if (m < 3) continue;
        double mx = 0, my = 0;
        for (std::size_t k = i0; k < i1; ++k) { mx += rows[k].t; my += rows[k].g; }
        mx /= m; my /= m;
        double den = 0, num = 0;
        for (std::size_t k = i0; k < i1; ++k) {
            den += (rows[k].t - mx) * (rows[k].t - mx);
            num += (rows[k].t - mx) * (rows[k].g - my);
        }
        sl[i] = den > 0 ? num / den : 0.0;
    }
    std::vector<std::pair<std::size_t, std::size_t>> segs;
    for (std::size_t i = 0; i < n;) {
        if (sl[i] > 3.0) {
            std::size_t j = i;
            while (j < n && (sl[j] > 1.5 || rows[j].t - rows[i].t < 0.1)) ++j;
            if (rows[j - 1].t - rows[i].t > 0.4) segs.emplace_back(i, j);
            i = j;
        } else ++i;
    }
    REQUIRE(segs.size() >= 6);

    double worst_onset = 0, worst_held = 0, worst_rebound = 0;
    for (std::size_t s = 0; s < segs.size(); ++s) {
        const auto [a, b] = segs[s];
        std::vector<double> ss(sl.begin() + a, sl.begin() + b);
        std::ranges::nth_element(ss, ss.begin() + ss.size() / 2);
        const double thr = std::max(2.0, 0.7 * ss[ss.size() / 2]);
        // Positive crossings only, searched inside the segment — a miss
        // must fail, never default to a zero delay.
        std::size_t kfirst = b;
        for (std::size_t k = a; k < b; ++k) {
            if (rows[k].f >= thr) { kfirst = k; break; }
        }
        REQUIRE(kfirst < b);
        worst_onset = std::max(worst_onset, rows[kfirst].t - rows[a].t);
        const std::size_t kheld = held_ge(rows, a, b, thr, 0.15);
        REQUIRE(kheld < b);
        worst_held = std::max(worst_held, rows[kheld].t - rows[a].t);
        // Stop hold: the first full 150 ms of |flow| <= 0.3 that lies
        // entirely inside this pulse's stop interval (up to the next
        // onset, or the capture end for the last one).
        const std::size_t stop_end =
            s + 1 < segs.size() ? segs[s + 1].first : n;
        const std::size_t kz = held_le(rows, b, stop_end, 0.3, 0.15);
        REQUIRE(kz < stop_end);
        const std::size_t e = std::min(n, b + 80);
        for (std::size_t k = b; k < e; ++k)
            worst_rebound = std::min(worst_rebound, rows[k].f);
    }
    CHECK(worst_onset <= 0.6);      // first crossing — was 0.83 s pre-fix
    CHECK(worst_held <= 0.65);      // 150 ms held crossing
    CHECK(worst_rebound >= -0.6);   // was ~-1.0 before the snap/pin
}

TEST_CASE("replay: pulsed-pour per-event onset and settle") {
    // tests/fixtures/pulsed-pour.txt (raw-20261001-101541): four fixed
    // pour pulses. The reference window seconds and the positive
    // 70%-of-median-slope thresholds are frozen from review — this is a
    // regression gate on held onset and settle, not an event search.
    struct evt { double a, b, thr, onset_max, zero_max, hold_max; };
    const evt evts[] = {
        // Event 0: hold_max .40 tolerates a recorded ~.11 g slosh bump
        // whose residual flow reaches .33 - NOT a claim of perfect
        // zero; the bump is real weight motion kept visible. Events
        // 1..3 keep the strict .3 deadband persistence.
        {27.576, 28.870, 3.542, 0.40, 0.35, 0.40},
        {32.562, 34.220, 3.990, 0.50, 0.35, 0.30},
        {35.450, 37.196, 5.299, 0.55, 0.80, 0.30},
        // Event 3: zero_max .40 is a conservative regression bound, not
        // the goal - .30 remains the desired clean-stop target, but
        // this capture carries real settle motion (measured .364) and
        // the zero-hold guard is intentionally conservative so low-rate
        // pours stay releasable. Not all stops are claimed to meet .30.
        {38.741, 41.064, 4.221, 0.50, 0.40, 0.30},
    };
    const auto rows = run_fixture("pulsed-pour.txt");
    REQUIRE(rows.size() == 3508);        // every W line is a CSV row

    const double last_t = rows.back().t;
    for (std::size_t i = 0; i < std::size(evts); ++i) {
        const auto& e = evts[i];
        CAPTURE(i);
        const std::size_t a = idx_at(rows, e.a);
        const std::size_t b = idx_at(rows, e.b);
        REQUIRE(b > a);
        // Onset: first and 150 ms-held positive threshold crossings,
        // searched ONLY inside the event window — a miss fails.
        std::size_t kfirst = b;
        for (std::size_t k = a; k < b; ++k) {
            if (rows[k].f >= e.thr) { kfirst = k; break; }
        }
        REQUIRE(kfirst < b);
        const std::size_t kheld = held_ge(rows, a, b, e.thr, 0.15);
        REQUIRE(kheld < b);
        CHECK(rows[kheld].t - e.a <= e.onset_max);
        // Held zero: first full 150 ms of |flow| <= 0.3 inside
        // [stop, min(stop + 1.5 s, next onset or last sample)).
        const double z_end_t =
            std::min(e.b + 1.5,
                     i + 1 < std::size(evts) ? evts[i + 1].a : last_t);
        const std::size_t zend = idx_at(rows, z_end_t);
        const std::size_t kz   = held_le(rows, b, zend, 0.3, 0.15);
        REQUIRE(kz < zend);
        CHECK(rows[kz].t - e.b <= e.zero_max);
        // Post-hold flow stays bounded through the rest of the window
        // (up to 1.5 s, cut 0.25 s short of the next onset's slosh-in).
        // hold_max is .3 deadband-exact except where the event records
        // real bump motion - see the table comment.
        const double   hold_end_t =
            std::min(e.b + 1.5,
                     i + 1 < std::size(evts) ? evts[i + 1].a - 0.25
                                             : last_t);
        const std::size_t hend = idx_at(rows, hold_end_t);
        double           worst_hold = 0.0;
        for (std::size_t k = kz; k < hend; ++k)
            worst_hold =
                std::max(worst_hold, std::fabs(rows[k].f));
        CHECK(worst_hold <= e.hold_max);
    }
}

TEST_CASE("replay: splash restart sustains flow after the gate") {
    // raw-20261001-115032, cropped to 32.5..37.709 s. This preserves the
    // quiet lead-in and the entire splash/pour/stop, and reproduces the
    // full capture's 0.703 s held onset. The reference is a raw-weight
    // trend crossing, not a synchronized physical-action marker.
    const auto rows = run_fixture("splash-restart.txt");
    REQUIRE(rows.size() == 412);
    constexpr double start = 34.155, stop = 36.226;
    const std::size_t a = idx_at(rows, start);
    const std::size_t b = idx_at(rows, stop);
    const std::size_t k = held_ge(rows, a, b, 5.027, 0.15);
    REQUIRE(k < b);
    CHECK(rows[k].t - start <= 0.40);

    // A faster resume must remain a sustained pour, not a one-sample
    // peak or an inflated estimate of this ~7.2 g/s segment.
    double sum = 0.0, peak = 0.0;
    std::size_t n = 0;
    for (std::size_t i = idx_at(rows, 35.0);
         i < idx_at(rows, 36.0); ++i) {
        CHECK(rows[i].f >= 5.027);
        sum += rows[i].f;
        peak = std::max(peak, rows[i].f);
        ++n;
    }
    REQUIRE(n > 0);
    CHECK(sum / static_cast<double>(n) >= 6.5);
    CHECK(sum / static_cast<double>(n) <= 8.0);
    CHECK(peak <= 9.0);
}

TEST_CASE("replay: splash stop keeps zero through the settling ring") {
    // The first displayed zero at 36.653 s used to be only a deadband
    // crossing: the stillness latch never engaged, and the next ring
    // crest resurfaced at 0.995 g/s. Bound every later sample through
    // the quiet interval, cut 0.25 s before the next weight onset.
    const auto rows = run_fixture("splash-restart.txt");
    REQUIRE(rows.size() == 412);
    REQUIRE(rows.back().t > 37.69);
    double peak = 0.0;
    for (std::size_t i = idx_at(rows, 36.653); i < rows.size(); ++i)
        peak = std::max(peak, std::fabs(rows[i].f));
    CHECK(peak <= 0.30);
}

TEST_CASE("replay: latest pulses keep each onset and stop bounded") {
    // Freeze all eight weight-reference intervals, including the first
    // pour: premature gate release at a small adopted slope used to
    // improve pulse 6 while delaying pulse 0 by 0.20 s.
    struct evt { double a, b, thr, onset_max, zero_max, hold_max; };
    const evt evts[] = {
        // The first three stops still carry measured settling motion.
        // These hold_max values are regression tolerances, not exact
        // zero guarantees; pulse 6 is the zero-reappearance fix.
        { 7.946, 10.571, 4.956, 0.15, 0.65, 0.55},
        {12.229, 15.054, 4.570, 0.25, 0.38, 0.95},
        {16.926, 19.915, 5.335, 0.15, 0.48, 1.30},
        {21.723, 24.033, 4.282, 0.15, 0.29, 0.30},
        {25.866, 28.165, 3.568, 0.33, 0.25, 0.30},
        {30.350, 32.635, 3.257, 0.30, 0.32, 0.30},
        {34.155, 36.226, 5.027, 0.40, 0.45, 0.30},
        {37.959, 39.931, 4.372, 0.32, 0.69, 0.30},
    };
    const auto rows = run_fixture("latest-pulses.txt");
    REQUIRE(rows.size() == 3327);
    for (std::size_t i = 0; i < std::size(evts); ++i) {
        CAPTURE(i);
        const auto& e = evts[i];
        const std::size_t a = idx_at(rows, e.a), b = idx_at(rows, e.b);
        const std::size_t k = held_ge(rows, a, b, e.thr, 0.15);
        REQUIRE(k < b);
        CHECK(rows[k].t - e.a <= e.onset_max);
        const double end = std::min(e.b + 1.5,
            i + 1 < std::size(evts) ? evts[i + 1].a - 0.25 : rows.back().t);
        const std::size_t zend = idx_at(rows, end);
        const std::size_t z = held_le(rows, b, zend, 0.30, 0.15);
        REQUIRE(z < zend);
        CHECK(rows[z].t - e.b <= e.zero_max);
        double peak = 0.0;
        for (std::size_t j = z; j < zend; ++j)
            peak = std::max(peak, std::fabs(rows[j].f));
        CHECK(peak <= e.hold_max);
    }
}

TEST_CASE("replay: noisy restarts sustain flow without waiting for a clean fast window") {
    struct evt { const char* fixture; std::size_t count; double a, b, thr; };
    const evt evts[] = {
        {"rapid-pulses.txt", 3138, 10.761517, 12.055241, 5.207},
        {"noisy-pulses.txt", 2269, 7.873320, 9.330246, 3.947},
        {"noisy-pulses.txt", 2269, 25.645900, 27.027472, 6.477},
    };
    for (const auto& e : evts) {
        INFO("fixture: ", std::string(e.fixture));
        CAPTURE(e.a);
        const auto rows = run_fixture(e.fixture);
        REQUIRE(rows.size() == e.count);
        const auto a = idx_at(rows, e.a), b = idx_at(rows, e.b);
        const auto k = held_ge(rows, a, b, e.thr, 0.15);
        REQUIRE(k < b);
        CHECK(rows[k].t - e.a <= 0.40);
        // The resumed pour must keep up through its middle, not merely
        // hold one early threshold crossing before falling back to zero.
        for (std::size_t j = idx_at(rows, e.a + 0.65);
             j < idx_at(rows, e.b - 0.25); ++j)
            CHECK(rows[j].f >= e.thr);
    }
}

TEST_CASE("replay: rapid and noisy pulses preserve every onset and recorded zero hold") {
    // Fixed raw-weight references for both full captures. Short gaps
    // without a baseline 150 ms zero run have zero_max = 0; these are
    // onset-only guards, not evidence of a stable stop.
    struct evt { double a, b, thr, onset_max, zero_max, hold_max; };
    const evt rapid[] = {
        {6.691876, 7.973098, 4.610032, 0.339, 0.30, 0.30},
        {8.789513, 9.894822, 4.680270, 0.301, 0.30, 0.30},
        {10.761517, 12.055241, 5.207105, 0.400, 0.38, 0.30},
        {13.499710, 14.906549, 4.568945, 0.163, 0.47, 0.30},
        {15.936541, 17.129699, 5.438716, 0.314, 0.00, 0.30},
        {18.084248, 19.704572, 5.639955, 0.628, 0.55, 0.30},
        {20.646557, 21.864847, 6.203942, 0.439, 0.58, 0.30},
        {22.957557, 24.226250, 5.585694, 0.440, 0.00, 0.30},
        {25.168264, 26.612720, 6.518087, 0.615, 0.65, 0.30},
        {27.755741, 28.948949, 6.400014, 0.364, 0.00, 0.30},
        {29.777890, 30.317976, 6.635998, 0.352, 0.00, 0.30},
        {30.556619, 31.272593, 4.217892, 0.176, 0.00, 0.30},
        {31.925756, 32.704538, 5.115105, 0.276, 0.00, 0.30},
        {33.520931, 35.291926, 5.596205, 0.477, 0.00, 0.30},
        {35.907353, 36.598131, 3.642600, 0.301, 0.34, 0.30},
        {37.439616, 38.306239, 5.385278, 0.389, 0.25, 0.30},
    };
    const evt noisy[] = {
        {6.139989, 6.906185, 3.684408, 0.289, 0.28, 0.30},
        {7.873320, 9.330246, 3.947412, 0.400, 0.26, 0.30},
        {11.000869, 12.533215, 4.482328, 0.427, 0.29, 0.34},
        {15.635705, 16.728461, 3.500435, 0.264, 0.54, 0.59},
        {18.386346, 19.855875, 4.270138, 0.452, 0.28, 0.30},
        {20.772778, 21.890611, 4.960877, 0.389, 0.34, 0.30},
        {22.920497, 24.050904, 5.059759, 0.578, 0.60, 0.30},
        {25.645900, 27.027472, 6.477352, 0.400, 0.23, 0.30},
    };
    const auto check = [](const char* fixture, std::size_t count,
                          const auto& evts) {
        INFO("fixture: ", std::string(fixture));
        const auto rows = run_fixture(fixture);
        REQUIRE(rows.size() == count);
        for (std::size_t i = 0; i < std::size(evts); ++i) {
            CAPTURE(i);
            const auto& e = evts[i];
            const auto a = idx_at(rows, e.a), b = idx_at(rows, e.b);
            const auto k = held_ge(rows, a, b, e.thr, 0.15);
            REQUIRE(k < b);
            CHECK(rows[k].t - e.a <= e.onset_max);
            if (e.zero_max == 0.0) continue;
            const double end = std::min(e.b + 1.5,
                i + 1 < std::size(evts) ? evts[i + 1].a - 0.25
                                       : rows.back().t);
            const auto zend = idx_at(rows, end);
            const auto z = held_le(rows, b, zend, 0.30, 0.15);
            REQUIRE(z < zend);
            CHECK(rows[z].t - e.b <= e.zero_max);
            double peak = 0.0;
            for (std::size_t j = z; j < zend; ++j)
                peak = std::max(peak, std::fabs(rows[j].f));
            CHECK(peak <= e.hold_max);
        }
    };
    check("rapid-pulses.txt", 3138, rapid);
    check("noisy-pulses.txt", 2269, noisy);
}
