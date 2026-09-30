#pragma once

/// BLE link — NimBLE peripheral replacing the old SoftAP + /ws channel.
///
/// Wire/ABI contract is scale_proto/proto.hpp (single source shared with
/// the web WASM bindings and the Korvo remote):
///   server -> client   20-byte state frame, read + notify @ ~20 Hz to
///                      every subscribed connection (up to
///                      CONFIG_BT_NIMBLE_MAX_CONNECTIONS centrals)
///   client -> server   1..5-byte command frame (proto::decode_command)
///
/// Open link by design: no pairing/bonding (CONFIG_BT_NIMBLE_SECURITY_ENABLE=n).

#include <cstddef>

#include "esp_err.h"

namespace proto {
struct state;
struct command;
} // namespace proto

namespace ble {

/// Bridge to the app model — supplied by main so this file stays free of
/// globals. Callbacks run on BLE-side tasks; the implementer owns locking.
struct source {
    void* ctx;
    /// Latest scale state to encode into a state frame.
    proto::state (*snapshot)(void* ctx);
    /// One decoded command from a central.
    void (*command)(void* ctx, const proto::command& cmd);
    /// Any link-level activity worth feeding the idle timer (connects,
    /// writes, subscriptions).
    void (*activity)(void* ctx);
};

/// Bring the controller + NimBLE host up, register the service, start
/// advertising (fast interval for the first 30 s) and the push task.
/// Idempotent — safe to call again after stop() (sleep/wake cycles).
/// Never fatal to app_main: a bring-up failure is logged and degraded.
esp_err_t start(const source& src);

/// Full teardown for low-power sleep: stop advertising, terminate every
/// connection, then nimble_port_stop() + nimble_port_deinit() — the
/// controller is powered off entirely. start() re-inits from scratch
/// (NimBLE's init/deinit cycle is designed for this: the STATIC_TO_DYNAMIC
/// path re-allocates its context each init).
void stop();

} // namespace ble
