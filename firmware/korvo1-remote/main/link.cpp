#include "link.hpp"

#include <atomic>
#include <cstring>
#include <mutex>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "link";

constexpr char kSsid[] = "coffee-scale";
constexpr char kPass[] = "coffeebrew";
constexpr char kWsUri[] = "ws://192.168.4.1/ws";

net::snapshot s_snap;
std::mutex     s_mtx;
std::atomic<std::uint32_t> s_seq{0};
std::atomic<int64_t>       s_last_rx_ms{0};

esp_websocket_client_handle_t s_ws = nullptr;
std::atomic<bool> s_wifi_up{false};

void on_ws(void*, esp_event_base_t, int32_t id, void* data) {
    auto* ev = static_cast<esp_websocket_event_data_t*>(data);
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(kTag, "ws connected");
        break;
    case WEBSOCKET_EVENT_DATA: {
        if (ev->op_code != 0x01 || ev->data_len <= 0) break;   // text frames only
        // ws payload can arrive fragmented — keep it simple for now, the
        // scale's frames are ~200 B, well under a single transport chunk.
        const char* txt = static_cast<const char*>(ev->data_ptr)
                          + ev->payload_offset;
        cJSON* j = cJSON_ParseWithLength(txt, ev->payload_len);
        if (!j) break;
        auto num = [](const cJSON* o, const char* k, float d = 0) {
            const cJSON* i = cJSON_GetObjectItemCaseSensitive(o, k);
            return cJSON_IsNumber(i) ? static_cast<float>(i->valuedouble) : d;
        };
        auto boo = [](const cJSON* o, const char* k) {
            const cJSON* i = cJSON_GetObjectItemCaseSensitive(o, k);
            return cJSON_IsTrue(i);
        };
        const cJSON* s = cJSON_GetObjectItemCaseSensitive(j, "snap");
        {
            std::lock_guard lk(s_mtx);
            if (s) {
                s_snap.grams        = num(s, "grams");
                s_snap.flow_gps     = num(s, "flowGps");
                s_snap.stable       = boo(s, "stable");
                s_snap.tared        = boo(s, "tared");
                s_snap.calibrated   = boo(s, "calibrated");
                s_snap.unit         = static_cast<int>(num(s, "unit"));
                s_snap.mode         = static_cast<int>(num(s, "mode"));
                s_snap.timer_state  = static_cast<int>(num(s, "timerState"));
                s_snap.timer_ms     = static_cast<long>(num(s, "timerMs"));
                s_snap.pitch_deg    = num(s, "pitchDeg");
                s_snap.roll_deg     = num(s, "rollDeg");
            }
            s_snap.display_value = num(j, "displayValue");
            s_snap.battery_pct   = static_cast<int>(num(j, "batteryPct", -1));
            s_snap.charging      = boo(j, "charging");
        }
        cJSON_Delete(j);
        s_seq.fetch_add(1);
        s_last_rx_ms.store(esp_timer_get_time() / 1000);
        break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGW(kTag, "ws down — scale asleep or out of range");
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(kTag, "ws error");
        break;
    default: break;
    }
}

void on_wifi(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_up = false;
        ESP_LOGW(kTag, "wifi lost, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_up = true;
        ESP_LOGI(kTag, "got ip, opening ws");
        if (s_ws && !esp_websocket_client_is_connected(s_ws)) {
            esp_websocket_client_start(s_ws);
        }
    }
}

} // namespace

void net::start() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t sta = {};
    std::memcpy(sta.sta.ssid, kSsid, sizeof(kSsid) - 1);
    std::memcpy(sta.sta.password, kPass, sizeof(kPass) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_websocket_client_config_t wcfg = {};
    wcfg.uri                  = kWsUri;
    wcfg.reconnect_timeout_ms = 2000;
    wcfg.network_timeout_ms   = 4000;
    s_ws = esp_websocket_client_init(&wcfg);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, nullptr);
    // started on IP_EVENT_STA_GOT_IP
}

bool net::send_cmd(const char* cmd) {
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) return false;
    char buf[64];
    const int n = snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}", cmd);
    return esp_websocket_client_send_text(s_ws, buf, n,
                                          pdMS_TO_TICKS(500)) == n;
}

net::snapshot net::latest(std::uint32_t* seq) {
    std::lock_guard lk(s_mtx);
    if (seq) *seq = s_seq.load(std::memory_order_relaxed);
    return s_snap;
}

bool net::online() {
    const int64_t last = s_last_rx_ms.load();
    return s_wifi_up && s_ws && esp_websocket_client_is_connected(s_ws)
           && last > 0
           && (esp_timer_get_time() / 1000 - last) < 1500;
}
