/// link.cpp — NimBLE central: scan → connect → discover → subscribe.
///
/// All GAP/GATT callbacks run on the NimBLE host task; `latest()`,
/// `online()` and `send()` may be called from the UI task — the small
/// shared state (conn handle, char handles, ready flag, snapshot) sits
/// behind s_mtx / atomics.
///
/// Discovery chain (all on the host task):
///   connect → disc_svc_by_uuid → disc_all_chrs [svc range]
///           → disc_all_dscs [state val → svc end, find CCCD 0x2902]
///           → write_flat {0x01,0x00} to the CCCD → ready
/// Any error mid-chain terminates the link; the disconnect path rescans.

#include "link.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"

namespace {

constexpr char kTag[] = "link";

net::snapshot s_snap;
std::mutex     s_mtx;
std::atomic<std::uint32_t> s_seq{0};
std::atomic<int64_t>       s_last_rx_ms{0};

std::atomic<uint16_t> s_conn{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint16_t> s_state_val{0};   // state char value handle
std::atomic<uint16_t> s_cmd_val{0};     // command char value handle
std::atomic<uint16_t> s_cccd{0};        // state CCCD handle
std::atomic<uint16_t> s_svc_start{0};   // scale service handle range
std::atomic<uint16_t> s_svc_end{0};
std::atomic<bool>     s_ready{false};   // subscribed — frames will flow
uint8_t s_own_addr_type = 0;

ble_uuid128_t s_uuid_svc{};
ble_uuid128_t s_uuid_state{};
ble_uuid128_t s_uuid_cmd{};

int gap_event(ble_gap_event* ev, void* arg);

// ---- scan -------------------------------------------------------------------

void start_scan() {
    const ble_gap_disc_params params = {
        .itvl                  = 0,
        .window                = 0,
        .filter_policy         = 0,
        .limited               = 0,
        .passive               = 1,
        .filter_duplicates     = 1,
        .disable_observer_mode = 0,
    };
    const int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params,
                                gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGW(kTag, "disc start: %d", rc);
    }
}

bool adv_is_scale(const ble_gap_disc_desc& disc) {
    ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, disc.data, disc.length_data) != 0) {
        return false;
    }
    for (int i = 0; i < f.num_uuids128; ++i) {
        if (ble_uuid_cmp(&f.uuids128[i].u, &s_uuid_svc.u) == 0) {
            return true;
        }
    }
    return false;
}

// ---- gatt discovery chain ----------------------------------------------------

void drop() {   // terminate + let the DISCONNECT handler rescan
    const uint16_t h = s_conn.load();
    if (h != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(h, BLE_ERR_REM_USER_CONN_TERM);
    }
}

int write_cb(uint16_t conn, const ble_gatt_error* err, ble_gatt_attr*,
             void*) {
    if (err->status == 0) {
        s_ready = true;
        ESP_LOGI(kTag, "conn %u subscribed — streaming", conn);
    } else {
        ESP_LOGW(kTag, "cccd write failed: %d", err->status);
        drop();
    }
    return 0;
}

int dsc_cb(uint16_t conn, const ble_gatt_error* err, uint16_t chr_val,
           const ble_gatt_dsc* dsc, void*) {
    if (err->status == 0 && dsc) {
        // descriptor of the state characteristic? CCCD uuid16 = 0x2902
        if (chr_val == s_state_val.load() &&
            dsc->uuid.u.type == BLE_UUID_TYPE_16 &&
            dsc->uuid.u16.value == BLE_GATT_DSC_CLT_CFG_UUID16) {
            s_cccd = dsc->handle;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE) {
        ESP_LOGW(kTag, "dsc disc failed: %d", err->status);
        drop();
        return 0;
    }
    if (s_cccd == 0) {
        ESP_LOGW(kTag, "no state CCCD found");
        drop();
        return 0;
    }
    const std::array<std::uint8_t, 2> sub = {0x01, 0x00};  // notify on
    if (ble_gattc_write_flat(conn, s_cccd, sub.data(), sub.size(),
                             write_cb, nullptr) != 0) {
        ESP_LOGW(kTag, "cccd write call failed");
        drop();
    }
    return 0;
}

int chr_cb(uint16_t conn, const ble_gatt_error* err,
           const ble_gatt_chr* chr, void*) {
    if (err->status == 0 && chr) {
        if (ble_uuid_cmp(&chr->uuid.u, &s_uuid_state.u) == 0) {
            s_state_val = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &s_uuid_cmd.u) == 0) {
            s_cmd_val = chr->val_handle;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE || s_state_val == 0 || s_cmd_val == 0) {
        ESP_LOGW(kTag, "chr disc failed/missing: %d state=%u cmd=%u",
                 err->status, s_state_val.load(), s_cmd_val.load());
        drop();
        return 0;
    }
    // Find the state char's CCCD — descriptors live between the char's
    // value handle and the service end.
    if (ble_gattc_disc_all_dscs(conn, s_state_val, s_svc_end,
                                dsc_cb, nullptr) != 0) {
        ESP_LOGW(kTag, "dsc disc call failed");
        drop();
    }
    return 0;
}

int svc_cb(uint16_t conn, const ble_gatt_error* err,
           const ble_gatt_svc* svc, void*) {
    if (err->status == 0 && svc) {
        s_svc_start = svc->start_handle;
        s_svc_end   = svc->end_handle;
        return 0;
    }
    if (err->status != BLE_HS_EDONE || s_svc_end == 0) {
        ESP_LOGW(kTag, "svc disc failed: %d", err->status);
        drop();
        return 0;
    }
    if (ble_gattc_disc_all_chrs(conn, s_svc_start, s_svc_end,
                                chr_cb, nullptr) != 0) {
        ESP_LOGW(kTag, "chr disc call failed");
        drop();
    }
    return 0;
}

// ---- gap events ----------------------------------------------------------------

int gap_event(ble_gap_event* ev, void*) {
    switch (ev->type) {
        case BLE_GAP_EVENT_DISC:
            if (adv_is_scale(ev->disc)) {
                ESP_LOGI(kTag, "scale found — connecting");
                ble_gap_disc_cancel();
                const ble_gap_conn_params p = {
                    .scan_itvl           = 0x0010,
                    .scan_window         = 0x0010,
                    .itvl_min            = 24,     // 30 ms
                    .itvl_max            = 40,     // 50 ms
                    .latency             = 0,
                    .supervision_timeout = 400,    // 4 s
                    .min_ce_len          = 0,
                    .max_ce_len          = 0,
                };
                if (ble_gap_connect(s_own_addr_type, &ev->disc.addr, 5000,
                                    &p, gap_event, nullptr) != 0) {
                    ESP_LOGW(kTag, "connect call failed — rescan");
                    start_scan();
                }
            }
            break;
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status == 0) {
                s_conn = ev->connect.conn_handle;
                ESP_LOGI(kTag, "conn %u up — discovering",
                         ev->connect.conn_handle);
                if (ble_gattc_disc_svc_by_uuid(s_conn, &s_uuid_svc.u,
                                               svc_cb, nullptr) != 0) {
                    ESP_LOGW(kTag, "svc disc call failed");
                    drop();
                }
            } else {
                ESP_LOGW(kTag, "connect failed: %d — rescanning",
                         ev->connect.status);
                start_scan();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(kTag, "conn %u down (reason %d) — rescanning",
                     ev->disconnect.conn.conn_handle,
                     ev->disconnect.reason);
            s_conn      = BLE_HS_CONN_HANDLE_NONE;
            s_state_val = 0;
            s_cmd_val   = 0;
            s_cccd      = 0;
            s_svc_start = 0;
            s_svc_end   = 0;
            s_ready     = false;
            start_scan();
            break;
        case BLE_GAP_EVENT_NOTIFY_RX: {
            if (ev->notify_rx.attr_handle != s_state_val.load()) {
                break;
            }
            std::array<std::uint8_t, 32> buf{};
            std::uint16_t n = 0;
            if (ble_hs_mbuf_to_flat(ev->notify_rx.om, buf.data(),
                                    buf.size(), &n) != 0) {
                break;
            }
            const auto s = proto::decode(
                std::span<const std::uint8_t>{buf.data(), n});
            if (!s) {
                break;
            }
            {
                std::lock_guard lk(s_mtx);
                s_snap.grams         = s->grams;
                s_snap.flow_gps      = s->flow_gps;
                s_snap.display_value = s->display_value;
                s_snap.pitch_deg     = s->pitch_deg;
                s_snap.roll_deg      = s->roll_deg;
                s_snap.timer_ms      = static_cast<long>(s->timer_ms);
                s_snap.battery_pct   = s->battery_pct;
                s_snap.unit          = s->unit;
                s_snap.mode          = s->mode;
                s_snap.timer_state   = s->timer_state;
                s_snap.stable        = s->stable;
                s_snap.tared         = s->tared;
                s_snap.calibrated    = s->calibrated;
                s_snap.charging      = s->charging;
            }
            s_seq.fetch_add(1);
            s_last_rx_ms.store(esp_timer_get_time() / 1000);
            break;
        }
        default:
            break;
    }
    return 0;
}

// ---- host bring-up ------------------------------------------------------------

void on_sync() {
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGE(kTag, "no usable device address");
        return;
    }
    start_scan();
}

void on_reset(int reason) {
    ESP_LOGW(kTag, "host reset: %d", reason);
    s_ready = false;
    s_conn  = BLE_HS_CONN_HANDLE_NONE;
}

void host_task(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

} // namespace

void net::start() {
    if (const esp_err_t e = nimble_port_init(); e != ESP_OK) {
        ESP_LOGE(kTag, "nimble_port_init: %s", esp_err_to_name(e));
        return;
    }
    s_uuid_svc.u.type = BLE_UUID_TYPE_128;
    std::memcpy(s_uuid_svc.value, proto::kServiceUuid128.data(), 16);
    s_uuid_state.u.type = BLE_UUID_TYPE_128;
    std::memcpy(s_uuid_state.value, proto::kStateUuid128.data(), 16);
    s_uuid_cmd.u.type = BLE_UUID_TYPE_128;
    std::memcpy(s_uuid_cmd.value, proto::kCommandUuid128.data(), 16);

    ble_hs_cfg.reset_cb = &on_reset;
    ble_hs_cfg.sync_cb  = &on_sync;
    nimble_port_freertos_init(&host_task);
    ESP_LOGI(kTag, "scanning for the scale");
}

bool net::send(const proto::command& cmd) {
    const uint16_t conn = s_conn.load();
    if (!s_ready || conn == BLE_HS_CONN_HANDLE_NONE || s_cmd_val == 0) {
        return false;
    }
    const auto f = proto::encode_command(cmd);
    const int rc = ble_gattc_write_no_rsp_flat(conn, s_cmd_val.load(),
                                               f.bytes.data(), f.size);
    return rc == 0;
}

net::snapshot net::latest(std::uint32_t* seq) {
    std::lock_guard lk(s_mtx);
    if (seq) *seq = s_seq.load(std::memory_order_relaxed);
    return s_snap;
}

bool net::online() {
    const int64_t last = s_last_rx_ms.load();
    return s_ready && last > 0
           && (esp_timer_get_time() / 1000 - last) < 1500;
}
