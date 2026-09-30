/// web_server — SoftAP + static LittleFS + /ws push channel.
///
/// Statics: GET <path> tries /littlefs/<path>.gz first (shipped
/// Content-Encoding: gzip; browsers always accept it), then the plain file.
/// Unknown paths fall back to /index.html — SPA route AND captive-portal
/// probe (phones hitting /generate_204 etc. get the page popped at them).
///
/// The push task broadcasts one snapshot frame to every websocket client at
/// ~20 Hz; clients are enumerated statelessly via httpd_get_client_list.

#include "web_server.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

const char* kTag = "web";

// SoftAP credentials — local network only, change freely.
constexpr char     kSsid[] = "coffee-scale";
constexpr char     kPass[] = "coffeebrew";   // WPA2 needs >= 8 chars
constexpr char     kFsBase[] = "/littlefs";
constexpr char     kFsLabel[] = "littlefs";
constexpr unsigned kPushPeriodMs = 50;       // 20 Hz snapshot frames

web::source    s_src{};
httpd_handle_t s_httpd = nullptr;
bool           s_wifi_inited = false;
bool           s_fs_mounted  = false;

// ---- wifi --------------------------------------------------------------------

void wifi_softap() {
    if (!s_wifi_inited) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_ap();

        const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        s_wifi_inited = true;
    }

    wifi_config_t ap = {};
    std::strcpy(reinterpret_cast<char*>(ap.ap.ssid), kSsid);
    std::strcpy(reinterpret_cast<char*>(ap.ap.password), kPass);
    ap.ap.ssid_len       = 0;
    ap.ap.channel        = 1;
    ap.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap.ap.ssid_hidden    = 0;
    ap.ap.max_connection = 4;
    ap.ap.beacon_interval = 100;
    ap.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;

    // Logged, not ESP_ERROR_CHECK: a wifi bring-up failure after sleep/wake
    // must not panic the scale out of its restore path.
    if (const esp_err_t e = esp_wifi_set_mode(WIFI_MODE_AP); e != ESP_OK) {
        ESP_LOGE(kTag, "wifi set_mode: %s", esp_err_to_name(e));
        return;
    }
    if (const esp_err_t e = esp_wifi_set_config(WIFI_IF_AP, &ap); e != ESP_OK) {
        ESP_LOGE(kTag, "wifi set_config: %s", esp_err_to_name(e));
        return;
    }
    if (const esp_err_t e = esp_wifi_start(); e != ESP_OK) {
        ESP_LOGE(kTag, "wifi start: %s", esp_err_to_name(e));
        return;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);   // low-latency ws pushes over latency-free battery
    ESP_LOGI(kTag, "SoftAP \"%s\" (pass \"%s\") — http://192.168.4.1", kSsid, kPass);
}

// ---- filesystem ---------------------------------------------------------------

void mount_fs() {
    if (s_fs_mounted) {
        return;
    }
    const esp_vfs_littlefs_conf_t conf = {
        .base_path              = kFsBase,
        .partition_label        = kFsLabel,
        .partition              = nullptr,
        .blockdev               = nullptr,
        .format_if_mount_failed = true,
        .read_only              = false,
        .dont_mount             = false,
        .grow_on_mount          = false,
    };
    if (esp_vfs_littlefs_register(&conf) != ESP_OK) {
        ESP_LOGW(kTag, "littlefs mount failed — statics will 404");
        return;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(kFsLabel, &total, &used);
    ESP_LOGI(kTag, "littlefs mounted: %u/%u B used",
             static_cast<unsigned>(used), static_cast<unsigned>(total));
    s_fs_mounted = true;
}

// ---- statics ------------------------------------------------------------------

const char* content_type(const char* uri) {
    const char* dot = std::strrchr(uri, '.');
    if (!dot) return "application/octet-stream";
    ++dot;
    if (!strcmp(dot, "html")) return "text/html";
    if (!strcmp(dot, "js") || !strcmp(dot, "mjs")) return "text/javascript";
    if (!strcmp(dot, "css")) return "text/css";
    if (!strcmp(dot, "wasm")) return "application/wasm";
    if (!strcmp(dot, "json")) return "application/json";
    if (!strcmp(dot, "svg")) return "image/svg+xml";
    if (!strcmp(dot, "png")) return "image/png";
    if (!strcmp(dot, "ico")) return "image/x-icon";
    return "application/octet-stream";
}

FILE* open_asset(const char* uri, bool* gzipped) {
    char path[96];
    std::snprintf(path, sizeof(path), "%s%s.gz", kFsBase, uri);
    FILE* f = std::fopen(path, "rb");
    if (f) {
        *gzipped = true;
        return f;
    }
    std::snprintf(path, sizeof(path), "%s%s", kFsBase, uri);
    f = std::fopen(path, "rb");
    *gzipped = false;
    return f;
}

void send_file(httpd_req_t* req, FILE* f, const char* uri, bool gzipped) {
    httpd_resp_set_type(req, content_type(uri));
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (gzipped) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    char chunk[1024];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            break;
        }
    }
    httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t static_get(httpd_req_t* req) {
    char uri[64];
    std::strncpy(uri, req->uri, sizeof(uri) - 1);
    uri[sizeof(uri) - 1] = '\0';
    if (char* q = std::strchr(uri, '?')) {
        *q = '\0';   // strip query
    }
    if (uri[0] == '\0' || !strcmp(uri, "/")) {
        std::strcpy(uri, "/index.html");
    }
    if (std::strstr(uri, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
        return ESP_OK;
    }

    bool gzipped = false;
    FILE* f      = open_asset(uri, &gzipped);
    if (!f) {
        // SPA + captive-portal fallback: anything missing gets index.html.
        ESP_LOGD(kTag, "miss %s -> index.html", uri);
        std::strcpy(uri, "/index.html");
        f = open_asset(uri, &gzipped);
    }
    if (!f) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req,
            "<h1>coffee-scale</h1><p>web assets missing — populate "
            "<code>web/root</code> via web/pack.sh, then idf.py flash</p>");
        return ESP_OK;
    }
    send_file(req, f, uri, gzipped);
    std::fclose(f);
    return ESP_OK;
}

// ---- websocket ----------------------------------------------------------------

/// Pull the command word out of {"cmd":"..."} without dragging in a JSON lib.
size_t extract_cmd(const uint8_t* p, size_t n, char* out, size_t cap) {
    const char* end = reinterpret_cast<const char*>(p) + n;
    const char* k   = std::strstr(reinterpret_cast<const char*>(p), "\"cmd\"");
    if (!k) {
        return 0;
    }
    const char* q = std::strchr(k, ':');
    if (!q) {
        return 0;
    }
    q = std::strchr(q, '"');
    if (!q || q >= end) {
        return 0;
    }
    const char* e = std::strchr(q + 1, '"');
    if (!e || e > end) {
        return 0;
    }
    const size_t len = std::min<size_t>(e - q - 1, cap - 1);
    std::memcpy(out, q + 1, len);
    out[len] = '\0';
    return len;
}

esp_err_t ws_get(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        return ESP_OK;   // handshake — handled by httpd
    }
    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK || frame.len == 0 ||
        frame.len > 255 || frame.type != HTTPD_WS_TYPE_TEXT) {
        return ESP_OK;
    }
    std::uint8_t buf[256];
    frame.payload = buf;
    if (httpd_ws_recv_frame(req, &frame, sizeof(buf)) != ESP_OK) {
        return ESP_OK;
    }
    buf[frame.len] = '\0';   // recv gives exactly len bytes; strstr needs NUL
    char cmd[32];
    const size_t n = extract_cmd(buf, frame.len, cmd, sizeof(cmd));
    if (n && s_src.command) {
        s_src.command(s_src.ctx, cmd, n);
    }
    return ESP_OK;
}

void push_task(void*) {
    char  buf[224];
    int   fds[8];
    for (;;) {
        if (s_httpd && s_src.snapshot_json) {
            const size_t n = s_src.snapshot_json(s_src.ctx, buf, sizeof(buf));
            size_t       nfd = sizeof(fds) / sizeof(fds[0]);
            if (n > 0 &&
                httpd_get_client_list(s_httpd, &nfd, fds) == ESP_OK) {
                httpd_ws_frame_t f = {
                    .final      = true,
                    .fragmented = false,
                    .type       = HTTPD_WS_TYPE_TEXT,
                    .payload    = reinterpret_cast<std::uint8_t*>(buf),
                    .len        = n,
                };
                for (size_t i = 0; i < nfd; ++i) {
                    if (httpd_ws_get_fd_info(s_httpd, fds[i]) ==
                        HTTPD_WS_CLIENT_WEBSOCKET) {
                        httpd_ws_send_frame_async(s_httpd, fds[i], &f);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kPushPeriodMs));
    }
}

} // namespace

esp_err_t web::start(const source& src) {
    s_src = src;

    mount_fs();
    wifi_softap();

    httpd_config_t cfg     = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn       = httpd_uri_match_wildcard;
    cfg.lru_purge_enable   = true;   // dead sockets must not strand the ws slot
    cfg.stack_size         = 6144;   // fs reads + chunk sends
    cfg.max_uri_handlers   = 8;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed");
        return ESP_FAIL;
    }

    const httpd_uri_t ws_uri = {
        .uri                     = "/ws",
        .method                  = HTTP_GET,
        .handler                 = &ws_get,
        .user_ctx                = nullptr,
        .is_websocket            = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol   = nullptr,
    };
    const httpd_uri_t any_uri = {
        .uri                     = "/*",
        .method                  = HTTP_GET,
        .handler                 = &static_get,
        .user_ctx                = nullptr,
        .is_websocket            = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol   = nullptr,
    };
    httpd_register_uri_handler(s_httpd, &ws_uri);
    httpd_register_uri_handler(s_httpd, &any_uri);

    // One push task across sleep/wake cycles: it skips ticks while the
    // server handle is null.
    static bool push_started = false;
    if (!push_started) {
        xTaskCreate(push_task, "webpush", 3072, nullptr, 3, nullptr);
        push_started = true;
    }
    return ESP_OK;
}

void web::stop() {
    if (const httpd_handle_t h = s_httpd; h) {
        s_httpd = nullptr;   // push task stops broadcasting first
        httpd_stop(h);
    }
    esp_wifi_stop();
    ESP_LOGI(kTag, "web stopped");
}
