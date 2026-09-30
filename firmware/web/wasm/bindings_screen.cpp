// Screen preview -> WASM: runs the real ui::create()/ui::update() LVGL code
// under Emscripten and exposes the rendered frame for a <canvas> blit. Feed
// it the same snapshot shape produced by the scale_core module (or by the
// device over WebSocket) and the page mirrors the physical ST7789 exactly.

#include <array>
#include <cstddef>
#include <cstdint>

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "lvgl.h"
#include "scale/app.hpp"
#include "scale_proto/proto.hpp"
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

// ---- BLE wire protocol (scale_proto/proto.hpp is the ABI source) ----------

/// Decode one state frame into the same {snap, displayValue, batteryPct,
/// charging} shape Screen.update() takes. Returns null on a bad frame.
emscripten::val decodeFrame(emscripten::val bytes) {
    const std::size_t n = bytes["length"].as<std::size_t>();
    if (n > 255) {
        return emscripten::val::null();
    }
    std::array<std::uint8_t, 255> buf{};
    for (std::size_t i = 0; i < n; ++i) {
        buf[i] = bytes[i].as<std::uint8_t>();
    }
    const auto s = proto::decode(
        std::span<const std::uint8_t>{buf.data(), n});
    if (!s) {
        return emscripten::val::null();
    }
    emscripten::val snap = emscripten::val::object();
    snap.set("grams", s->grams);
    snap.set("flowGps", s->flow_gps);
    snap.set("stable", s->stable);
    snap.set("tared", s->tared);
    snap.set("calibrated", s->calibrated);
    snap.set("unit", static_cast<int>(s->unit));
    snap.set("mode", static_cast<int>(s->mode));
    snap.set("timerState", static_cast<int>(s->timer_state));
    snap.set("timerMs", static_cast<double>(s->timer_ms));
    snap.set("pitchDeg", s->pitch_deg);
    snap.set("rollDeg", s->roll_deg);
    emscripten::val m = emscripten::val::object();
    m.set("snap", snap);
    m.set("displayValue", s->display_value);
    m.set("batteryPct", static_cast<int>(s->battery_pct));
    m.set("charging", s->charging);
    return m;
}

/// Encode a command (op + arg, see proto::op) into a fresh Uint8Array.
emscripten::val encodeCommand(int opcode, int arg) {
    const auto f = proto::encode_command(
        {static_cast<proto::op>(opcode), arg});
    emscripten::val arr =
        emscripten::val::global("Uint8Array").new_(f.size);
    arr.call<void>("set", emscripten::val(emscripten::typed_memory_view(
                              f.size, f.bytes.data())));
    return arr;
}

/// GATT UUIDs straight from the proto header — the page never hardcodes
/// them.
emscripten::val bleUuids() {
    emscripten::val o = emscripten::val::object();
    o.set("service", std::string(proto::kServiceUuid));
    o.set("state", std::string(proto::kStateUuid));
    o.set("command", std::string(proto::kCommandUuid));
    return o;
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
    function("decodeFrame", &decodeFrame);
    function("encodeCommand", &encodeCommand);
    function("bleUuids", &bleUuids);
    function("width", +[] { return ui::hor_res; });
    function("height", +[] { return ui::ver_res; });
}
