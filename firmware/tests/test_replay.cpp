// Replay driver: a synthetic capture string through the shared parser +
/// scale::app feed path — grams must converge on the ramp target and the
/// derived flow must sit at the ramp slope.

#include <doctest/doctest.h>

#include <cmath>
#include <sstream>
#include <string>
#include <string_view>

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
