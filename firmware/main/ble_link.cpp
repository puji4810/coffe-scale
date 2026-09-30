/// ble_link — NimBLE peripheral: GAP advertising + GATT state/command
/// service + a 20 Hz notify push task.
///
/// Advertising keeps running while a connection slot is free (a browser
/// and the Korvo remote can both stay attached): flags + the 128-bit
/// service UUID in the adv packet, the complete name in the scan
/// response. 100 ms interval for the first 30 s after every start or
/// disconnect (fast phone pickup), 500 ms thereafter.
///
/// Sleep teardown is a full nimble_port_deinit() — verified against IDF
/// 6.0.2 sources: nimble_port_init()/nimble_port_deinit() are designed as
/// a cycle (ble_npl_reset_deinit_flag() "called at start of new init
/// cycle", esp_nimble_hci_init/deinit have no one-shot guards, and
/// esp_bt_controller_mem_release is only invoked on the original ESP32,
/// never on S3), so init-after-deinit is supported and the controller is
/// genuinely off during light sleep.

#include "ble_link.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "scale_proto/proto.hpp"

#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace {

const char* kTag = "ble";

constexpr char     kDeviceName[]  = "coffee-scale";
constexpr unsigned kPushPeriodMs  = 50;      // 20 Hz state frames
constexpr uint16_t kAdvFastItvlMs = 100;     // right after start/disconnect
constexpr uint16_t kAdvSlowItvlMs = 500;     // steady state
constexpr int64_t  kFastAdvWindowMs = 30'000;
constexpr int      kMaxConns = 3;            // CONFIG_BT_NIMBLE_MAX_CONNECTIONS
static_assert(kMaxConns == CONFIG_BT_NIMBLE_MAX_CONNECTIONS);

ble::source        s_src{};
std::atomic<bool>  s_running{false};
uint8_t            s_own_addr_type = 0;
uint16_t      s_state_val_handle = 0;
std::uint8_t  s_seq = 0;

/// Latest encoded frame for the read characteristic — the push task
/// refreshes it; reads between notifies stay cheap.
std::array<std::uint8_t, proto::kStateLen> s_frame{};

/// Connected handles + per-conn state-char subscription state. GAP events
/// run on the NimBLE host task, the push task reads them — a spinlock keeps
/// the small array consistent.
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
uint16_t     s_conns[kMaxConns];      // BLE_HS_CONN_HANDLE_NONE = free
bool         s_subs[kMaxConns];

int64_t      s_fast_adv_until_ms = 0; // fast-interval deadline (esp_timer)
bool         s_adv_is_fast = false;   // current adv cadence (set by advertise)

/// Serializes push-task ticks against stack teardown: stop()/start() hold
/// it while the host/controller go down or come up, so the push task can
/// never call into a half-dead stack.
StaticSemaphore_t s_mtx_buf;
SemaphoreHandle_t s_mtx = xSemaphoreCreateMutexStatic(&s_mtx_buf);

int gap_event(ble_gap_event* ev, void* arg);

// ---- connection tracking ------------------------------------------------------

void conn_add(std::uint16_t h) {
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < kMaxConns; ++i) {
        if (s_conns[i] == BLE_HS_CONN_HANDLE_NONE) {
            s_conns[i] = h;
            s_subs[i]  = false;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

void conn_del(std::uint16_t h) {
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < kMaxConns; ++i) {
        if (s_conns[i] == h) {
            s_conns[i] = BLE_HS_CONN_HANDLE_NONE;
            s_subs[i]  = false;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

void conn_subscribe(std::uint16_t h, bool on) {
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < kMaxConns; ++i) {
        if (s_conns[i] == h) {
            s_subs[i] = on;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

int conn_count() {
    int n = 0;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < kMaxConns; ++i) {
        n += s_conns[i] != BLE_HS_CONN_HANDLE_NONE;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

// ---- advertising ----------------------------------------------------------------

void advertise() {
    if (!s_running || conn_count() >= kMaxConns) {
        return;
    }
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }
    const bool fast = esp_timer_get_time() / 1000 < s_fast_adv_until_ms;
    const uint16_t itvl = static_cast<uint16_t>(
        (fast ? kAdvFastItvlMs : kAdvSlowItvlMs) * 8 / 5);  // 0.625 ms units
    const ble_gap_adv_params params = {
        .conn_mode          = BLE_GAP_CONN_MODE_UND,
        .disc_mode          = BLE_GAP_DISC_MODE_GEN,
        .itvl_min           = itvl,
        .itvl_max           = itvl,
        .channel_map        = 0,
        .filter_policy      = 0,
        .high_duty_cycle    = 0,
    };
    const int rc = ble_gap_adv_start(s_own_addr_type, nullptr,
                                     BLE_HS_FOREVER, &params,
                                     gap_event, nullptr);
    s_adv_is_fast = fast;
    if (rc != 0) {
        ESP_LOGW(kTag, "adv_start: %d", rc);
    }
}

// ---- gap events -----------------------------------------------------------------

int gap_event(ble_gap_event* ev, void*) {
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status == 0) {
                conn_add(ev->connect.conn_handle);
                if (s_src.activity) {
                    s_src.activity(s_src.ctx);
                }
                // 30-50 ms interval, no latency skip, 4 s supervision.
                const ble_gap_upd_params p = {
                    .itvl_min            = 24,
                    .itvl_max            = 40,
                    .latency             = 0,
                    .supervision_timeout = 400,   // x10 ms
                    .min_ce_len          = 0,
                    .max_ce_len          = 0,
                };
                ble_gap_update_params(ev->connect.conn_handle, &p);
                ESP_LOGI(kTag, "conn %u up (%d live)",
                         ev->connect.conn_handle, conn_count());
            }
            // Advertising stopped on connect (or never ran) — restart while
            // a slot is free. Also covers a failed connect.
            advertise();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            conn_del(ev->disconnect.conn.conn_handle);
            s_fast_adv_until_ms =
                esp_timer_get_time() / 1000 + kFastAdvWindowMs;
            ESP_LOGI(kTag, "conn %u down (reason %d)",
                     ev->disconnect.conn.conn_handle,
                     ev->disconnect.reason);
            advertise();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();   // duration reached / terminated — keep going
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (ev->subscribe.attr_handle == s_state_val_handle) {
                conn_subscribe(ev->subscribe.conn_handle,
                               ev->subscribe.cur_notify != 0);
                if (s_src.activity) {
                    s_src.activity(s_src.ctx);
                }
                ESP_LOGI(kTag, "conn %u notify=%d",
                         ev->subscribe.conn_handle, ev->subscribe.cur_notify);
            }
            break;
        default:
            break;
    }
    return 0;
}

// ---- gatt ----------------------------------------------------------------------

ble_uuid128_t s_uuid_svc{};
ble_uuid128_t s_uuid_state{};
ble_uuid128_t s_uuid_cmd{};

void fill_uuid(ble_uuid128_t& u,
               const std::array<std::uint8_t, 16>& bytes) {
    u.u.type = BLE_UUID_TYPE_128;
    std::memcpy(u.value, bytes.data(), 16);
}

int state_access(std::uint16_t, std::uint16_t, ble_gatt_access_ctxt* ctxt,
                 void*) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    std::array<std::uint8_t, proto::kStateLen> f;
    portENTER_CRITICAL(&s_mux);
    f = s_frame;
    portEXIT_CRITICAL(&s_mux);
    if (os_mbuf_append(ctxt->om, f.data(), f.size()) != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

int command_access(std::uint16_t, std::uint16_t, ble_gatt_access_ctxt* ctxt,
                   void*) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    std::array<std::uint8_t, 8> buf{};
    std::uint16_t               n = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf.data(), buf.size(), &n) != 0 ||
        n == 0 || n > 5) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    const auto cmd = proto::decode_command(
        std::span<const std::uint8_t>{buf.data(), n});
    if (!cmd) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (s_src.activity) {
        s_src.activity(s_src.ctx);
    }
    if (s_src.command) {
        s_src.command(s_src.ctx, *cmd);
    }
    return 0;
}

const ble_gatt_chr_def s_chrs[] = {
    {
        .uuid        = &s_uuid_state.u,
        .access_cb   = &state_access,
        .arg         = nullptr,
        .descriptors = nullptr,
        .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle  = &s_state_val_handle,
        .cpfd        = nullptr,
    },
    {
        .uuid        = &s_uuid_cmd.u,
        .access_cb   = &command_access,
        .arg         = nullptr,
        .descriptors = nullptr,
        .flags       = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle  = nullptr,
        .cpfd        = nullptr,
    },
    {},
};

const ble_gatt_svc_def s_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &s_uuid_svc.u,
        .includes        = nullptr,
        .characteristics = s_chrs,
    },
    {},
};

// ---- host bring-up / teardown ----------------------------------------------------

void on_sync() {
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGE(kTag, "no usable device address");
        return;
    }
    // Adv packet: flags + service UUID; complete name in the scan
    // response. These are HCI writes — only legal post-sync on the host
    // task (pre-sync calls return ENOTSYNCED).
    ble_hs_adv_fields adv{};
    adv.flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128             = &s_uuid_svc;
    adv.num_uuids128         = 1;
    adv.uuids128_is_complete = 1;
    ble_hs_adv_fields rsp{};
    rsp.name             = reinterpret_cast<const uint8_t*>(kDeviceName);
    rsp.name_len         = sizeof(kDeviceName) - 1;
    rsp.name_is_complete = 1;
    if (ble_gap_adv_set_fields(&adv) != 0 ||
        ble_gap_adv_rsp_set_fields(&rsp) != 0) {
        ESP_LOGE(kTag, "adv fields failed");
        return;
    }
    advertise();
}

void on_reset(int reason) {
    ESP_LOGW(kTag, "host reset: %d", reason);
}

void host_task(void*) {
    nimble_port_run();               // returns once nimble_port_stop() unwinds
    nimble_port_freertos_deinit();   // deletes this task
}

void push_task(void*) {
    for (;;) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        if (s_running && s_src.snapshot) {
            proto::state s = s_src.snapshot(s_src.ctx);
            s.seq = s_seq++;
            const auto f = proto::encode(s);
            portENTER_CRITICAL(&s_mux);
            s_frame = f;
            const uint16_t conns[kMaxConns] = {
                s_conns[0], s_conns[1], s_conns[2]};
            const bool subs[kMaxConns] = {s_subs[0], s_subs[1], s_subs[2]};
            portEXIT_CRITICAL(&s_mux);
            for (int i = 0; i < kMaxConns; ++i) {
                if (!subs[i] || conns[i] == BLE_HS_CONN_HANDLE_NONE) {
                    continue;
                }
                // ble_gatts_notify_custom consumes the mbuf on success AND
                // failure — fresh allocation per connection, never freed
                // here.
                os_mbuf* om = ble_hs_mbuf_from_flat(f.data(), f.size());
                if (!om) {
                    break;
                }
                ble_gatts_notify_custom(conns[i], s_state_val_handle, om);
            }
            // Fast adv window elapsed -> drop to the slow interval once.
            if (s_adv_is_fast && ble_gap_adv_active() &&
                esp_timer_get_time() / 1000 >= s_fast_adv_until_ms) {
                advertise();
            }
        }
        xSemaphoreGive(s_mtx);
        vTaskDelay(pdMS_TO_TICKS(kPushPeriodMs));
    }
}

} // namespace

esp_err_t ble::start(const source& src) {
    s_src = src;
    if (s_running) {
        return ESP_OK;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    // Set before the host task is spawned so on_sync->advertise() can't
    // lose the race to it; push_task is blocked on s_mtx throughout.
    s_running = true;
    for (int i = 0; i < kMaxConns; ++i) {
        s_conns[i] = BLE_HS_CONN_HANDLE_NONE;
        s_subs[i]  = false;
    }

    if (const esp_err_t e = nimble_port_init(); e != ESP_OK) {
        ESP_LOGE(kTag, "nimble_port_init: %s", esp_err_to_name(e));
        s_running = false;
        xSemaphoreGive(s_mtx);
        return e;
    }
    fill_uuid(s_uuid_svc,   proto::kServiceUuid128);
    fill_uuid(s_uuid_state, proto::kStateUuid128);
    fill_uuid(s_uuid_cmd,   proto::kCommandUuid128);

    ble_hs_cfg.reset_cb = &on_reset;
    ble_hs_cfg.sync_cb  = &on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(kDeviceName);
    ble_gatts_count_cfg(s_svcs);
    if (ble_gatts_add_svcs(s_svcs) != 0) {
        ESP_LOGE(kTag, "gatts add_svcs failed");
        s_running = false;
        nimble_port_deinit();
        xSemaphoreGive(s_mtx);
        return ESP_FAIL;
    }

    // Set before the host task spawns: on_sync may run before the next
    // line executes, and it needs the fast-adv window already armed.
    s_fast_adv_until_ms = esp_timer_get_time() / 1000 + kFastAdvWindowMs;
    nimble_port_freertos_init(&host_task);
    xSemaphoreGive(s_mtx);
    ESP_LOGI(kTag, "BLE peripheral \"%s\" up", kDeviceName);

    // One push task across sleep/wake cycles: it skips ticks while
    // s_running is false.
    static bool push_started = false;
    if (!push_started) {
        xTaskCreate(push_task, "blepush", 3072, nullptr, 3, nullptr);
        push_started = true;
    }
    return ESP_OK;
}

void ble::stop() {
    if (!s_running) {
        return;
    }
    s_running = false;   // push task stops notifying first
    // Block until an in-flight push tick leaves the stack before tearing
    // it down; the DISCONNECT handlers spawned by terminate() run on the
    // host task and see s_running=false, so they can't re-arm advertising.
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    ble_gap_adv_stop();
    for (int i = 0; i < kMaxConns; ++i) {
        if (s_conns[i] != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(s_conns[i], BLE_ERR_REM_USER_CONN_TERM);
        }
        s_conns[i] = BLE_HS_CONN_HANDLE_NONE;
        s_subs[i]  = false;
    }
    // Stop the host (unwinds nimble_port_run on the host task, which then
    // deletes itself via nimble_port_freertos_deinit), let it settle, then
    // power the controller off entirely.
    nimble_port_stop();
    vTaskDelay(pdMS_TO_TICKS(50));
    nimble_port_deinit();
    xSemaphoreGive(s_mtx);
    ESP_LOGI(kTag, "ble stopped");
}
