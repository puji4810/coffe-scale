#pragma once
/// Link to the scale: WiFi STA onto its SoftAP + WebSocket client.
///
/// The scale speaks exactly the protocol its web UI uses:
///   ws://192.168.4.1/ws pushes snapshot JSON at ~20 Hz and accepts
///   {"cmd": tare|long|mode|unit0|unit1|calzero|calspan:<g>|sleep}.
/// Field names are the same wire ABI the wasm modules consume.

#include <cstdint>

namespace net {

/// One decoded snapshot frame — mirrors m.snap in the web app.
struct snapshot {
    float grams          = 0;
    float flow_gps       = 0;
    float display_value  = 0;   // weight in the scale's current unit
    float pitch_deg      = 0;
    float roll_deg       = 0;
    long  timer_ms       = 0;
    int   battery_pct    = -1;
    int   unit           = 0;   // 0=g 1=oz
    int   mode           = 0;   // 0=WEIGH 1=BREW
    int   timer_state    = 0;
    bool  stable         = false;
    bool  tared          = false;
    bool  calibrated     = false;
    bool  charging       = false;
};

/// Bring up STA + connect + run the WS client (with auto-reconnect).
void start();

/// Send a command word to the scale, e.g. "tare". Returns false if the
/// socket isn't up.
bool send_cmd(const char* cmd);

/// Latest decoded snapshot (copy, atomic). `seq` is bumped on every new
/// frame — compare against your last-seen value to detect freshness.
snapshot latest(std::uint32_t* seq = nullptr);

/// True while the WS is connected and frames are arriving.
bool online();

/// Sim-only hook: flip the stub's offline flag (host preview 'O' key).
/// No-op on target — never called there.
void sim_toggle_offline();

} // namespace net
