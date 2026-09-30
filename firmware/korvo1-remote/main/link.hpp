#pragma once
/// Link to the scale: BLE central (NimBLE) — passive scan for the service
/// UUID, connect, subscribe to state notifications.
///
/// The wire ABI is firmware/components/scale_proto/proto.hpp — the same
/// 20-byte state frame the web UI decodes, and the same command encoding
/// its buttons write.

#include <cstdint>

#include "scale_proto/proto.hpp"

namespace net {

/// One decoded snapshot frame — mirrors the proto::state fields.
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

/// Bring up the NimBLE host and start scanning (reconnects forever).
void start();

/// Send one proto command to the scale (write-without-response on the
/// command characteristic). Returns false while the link isn't ready.
bool send(const proto::command& cmd);

/// Latest decoded snapshot (copy, atomic). `seq` is bumped on every new
/// frame — compare against your last-seen value to detect freshness.
snapshot latest(std::uint32_t* seq = nullptr);

/// True while subscribed and frames are arriving (< 1.5 s old).
bool online();

/// Sim-only hook: flip the stub's offline flag (host preview 'O' key).
/// No-op on target — never called there.
void sim_toggle_offline();

} // namespace net
