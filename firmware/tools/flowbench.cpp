// Flow measurement tool. Uses the production trailing regression + EMA;
// no estimator-only boost/gate states exist.
//   flowbench                  synthetic weight trends
//   flowbench trace ramp       per-sample CSV
//   flowbench cap capture.txt  raw W lines, with header calibration
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include "scale/flow_rate.hpp"

namespace {
struct probe {
    scale::flow_rate flow;
    scale::median_filter<3> median;
    float feed(double seconds, float grams) {
        const auto counts = static_cast<std::int32_t>(std::lround(grams*10000));
        flow.push(scale::clock_ms{static_cast<std::int64_t>(std::llround(seconds*1000))},
                  median.push(counts)/10000.f);
        const auto f = flow.readout_rate();
        return std::fabs(f) < .3f ? 0.f : std::clamp(f, -30.f, 30.f);
    }
};
float weight(const std::string& scenario, double t, double rate) {
    if (scenario == "step") return t < 5 ? 0 : static_cast<float>(rate);
    if (scenario == "wiggle") return t < 3 ? 0 : rate*std::sin(2*3.141592653589793*1.5*(t-3));
    if (scenario == "slow-restart") return t < 2 ? 0 : t < 5 ? 15*(t-2)
        : t < 7 ? 45 : 45+rate*(t-7);
    const double mass = t < 2 ? 0 : t < 8 ? rate*(t-2) : rate*6;
    if (scenario == "ripple" && t >= 2 && t < 8)
        return mass+.4*std::sin(2*3.141592653589793*3*t);
    return mass;
}
void bench(const std::string& scenario, double rate, bool trace = false) {
    probe p;
    float peak = 0, minimum = 30, tail = 0;
    double sum = 0;
    int n = 0, zeros = 0;
    if (trace) std::puts("t,grams,flow_gps,fit_gps,level_step,fitted_weight");
    for (int i = 0; i <= 12*80; ++i) {
        const double t = i/80.;
        if (scenario == "gap" && t >= 5 && t < 5.2) continue;
        const auto g = weight(scenario,t,rate), f = p.feed(t,g);
        peak = std::max(peak,std::fabs(f));
        if (t >= 4 && t < 7) { sum += f; ++n; minimum = std::min(minimum,f); zeros += f == 0; }
        if (t >= 10) tail = std::max(tail,std::fabs(f));
        if (trace) std::printf("%.4f,%.4f,%.4f,%.4f,%d,%.4f\n",t,g,f,
            p.flow.rate(),p.flow.discontinuity()?1:0,p.flow.weight());
    }
    if (!trace) std::printf("%s rate=%.2f mean=%.3f min=%.3f zeros=%d peak=%.3f tail=%.3f\n",
        scenario.c_str(),rate,sum/n,minimum,zeros,peak,tail);
}
int capture(const char* path) {
    std::ifstream input(path);
    if (!input) { std::fprintf(stderr,"cannot open %s\n",path); return 1; }
    std::string line;
    if (!std::getline(input,line)) return 1;
    const auto pos = line.find("cpg=");
    if (pos == std::string::npos) { std::fputs("missing capture calibration\n",stderr); return 1; }
    const float cpg = std::stof(line.substr(pos+4));
    if (!(cpg > 0)) return 1;
    scale::flow_rate flow;
    scale::median_filter<3> median;
    std::puts("t,flow_gps,fit_gps,level_step,fitted_weight");
    while (std::getline(input,line)) {
        long long us; int raw;
        if (std::sscanf(line.c_str(),"W,%lld,%d",&us,&raw) != 2) continue;
        flow.push(scale::clock_ms{us/1000},median.push(raw)/cpg);
        std::printf("%.6f,%.4f,%.4f,%d,%.4f\n",us/1e6,flow.readout_rate(),
            flow.rate(),flow.discontinuity()?1:0,flow.weight());
    }
    return 0;
}
}
int main(int argc,char**argv) {
    if (argc >= 3 && std::string(argv[1]) == "cap") return capture(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "trace") {
        bench(argv[2],argc > 3 ? std::stof(argv[3]) : 4.f,true); return 0;
    }
    for (float rate : {.35f,.6f,3.f,6.f,15.f,30.f}) bench("ramp",rate);
    bench("ripple",6); bench("step",300); bench("gap",4);
    for (float rate : {.35f,.6f,3.f}) bench("slow-restart",rate);
}
