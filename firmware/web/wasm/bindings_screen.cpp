// Screen preview -> WASM: runs the real ui::create()/ui::update() LVGL code
// under Emscripten and exposes the rendered frame for a <canvas> blit. Feed
// it the same snapshot shape produced by the scale_core module (or by the
// device over WebSocket) and the page mirrors the physical ST7789 exactly.

#include <cstddef>
#include <cstdint>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "lvgl.h"
#include "scale/app.hpp"
#include "ui/ui.hpp"
#include "wasm_port.hpp"

namespace {

/// Same wire shape as the scale_core module's Snapshot — field names must
/// stay identical so objects pass between the two modules untouched.
struct JsSnapshot {
    float grams;
    float flow_gps;
    bool  stable;
    bool  tared;
    bool  calibrated;
    int   unit;
    int   mode;
    int   timer_state;
    int   timer_ms;
    float pitch_deg;
    float roll_deg;
};

/// Mirror of ui::model with the flattened snapshot.
struct JsModel {
    JsSnapshot snap;
    float      display_value;
    int        battery_pct;
    bool       charging;
};

bool s_ready = false;

void init() {
    if (s_ready) {
        return;
    }
    ui::port_init();
    ui::create();
    s_ready = true;
}

void update(const JsModel& m) {
    const scale::app::snapshot snap{
        .grams         = m.snap.grams,
        .flow_gps      = m.snap.flow_gps,
        .stable        = m.snap.stable,
        .tared         = m.snap.tared,
        .calibrated    = m.snap.calibrated,
        .u             = static_cast<scale::unit>(m.snap.unit),
        .m             = static_cast<scale::app::mode>(m.snap.mode),
        .timer_state =
            static_cast<scale::brew_timer::state>(m.snap.timer_state),
        .timer_elapsed = scale::clock_ms{m.snap.timer_ms},
        .pitch_deg     = m.snap.pitch_deg,
        .roll_deg      = m.snap.roll_deg};
    ui::update({.snap          = snap,
                .display_value = m.display_value,
                .battery_pct   = m.battery_pct,
                .charging      = m.charging});
}

/// Advance LVGL by one iteration; true = framebuffer changed, blit it.
bool pump() {
    ui::housekeeping();
    lv_timer_handler();
    return ui::wasm::take_dirty();
}

emscripten::val framebuffer() {
    return emscripten::val(emscripten::typed_memory_view(
        ui::wasm::framebuffer_len(), ui::wasm::framebuffer()));
}

} // namespace

EMSCRIPTEN_BINDINGS(scale_screen) {
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

    value_object<JsModel>("ScreenModel")
        .field("snap", &JsModel::snap)
        .field("displayValue", &JsModel::display_value)
        .field("batteryPct", &JsModel::battery_pct)
        .field("charging", &JsModel::charging);

    function("init", &init);
    function("update", &update);
    function("pump", &pump);
    function("framebuffer", &framebuffer);
    function("width", +[] { return ui::hor_res; });
    function("height", +[] { return ui::ver_res; });
}
