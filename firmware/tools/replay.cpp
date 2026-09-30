/// replay — feed a tools/capture.py raw capture back through the
/// scale_core pipeline on the host, emitting per-sample CSV.
///
///   xmake run replay <capture.txt> [out.csv]
///
/// The first line must be the '# coffee-scale raw v1' header; W/A/E/M
/// lines are replayed in file order (see replay_core.hpp).

#include <charconv>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "replay_core.hpp"

namespace {

/// '# coffee-scale raw v1 cpg=<> zero=<> tare=<> fw=<>'
bool parse_header(std::string_view l, replay::driver& d) {
    if (l.find("# coffee-scale raw") != 0) return false;
    double cpg = 0, zero = 0, tare = 0;
    for (std::string_view rest = l; ;) {
        const auto sp = rest.find(' ');
        const auto tok = rest.substr(0, sp);
        rest = sp == std::string_view::npos ? "" : rest.substr(sp + 1);
        const auto eq = tok.find('=');
        if (eq != std::string_view::npos) {
            double v = 0;
            const auto vs = tok.substr(eq + 1);
            std::from_chars(vs.data(), vs.data() + vs.size(), v);
            const auto key = tok.substr(0, eq);
            if (key == "cpg") cpg = v;
            else if (key == "zero") zero = v;
            else if (key == "tare") tare = v;
        }
        if (rest.empty()) break;
    }
    if (cpg == 0) return false;
    d.header(cpg, zero, tare);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: replay <capture.txt> [out.csv]\n");
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    std::ofstream fout;
    std::ostream* outp = &std::cout;
    if (argc > 2) {
        fout.open(argv[2]);
        if (!fout) {
            std::fprintf(stderr, "cannot write %s\n", argv[2]);
            return 1;
        }
        outp = &fout;
    }
    std::ostream& out = *outp;

    replay::driver drv(out);
    bool have_header = false;
    std::string line;
    std::size_t nw = 0;
    while (std::getline(in, line)) {
        if (!have_header) {
            if (parse_header(line, drv)) {
                have_header = true;
            }
            continue;                    // skip comments/log lines
        }
        drv.line(line);
        if (line.rfind("W,", 0) == 0) ++nw;
    }
    if (!have_header) {
        std::fprintf(stderr, "%s: no '# coffee-scale raw' header\n", argv[1]);
        return 1;
    }
    out.flush();
    std::fprintf(stderr, "replayed %zu W lines%s\n", nw,
                 argc > 2 ? " -> csv" : "");
    return 0;
}
