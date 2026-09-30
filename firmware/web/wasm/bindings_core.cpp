// scale_core -> WASM: embind wrapper around scale::app. This module is the
// shared logic library for any JS frontend (self-hosted page, PWA, tests):
// feed raw ADC counts in, get the same grams/flow/stability the firmware
// computes. Time is injected (ms, caller's clock) exactly like on target.

#include <cstdint>

#include <emscripten/bind.h>

#include "scale/app.hpp"

namespace {

/// Plain-field mirror of scale::app::snapshot for the JS ABI — ints for the
/// enums, ms for the duration, so the wire shape stays stable if internals
/// change. bindings_screen.cpp binds the same field names, letting a
/// snapshot object flow from this module straight into the screen preview.
struct JsSnapshot {
    float grams;
    float flow_gps;
    bool  stable;
    bool  tared;
    bool  calibrated;
    int   unit;        // scale::unit:        0 = gram, 1 = ounce
    int   mode;        // scale::app::mode:   0 = weigh, 1 = brew
    int   timer_state; // brew_timer::state:  0 = idle, 1 = running, 2 = paused
    int   timer_ms;
    float pitch_deg;
    float roll_deg;
};

class JsApp {
public:
    void feed(std::int32_t counts, double now_ms) {
        app_.feed(counts, scale::clock_ms{static_cast<std::int64_t>(now_ms)});
    }
    void feedAccel(float x_mg, float y_mg, float z_mg) {
        app_.feed_accel(x_mg, y_mg, z_mg);
    }
    void tare() { app_.tare(); }
    void tareLong() { app_.tare_long(); }
    void nextMode() { app_.next_mode(); }
    void setUnit(int u) { app_.set_unit(static_cast<scale::unit>(u)); }
    int  currentUnit() const { return static_cast<int>(app_.current_unit()); }
    int  currentMode() const { return static_cast<int>(app_.current_mode()); }

    void calZero() { app_.cal_zero(); }
    bool calSpan(float mass_g) { return app_.cal_span(mass_g); }
    void loadCalibration(float zero_counts, float counts_per_gram) {
        app_.load_calibration({.zero_counts     = zero_counts,
                               .counts_per_gram = counts_per_gram});
    }

    void setTemperature(float temp_c) { app_.set_temperature(temp_c); }

    float displayValue() const { return app_.display_value(); }

    JsSnapshot snapshot() const {
        const auto s = app_.state();
        return {.grams       = s.grams,
                .flow_gps    = s.flow_gps,
                .stable      = s.stable,
                .tared       = s.tared,
                .calibrated  = s.calibrated,
                .unit        = static_cast<int>(s.u),
                .mode        = static_cast<int>(s.m),
                .timer_state = static_cast<int>(s.timer_state),
                .timer_ms    = static_cast<int>(s.timer_elapsed.count()),
                .pitch_deg   = s.pitch_deg,
                .roll_deg    = s.roll_deg};
    }

private:
    scale::app app_;
};

} // namespace

EMSCRIPTEN_BINDINGS(scale_core) {
    using namespace emscripten;

    value_object<JsSnapshot>("Snapshot")
        .field("grams", &JsSnapshot::grams)
        .field("flowGps", &JsSnapshot::flow_gps)
        .field("stable", &JsSnapshot::stable)
        .field("tared", &JsSnapshot::tared)
        .field("calibrated", &JsSnapshot::calibrated)
        .field("unit", &JsSnapshot::unit)
        .field("mode", &JsSnapshot::mode)
        .field("timerState", &JsSnapshot::timer_state)
        .field("timerMs", &JsSnapshot::timer_ms)
        .field("pitchDeg", &JsSnapshot::pitch_deg)
        .field("rollDeg", &JsSnapshot::roll_deg);

    class_<JsApp>("ScaleApp")
        .constructor<>()
        .function("feed", &JsApp::feed)
        .function("feedAccel", &JsApp::feedAccel)
        .function("tare", &JsApp::tare)
        .function("tareLong", &JsApp::tareLong)
        .function("nextMode", &JsApp::nextMode)
        .function("setUnit", &JsApp::setUnit)
        .function("currentUnit", &JsApp::currentUnit)
        .function("currentMode", &JsApp::currentMode)
        .function("calZero", &JsApp::calZero)
        .function("calSpan", &JsApp::calSpan)
        .function("loadCalibration", &JsApp::loadCalibration)
        .function("setTemperature", &JsApp::setTemperature)
        .function("displayValue", &JsApp::displayValue)
        .function("snapshot", &JsApp::snapshot);
}
