// Replay driver: a synthetic capture string through the shared parser +
/// scale::app feed path — grams must converge on the ramp target and the
/// derived flow must sit at the ramp slope.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../tools/replay_core.hpp"

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

    std::ostringstream csv;
    replay::driver drv(csv);
    std::istringstream in(cap.str());
    std::string line;
    REQUIRE(std::getline(in, line));
    // mirror replay.cpp's header parsing
    double cpg = 0, zero = 0, tare = 0;
    for (std::string_view rest = std::string_view(line);;) {
        const auto sp = rest.find(' ');
        const auto tok = rest.substr(0, sp);
        rest = sp == std::string_view::npos ? "" : rest.substr(sp + 1);
        if (const auto eq = tok.find('='); eq != std::string_view::npos) {
            double v = 0;
            const auto vs = tok.substr(eq + 1);
            std::from_chars(vs.data(), vs.data() + vs.size(), v);
            if (tok.substr(0, eq) == "cpg") cpg = v;
            else if (tok.substr(0, eq) == "zero") zero = v;
            else if (tok.substr(0, eq) == "tare") tare = v;
        }
        if (rest.empty()) break;
    }
    REQUIRE(cpg > 0);
    drv.header(cpg, zero, tare);
    while (std::getline(in, line)) drv.line(line);

    // scan the CSV: mid-ramp flow ≈ 5 g/s, tail grams ≈ 50
    std::istringstream rows(csv.str());
    std::string row;
    std::getline(rows, row);                 // header
    double lastG = 0, midFlow = 0;
    int    r = 0;
    while (std::getline(rows, row)) {
        if (row.empty() || row[0] == '#') continue;
        std::sscanf(row.c_str(), "%*f,%*d,%lf,%*f,%lf", &lastG, &midFlow);
        if (r == 500) {                      // well inside the ramp
            CHECK(midFlow == doctest::Approx(5.0).epsilon(0.12));
        }
        ++r;
    }
    CHECK(r > 1000);
    CHECK(lastG == doctest::Approx(50.0).epsilon(0.02));
}

TEST_CASE("replay: pour-onset capture bounds onset delay and rebound") {
    // captures/pour-onset.txt is a real pulsed pour whose splash knocks
    // used to gate and resume at f0=0 (~0.8 s onset lag) and whose stops
    // tailed down and rebounded to ~-1 g/s. Skip cleanly if the capture
    // is absent; run from the firmware dir or a couple of fallbacks.
    const char* paths[] = {"captures/pour-onset.txt",
                           "../captures/pour-onset.txt",
                           "firmware/captures/pour-onset.txt",
                           "../../../../captures/pour-onset.txt"};
    std::FILE* f = nullptr;
    for (const char* p : paths) {
        f = std::fopen(p, "r");
        if (f) break;
    }
    if (!f) {
        MESSAGE("captures/pour-onset.txt not found — skipped");
        return;
    }
    std::string cap;
    {
        char chunk[4096];
        std::size_t nr;
        while ((nr = std::fread(chunk, 1, sizeof chunk, f)) > 0)
            cap.append(chunk, nr);
        std::fclose(f);
    }
    std::istringstream in(cap);
    std::string        line;
    REQUIRE(std::getline(in, line));
    double cpg = 0, zero = 0, tare = 0;
    for (std::string_view rest = std::string_view(line);;) {
        const auto sp  = rest.find(' ');
        const auto tok = rest.substr(0, sp);
        rest = sp == std::string_view::npos ? "" : rest.substr(sp + 1);
        if (const auto eq = tok.find('='); eq != std::string_view::npos) {
            double v = 0;
            const auto vs = tok.substr(eq + 1);
            std::from_chars(vs.data(), vs.data() + vs.size(), v);
            if (tok.substr(0, eq) == "cpg") cpg = v;
            else if (tok.substr(0, eq) == "zero") zero = v;
            else if (tok.substr(0, eq) == "tare") tare = v;
        }
        if (rest.empty()) break;
    }
    REQUIRE(cpg > 0);
    std::ostringstream csv;
    replay::driver     drv(csv);
    drv.header(cpg, zero, tare);
    while (std::getline(in, line)) drv.line(line);

    struct row { double t, g, f; };
    std::vector<row> rows;
    {
        std::istringstream rs(csv.str());
        std::string        r;
        std::getline(rs, r);            // header
        while (std::getline(rs, r)) {
            if (r.empty() || r[0] == '#') continue;
            row w{};
            if (std::sscanf(r.c_str(), "%lf,%*d,%lf,%*f,%lf",
                            &w.t, &w.g, &w.f) == 3) {
                w.t /= 1000.0;
                rows.push_back(w);
            }
        }
    }
    REQUIRE(rows.size() > 3000);

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

    double worst_onset = 0, worst_rebound = 0;
    for (const auto& [a, b] : segs) {
        std::vector<double> ss(sl.begin() + a, sl.begin() + b);
        std::ranges::nth_element(ss, ss.begin() + ss.size() / 2);
        const double thr = std::max(2.0, 0.7 * ss[ss.size() / 2]);
        for (std::size_t k = a; k < n; ++k) {
            if (std::fabs(rows[k].f) >= thr) {
                worst_onset = std::max(worst_onset, rows[k].t - rows[a].t);
                break;
            }
        }
        const std::size_t e = std::min(n, b + 80);
        for (std::size_t k = b; k < e; ++k)
            worst_rebound = std::min(worst_rebound, rows[k].f);
    }
    CHECK(worst_onset <= 0.6);      // was 0.83 s before the resume fix
    CHECK(worst_rebound >= -0.6);   // was ~-1.0 before the snap/pin
}
