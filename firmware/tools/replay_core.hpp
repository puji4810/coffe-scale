#pragma once

/// Replay driver for tools/capture.py raw captures — shared between the
/// `replay` xmake binary and the doctest in tests/ so both exercise the
/// same parser + model calls.
///
/// Capture format (app_main.cpp 'r' stream):
///   # coffee-scale raw v1 cpg=<f> zero=<f> tare=<f> fw=<s>
///   W,<t_us>,<raw_counts>
///   A,<t_us>,<x_mg>,<y_mg>,<z_mg>
///   E,<t_us>,<name>        tare tare_long mode unit timer_toggle
///                          timer_reset cal_zero cal_span
///   M,<t_us>,<label>       user markers -> passed through as #M rows
///   T,<t_us>,<temp_c>      TMP102 -> app set_temperature (mirrors firmware)
///   C,<t_us>,<chip_c>      internal chip temp — ignored
///
/// The model is scale::app fed exactly like firmware:
/// feed(counts, clock_ms{t_us/1000}) per W, feed_accel on every 5th A
/// (firmware decimates the 100 Hz poll to 20 Hz). At the first W line the
/// header's tare is applied by priming the pipeline on constant
/// zero+tare counts and calling tare() — the same code path as the
/// button. cal_span events carry no arg, so they cannot be applied.

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string_view>

#include "scale/app.hpp"

namespace replay {

class driver {
public:
    explicit driver(std::ostream& out) : out_(out) {
        out_ << "t_ms,raw,grams,display_g,flow_gps,stable,system_stable,"
                "disturbed,ftrip,fboost,fresume\n";
    }

    /// '# coffee-scale raw v1' header — stash calibration; the tare is
    /// applied lazily at the first W sample (the pipeline needs its real
    /// time base).
    void header(double cpg, double zero, double tare) {
        scale::calibration c;
        c.counts_per_gram = static_cast<float>(cpg);
        c.zero_counts     = static_cast<float>(zero);
        zero_counts_      = static_cast<float>(zero);
        tare_counts_      = static_cast<float>(tare);
        app_.load_calibration(c);
        have_header_ = true;
    }

    /// One capture line (no trailing newline).
    void line(std::string_view l) {
        if (l.size() < 2 || l[1] != ',') return;
        const char tag = l[0];
        l.remove_prefix(2);
        if (tag == 'W') {
            const auto us = num(l);
            const auto counts = num(l);
            w(us, static_cast<std::int32_t>(counts));
        } else if (tag == 'A') {
            const auto us = num(l);
            const auto x = num(l), y = num(l), z = num(l);
            accel(us, x, y, z);
        } else if (tag == 'E') {
            num(l);                       // t_us unused — ordering is by line
            event(trim(l));
        } else if (tag == 'T') {
            num(l);                       // t_us unused
            app_.set_temperature(static_cast<float>(num(l)));
        } else if (tag == 'C') {
            // internal chip temp — recorded for the analyst only
        } else if (tag == 'M') {
            const auto us = num(l);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "#M,%.3f,", us / 1000.0);
            out_ << buf << l << '\n';
        }
    }

private:
    /// csv field: consume up to next ',' and parse as double.
    double num(std::string_view& l) {
        const auto c = l.find(',');
        const auto tok = l.substr(0, c);
        l.remove_prefix(c == std::string_view::npos ? l.size() : c + 1);
        double v = 0;
        std::from_chars(tok.data(), tok.data() + tok.size(), v);
        return v;
    }

    static std::string_view trim(std::string_view s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) {
            s.remove_suffix(1);
        }
        return s;
    }

    /// Apply the captured tare: prime on constant zero+tare counts so
    /// filtered_ converges, then tare() — tare_counts_ becomes
    /// filtered - zero - drift(25 C), i.e. the captured value (the
    /// thermal model is anchored at 25 C so the drift term is ~0).
    /// The first ~second of replayed output is a convergence transient.
    void prime_and_tare(std::int64_t first_us) {
        constexpr int kPrime = 800;    // 10 s at 80 Hz — EMA/median settle
        const auto counts = static_cast<std::int32_t>(
            zero_counts_ + tare_counts_);
        for (int i = kPrime; i > 0; --i) {
            app_.feed(counts, scale::clock_ms{
                first_us / 1000 - static_cast<std::int64_t>(i * 25 / 2)});
        }
        app_.tare();
    }

    void w(std::int64_t us, std::int32_t counts) {
        if (have_header_ && !primed_) {
            prime_and_tare(us);
            primed_ = true;
        }
        app_.feed(counts, scale::clock_ms{us / 1000});
        const auto s = app_.state();   // snapshot.stable == system_stable
        const auto& d = app_.inner().last_diag();
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "%.3f,%ld,%.4f,%.2f,%.3f,%d,%d,%d,%d,%d,%.2f\n",
                      us / 1000.0, static_cast<long>(counts),
                      static_cast<double>(s.grams),
                      static_cast<double>(app_.display_value()),
                      static_cast<double>(s.flow_gps),
                      app_.inner().stable() ? 1 : 0,
                      s.stable ? 1 : 0,
                      app_.inner().disturbed() ? 1 : 0,
                      d.flow_trip, d.flow_boost,
                      static_cast<double>(d.flow_resume));
        out_ << buf;
    }

    void accel(std::int64_t /*us*/, double x, double y, double z) {
        // firmware feeds the model every 5th accel sample
        if (++accel_decim_ >= 5) {
            accel_decim_ = 0;
            app_.feed_accel(static_cast<float>(x), static_cast<float>(y),
                            static_cast<float>(z));
        }
    }

    void event(std::string_view name) {
        if (name == "tare")                       app_.tare();
        else if (name == "tare_long")             app_.tare_long();
        else if (name == "timer_toggle")          app_.tare_long();
        else if (name == "timer_reset")           app_.timer_reset();
        else if (name == "mode")                  app_.next_mode();
        else if (name == "unit") {
            app_.set_unit(app_.current_unit() == scale::unit::gram
                              ? scale::unit::ounce : scale::unit::gram);
        } else if (name == "cal_zero")            app_.cal_zero();
        // cal_span carries no mass arg on the wire — cannot be applied.
    }

    scale::app  app_;
    std::ostream& out_;
    bool        have_header_ = false;
    bool        primed_      = false;
    float       zero_counts_ = 0.0f;
    float       tare_counts_ = 0.0f;
    int         accel_decim_ = 0;
};

} // namespace replay
