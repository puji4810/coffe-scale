#pragma once

/// Self-hosted web UI: SoftAP WiFi + LittleFS statics + httpd + /ws channel.
///
/// Wire/ABI contract shared with the browser side (web/app.js, wasm modules):
///   server -> client   {"snap":{grams,flowGps,stable,tared,calibrated,unit,
///                      mode,timerState,timerMs,pitchDeg,rollDeg},
///                      "displayValue":..,"batteryPct":..,"charging":..}
///                      pushed at ~20 Hz to every ws client
///   client -> server   {"cmd":"tare"|"long"|"reset"|"mode"|"unit0"|"unit1"
///                      |"calzero"|"calspan:<g>"|"sleep"}

#include <cstddef>

#include "esp_err.h"

namespace web {

/// Bridge to the app model — supplied by main so this file stays free of
/// globals. Callbacks run on web-side tasks; the implementer owns locking.
struct source {
    void* ctx;
    /// Write one snapshot JSON frame into buf; return its length, 0 = skip
    /// this tick.
    size_t (*snapshot_json)(void* ctx, char* buf, size_t cap);
    /// One command word from a client (already unquoted, not NUL-terminated —
    /// use len).
    void (*command)(void* ctx, const char* cmd, size_t len);
};

/// Mount littlefs, bring the SoftAP up, start httpd + the push task.
/// Idempotent — safe to call again after stop() (sleep/wake cycles).
/// Never blocks app_main: a missing filesystem or failed wifi bring-up is
/// logged and degraded, not fatal (the scale works headless).
esp_err_t start(const source& src);

/// Tear down httpd + the SoftAP for low-power sleep; fs/netif/wifi-driver
/// init stays so start() comes back up fast.
void stop();

} // namespace web
