/// app_main — scale-adc-s3 bring-up firmware.
///
/// Threads:
///   adc_task     waits on NAU7802 DRDY (IO12), reads the conversion, feeds
///                the app model; prints telemetry CSV when enabled
///   accel_task   polls LIS2DW12 at 100 Hz; feed_accel/motion decimated
///                to 20 Hz; raw 'A' lines go out on every sample
///   button_task  debounces TARE (IO48) / MODE (IO18) -> app model
///   battery_task samples VBAT on ADC1_CH8 + charge status + TMP102, ~2 s
///   console_task USB-serial commands: 't' = CSV telemetry, 'z' = tare,
///                'r' = raw capture stream (full-rate W/A lines + events —
///                the input format for tools/replay)
///   power_task   tracks "new activity" (weight steps, motion, buttons,
///                taps, BLE link events); after kIdleTimeoutMs of quiet it
///                powers down LCD/backlight/BLE/NAU7802, arms LIS2DW12
///                double-tap->INT1 as the wake source and enters deep
///                sleep (light-sleep fallback is configurable).
///                Double-tap (or MODE button) wakes everything
///                back up. btn_tare (IO48) is not an RTC pin and cannot
///                wake.
///   lvgl_task    spawned by ui::port_init(); renders + pulls ui::model via
///                the refresh hook (all LVGL calls stay on its thread)
///
/// Bring-up per pcb/scale-adc-s3/README.md: missing sensors log and boot
/// continues so the board can be brought up peripheral by peripheral.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <optional>
#include <type_traits>
#include <sys/time.h>

#include "board/pins.hpp"
#include "bus/i2c_esp.hpp"
#include "lis2dw12/lis2dw12.hpp"
#include "nau7802/nau7802.hpp"
#include "scale/app.hpp"
#include "scale/button_press.hpp"
#include "scale/calibration.hpp"
#include "scale/telemetry.hpp"
#include "tmp102/tmp102.hpp"
#include "ui/ui.hpp"
#include "ble_link.hpp"
#include "scale_proto/proto.hpp"

#include <fcntl.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/temperature_sensor.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {

const char* kTag = "scale";

std::mutex      g_mtx;
// Serializes sensor access across shutdown/restore. In particular, the
// accel poller must not clear the latched wake event while we arm sleep.
std::mutex      g_sensor_mtx;
std::atomic<bool> g_peripherals_paused{false};
scale::app      g_app;
std::atomic<int>  g_battery_pct{-1};
std::atomic<int>  g_battery_mv{0};
std::atomic<bool> g_charging{false};
std::atomic<bool> g_telemetry{false};
std::atomic<bool> g_raw{false};   // 'r' console key: raw capture stream

/// Raw-capture event line `E,<t_us>,<name>` — emitted from every command
/// dispatch site (buttons, console, BLE) so a host replay sees the same
/// sequence of model calls the live firmware made.
void raw_event(const char* name) {
    if (!g_raw.load(std::memory_order_relaxed)) {
        return;
    }
    char line[64];
    snprintf(line, sizeof(line), "E,%lld,%s\n",
             static_cast<long long>(esp_timer_get_time()), name);
    fputs(line, stdout);
}

// ---- activity / power management ----------------------------------------------
// "New activity" is deliberately noise-aware: the idle timer is refreshed
// only by real events — a weight step past the raw-count threshold, IMU
// motion, a button, a wake tap, a console or web command — never by the
// continuous jitter of the load-cell signal itself.
std::atomic<int64_t> g_last_activity_ms{0};
std::atomic<bool>    g_sleep_request{false};

constexpr int64_t kIdleTimeoutMs       = 300'000;   // 5 min
constexpr float   kActivityCountsThresh = 20000.0f; // raw-count step
constexpr float   kMotionThreshMg      = 60.0f;     // per-sample accel delta

struct standby_record {
    std::uint32_t magic;
    scale::app::standby_state app;
    std::int64_t entered_wall_us;
};
static_assert(std::is_trivially_copyable_v<standby_record>);
RTC_DATA_ATTR standby_record s_standby{};
constexpr std::uint32_t kStandbyMagic = 0x534C5001;
std::int64_t g_awake_since_ms = 0;
std::atomic<std::int64_t> g_last_sleep_ms{0};
std::atomic<std::uint32_t> g_last_wake{0};

const char* wake_name(std::uint32_t causes) {
    const bool mode = (causes & (1U << ESP_SLEEP_WAKEUP_EXT0)) != 0;
    const bool tap = (causes & (1U << ESP_SLEEP_WAKEUP_EXT1)) != 0;
    if (mode && tap) return "MODE + double-tap";
    if (mode) return "MODE";
    if (tap) return "double-tap";
    if (causes & (1U << ESP_SLEEP_WAKEUP_GPIO)) return "GPIO (see MODE/INT1 log)";
    if (!causes || causes == (1U << ESP_SLEEP_WAKEUP_UNDEFINED)) return "cold boot / rejected sleep";
    return "other";
}

std::int64_t wall_us() {
    timeval tv{};
    gettimeofday(&tv, nullptr); // RTC-backed system time survives deep sleep
    return static_cast<std::int64_t>(tv.tv_sec) * 1'000'000 + tv.tv_usec;
}

void mark_activity() {
    g_last_activity_ms.store(esp_timer_get_time() / 1000,
                             std::memory_order_relaxed);
}

// Defined in the BLE-bridge section below; power management restarts the
// link with the same source after wake.
proto::state ble_snapshot(void*);
void         ble_command(void*, const proto::command& cmd);

TaskHandle_t  g_adc_task   = nullptr;
TaskHandle_t  g_accel_task = nullptr;
QueueHandle_t g_btn_q      = nullptr;
// Protected by g_mtx together with the model; sleep/wake drops old presses.
scale::button_press g_tare_button, g_mode_button;

// Devices live for the whole run; keep them in static storage.
std::optional<bus::i2c_dev_esp>                   s_adc_dev, s_acc_dev, s_tmp_dev;
std::optional<drv::nau7802<bus::i2c_dev_esp>>     s_adc;
std::optional<drv::lis2dw12<bus::i2c_dev_esp>>    s_acc;
std::optional<drv::tmp102<bus::i2c_dev_esp>>      s_tmp;

void delay_ms(std::uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// ---- calibration persistence ------------------------------------------------

bool load_calibration(scale::calibration& c) {
    nvs_handle_t h;
    if (nvs_open("scale", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len   = sizeof(c);
    const bool ok = nvs_get_blob(h, "cal", &c, &len) == ESP_OK && len == sizeof(c);
    nvs_close(h);
    return ok;
}

void save_calibration(const scale::calibration& c) {
    nvs_handle_t h;
    if (nvs_open("scale", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "cal", &c, sizeof(c));
    nvs_commit(h);
    nvs_close(h);
}

// ---- interrupts --------------------------------------------------------------

// Low word of the DRDY edge time (us) — written in the ISR so the
// conversion-complete instant survives task scheduling jitter. The adc
// task unwraps it against the current time; edges are ~12.5 ms apart so
// the wrap ambiguity never spans two samples.
volatile std::uint32_t g_drdy_lo = 0;

void IRAM_ATTR drdy_isr(void*) {
    g_drdy_lo = static_cast<std::uint32_t>(esp_timer_get_time());
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(g_adc_task, &hp);
    portYIELD_FROM_ISR(hp);
}

void IRAM_ATTR btn_isr(void* arg) {
    int pin = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    xQueueSendFromISR(g_btn_q, &pin, nullptr);
}

// LIS2DW12 INT1: tap/motion events (latched level) — flags to accel_task,
// which clears the source regs over i2c and marks activity.
void IRAM_ATTR int1_isr(void*) {
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(g_accel_task, &hp);
    portYIELD_FROM_ISR(hp);
}

// ---- tasks -------------------------------------------------------------------

void adc_task(void*) {
    scale::diag d;
    int           no_edge_streak = 0, err_streak = 0;
    float         activity_base  = 0.0f;   // slow count baseline for activity
    bool          base_init      = false;
    // sampling health: coalesced/lost edges, i2c + feed cost, interval max
    std::uint32_t n_samp = 0, n_coalesced = 0, n_late = 0;
    std::int64_t  i2c_sum = 0, feed_sum = 0, prev_us = 0;
    std::int64_t  i2c_max = 0, feed_max = 0, gap_max = 0;
    for (;;) {
        // Primary path: one notification per DRDY rising edge. ~2 sample
        // periods is generous — a longer silence means a lost edge (or a
        // stalled task), so fall back to polling the CR bit over i2c.
        const int pending = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30));
        if (g_peripherals_paused.load()) continue;
        std::unique_lock sensor_lk(g_sensor_mtx);
        if (g_peripherals_paused.load()) continue;
        if (!s_adc) continue;
        std::int64_t t_us = 0;
        if (pending > 0) {
            no_edge_streak = 0;
            if (pending > 1) n_coalesced += pending - 1;  // late task
            // Conversion-complete instant from the ISR, unwrapped
            // against now. The low word MUST be read before `now`: an
            // edge landing between the two reads writes a low word
            // newer than `now`, and wrapping it backwards then lands
            // the timestamp ~71.6 min in the past. Read first, and any
            // race only makes the stamp older by one edge — never the
            // future, never backwards by minutes.
            const std::uint32_t lo = g_drdy_lo;
            const std::int64_t  now = esp_timer_get_time();
            t_us = (now & ~0xffffffffLL) | lo;
            if (t_us > now) t_us -= 1LL << 32;
        } else {
            ++n_late;
            if (++no_edge_streak == 8) {
                ESP_LOGW(kTag, "adc: no DRDY edges on IO12 — polling CR bit");
            }
        }
        // The notification is only a wake hint: NAU7802 latches the
        // previous conversion on a premature read, so every path
        // verifies CR before reading — a spurious/stale wake can then
        // never produce a duplicated sample.
        const auto rdy = s_adc->data_ready();
        if (!rdy) {
            if (++err_streak % 8 == 1) {
                ESP_LOGW(kTag, "NAU7802 i2c error: errc %d",
                         static_cast<int>(rdy.error()));
            }
            continue;
        }
        if (!*rdy) continue;
        if (pending == 0) {
            // An edge may have queued while we polled — drain it so the
            // same conversion isn't sampled twice on the next take.
            (void)ulTaskNotifyTake(pdTRUE, 0);
            t_us = esp_timer_get_time();   // polled sample: read time
        }
        const auto t0 = esp_timer_get_time();
        const auto v  = s_adc->read();
        sensor_lk.unlock();
        const auto i2c_dur = esp_timer_get_time() - t0;
        i2c_sum += i2c_dur;
        i2c_max  = std::max(i2c_max, i2c_dur);
        if (!v) {
            if (++err_streak % 8 == 1) {
                ESP_LOGW(kTag, "NAU7802 read failed: errc %d",
                         static_cast<int>(v.error()));
            }
            continue;
        }
        err_streak = 0;
        // Timestamps must be non-decreasing — the estimator treats dt as
        // truth. Any residual race can only nudge it, never step it back.
        if (prev_us != 0 && t_us < prev_us) t_us = prev_us;
        if (prev_us != 0) gap_max = std::max(gap_max, t_us - prev_us);
        prev_us = t_us;
        // Activity = a step away from a slowly-following count baseline:
        // pouring or a load placed/removed trips it instantly, while
        // thermal creep and quiet-state noise just move the baseline.
        const float raw = static_cast<float>(*v);
        if (!base_init) {
            activity_base = raw;
            base_init     = true;
        } else if (std::fabs(raw - activity_base) > kActivityCountsThresh) {
            mark_activity();
            activity_base = raw;
        } else {
            activity_base += (raw - activity_base) * 0.002f;
        }
        {
            const auto f0 = esp_timer_get_time();
            std::lock_guard lk(g_mtx);
            g_app.feed(*v, scale::clock_ms{t_us / 1000});
            d = g_app.inner().last_diag();
            const auto dur = esp_timer_get_time() - f0;
            feed_sum += dur;
            feed_max  = std::max(feed_max, dur);
        }
        // Raw record goes out after the model saw the sample — logging
        // can never hold up the estimator's view of the stream.
        if (g_raw.load(std::memory_order_relaxed)) {
            char line[64];
            snprintf(line, sizeof(line), "W,%lld,%ld\n",
                     static_cast<long long>(t_us),
                     static_cast<long>(*v));
            fputs(line, stdout);
        }
        if (g_telemetry.load(std::memory_order_relaxed)) {
            char line[192];
            scale::diag_csv(line, sizeof(line), d);
            fputs(line, stdout);   // USB-serial-JTAG console
        }
        if (++n_samp % 1600 == 0) {   // ~20 s at 80 Hz
            ESP_LOGI(kTag,
                     "adc: i2c %.1f/%lld us, feed %.1f/%lld us, "
                     "gap<=%lld us, coalesced %lu, late-poll %lu",
                     static_cast<double>(i2c_sum) / n_samp,
                     static_cast<long long>(i2c_max),
                     static_cast<double>(feed_sum) / n_samp,
                     static_cast<long long>(feed_max),
                     static_cast<long long>(gap_max),
                     static_cast<unsigned long>(n_coalesced),
                     static_cast<unsigned long>(n_late));
            i2c_sum = feed_sum = 0;
            i2c_max = feed_max = gap_max = 0;
            n_samp = n_coalesced = n_late = 0;
        }
    }
}

void accel_task(void*) {
    drv::lis2dw12<bus::i2c_dev_esp>::vec3 prev{};
    bool primed  = false;
    int  decim   = 0;   // feed_accel/motion run on every 5th sample (20 Hz
                        // effective — same meaning as before the rate bump)
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (g_peripherals_paused.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            last = xTaskGetTickCount(); // don't replay missed poll ticks
            primed = false;
            continue;
        }
        std::unique_lock sensor_lk(g_sensor_mtx);
        if (g_peripherals_paused.load()) continue;
        if (ulTaskNotifyTake(pdTRUE, 0) != 0) {
            // A tap/motion event latched INT1 — clear the source regs so the
            // pin drops again and count the event as user activity.
            if (s_acc) (void)s_acc->clear_wake_srcs();
            mark_activity();
        }
        std::optional<drv::lis2dw12<bus::i2c_dev_esp>::vec3> a;
        if (s_acc) {
            if (auto result = s_acc->read_mg(); result) a = *result;
        }
        sensor_lk.unlock(); // logging/model work must not block ADC's I2C read
        if (a && !g_peripherals_paused.load()) {
            if (g_raw.load(std::memory_order_relaxed)) {
                char line[96];
                snprintf(line, sizeof(line), "A,%lld,%.1f,%.1f,%.1f\n",
                         static_cast<long long>(esp_timer_get_time()),
                         static_cast<double>(a->x),
                         static_cast<double>(a->y),
                         static_cast<double>(a->z));
                fputs(line, stdout);
            }
            if (++decim >= 5) {
                decim = 0;
                // Static offset and a permanently tilted mount cancel out.
                if (primed &&
                    std::fabs(a->x - prev.x) + std::fabs(a->y - prev.y) +
                            std::fabs(a->z - prev.z) > kMotionThreshMg) {
                    mark_activity();
                }
                prev   = *a;
                primed = true;
                std::lock_guard lk(g_mtx);
                if (!g_app.sleeping()) g_app.feed_accel(a->x, a->y, a->z);
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));   // 100 Hz (tick = 10 ms)
    }
}

/// Minimal console: 't' toggles the per-sample CSV stream, 'z' = tare,
/// 'r' toggles the raw capture stream; 'p' shows battery + standby diagnostics.
void console_task(void*) {
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    for (;;) {
        if (g_peripherals_paused.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        char c;
        while (read(STDIN_FILENO, &c, 1) == 1) {
            mark_activity();
            if (c == 't') {
                const bool on = !g_telemetry.load();
                g_telemetry   = on;
                if (on) {
                    printf("%s\n", scale::kDiagCsvHeader);
                }
            } else if (c == 'r') {
                const bool on = !g_raw.load();
                g_raw         = on;
                if (on) {
                    double cpg, zero, tare;
                    {
                        std::lock_guard lk(g_mtx);
                        const auto& cal = g_app.inner().calibration_data();
                        cpg             = cal.counts_per_gram;
                        zero            = cal.zero_counts;
                        tare            = g_app.inner().tare_counts();
                    }
                    printf("# coffee-scale raw v1 cpg=%.6f zero=%.1f "
                           "tare=%.1f fw=%s\n", cpg, zero, tare,
                           esp_app_get_description()->version);
                }
            } else if (c == 'z') {
                raw_event("tare");
                std::lock_guard lk(g_mtx);
                g_app.tare();
            } else if (c == 'p') {
                ESP_LOGI(kTag, "power: VBAT=%d mV, estimate=%d%%, last sleep=%lld ms, wake=%s",
                         g_battery_mv.load(), g_battery_pct.load(),
                         static_cast<long long>(g_last_sleep_ms.load()),
                         wake_name(g_last_wake.load()));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void beep(int ms) {
    gpio_set_level(static_cast<gpio_num_t>(board::pins::buzz), 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(static_cast<gpio_num_t>(board::pins::buzz), 0);
}

void button_task(void*) {
    int pin = 0;
    for (;;) {
        if (xQueueReceive(g_btn_q, &pin, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const int64_t t = esp_timer_get_time() / 1000;
        {
            std::lock_guard lk(g_mtx);
            if (g_app.sleeping()) continue;
            auto& s = pin == board::pins::btn_tare ? g_tare_button : g_mode_button;
            const auto action = s.push(
                gpio_get_level(static_cast<gpio_num_t>(pin)) == 0,
                scale::clock_ms{t});
            if (action == scale::button_press::action::none) continue;
            mark_activity();
            if (pin == board::pins::btn_tare) {
                if (action == scale::button_press::action::long_press) {
                    g_app.tare_long(scale::clock_ms{t});
                    raw_event("tare_long");
                } else {
                    g_app.tare();
                    raw_event("tare");
                }
            } else if (action == scale::button_press::action::short_press) {
                g_app.next_mode();
                raw_event("mode");
            }
        }
        beep(40);
    }
}

// ---- power management -----------------------------------------------------
// Idle -> standby: LCD SLPIN + backlight off, BLE controller fully torn
// down, NAU7802 powered down, LIS2DW12 reconfigured to drive a LATCHED tap
// interrupt on INT1 (IO13 is an RTC pin, so a level wake works), MODE
// button as a second source. Deep sleep retains only semantic app state
// in RTC memory; the optional light-sleep fallback retains normal RAM.

const ble::source s_ble_src = {
    .ctx      = nullptr,
    .snapshot = &ble_snapshot,
    .command  = &ble_command,
    .activity = [](void*) { mark_activity(); },
};

void enter_sleep() {
    {
        std::lock_guard lk(g_mtx);
        g_app.prepare_sleep(scale::clock_ms{esp_timer_get_time() / 1000});
        g_tare_button.reset();
        g_mode_button.reset();
    }
    ESP_LOGI(kTag, "idle — low power (double-tap / MODE to wake)");
    beep(80);

    ui::render_pause(true);   // freeze LVGL + drain in-flight SPI DMA
    ui::display_power(false);
    // Latch the backlight GPIO low across light sleep — without hold the
    // pad can float when the digital domain gates and the LED relights.
    gpio_hold_en(static_cast<gpio_num_t>(board::pins::lcd_bl));
    ble::stop();
    g_peripherals_paused.store(true);
    // Wait for in-flight I2C reads, then own the bus through wake/restore.
    std::unique_lock sensor_lk(g_sensor_mtx);
    if (s_adc) {
        if (auto r = s_adc->power_down(); !r) {
            ESP_LOGW(kTag, "adc power_down failed: errc %d",
                     static_cast<int>(r.error()));
        }
    }
    bool tap_wake = false;
    if (s_acc) {
        if (auto r = s_acc->enable_tap_wake(); r) {
            tap_wake = true;
        } else {
            ESP_LOGW(kTag, "accel tap-wake config failed: errc %d",
                     static_cast<int>(r.error()));
        }
    }

    // Mask before clearing the latch and arming level wake. A latched
    // INT1 or a held button fires the GPIO ISR the instant interrupts are
    // unmasked inside esp_light_sleep_start, and NotifyGiveFromISR deadlocks
    // on the port spinlock (Interrupt WDT). Wake detection itself is the
    // pin's wake latch, not the ISR — so mask the ISRs for the sleep window.
    gpio_intr_disable(static_cast<gpio_num_t>(board::pins::accel_int1));
    gpio_intr_disable(static_cast<gpio_num_t>(board::pins::btn_mode));
    if (tap_wake) {
        if (auto r = s_acc->clear_wake_srcs(); !r) {
            tap_wake = false; // keep MODE wake even if the IMU is unavailable
            ESP_LOGW(kTag, "wake latch clear failed; MODE-only standby");
        }
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const auto entered_us = esp_timer_get_time();
    ESP_LOGI(kTag, "power: awake=%lld ms, VBAT=%d mV, estimate=%d%%, standby=%s",
             static_cast<long long>(entered_us / 1000 - g_awake_since_ms),
             g_battery_mv.load(), g_battery_pct.load(),
#if CONFIG_SCALE_DEEP_SLEEP
             "deep"
#else
             "light"
#endif
    );
    esp_err_t sleep_result;
#if CONFIG_SCALE_DEEP_SLEEP
    // S3 EXT1 requires one polarity across its pins: IMU uses EXT1 high,
    // active-low MODE uses EXT0. Both are RTC pins; IO48 cannot wake.
    sleep_result = esp_sleep_enable_ext0_wakeup(
        static_cast<gpio_num_t>(board::pins::btn_mode), 0);
    if (sleep_result == ESP_OK && tap_wake) {
        sleep_result = esp_sleep_enable_ext1_wakeup_io(
            1ULL << board::pins::accel_int1, ESP_EXT1_WAKEUP_ANY_HIGH);
    }
    if (sleep_result == ESP_OK) {
        {
            std::lock_guard lk(g_mtx);
            s_standby.app = g_app.save_standby();
        }
        s_standby.entered_wall_us = wall_us();
        s_standby.magic = kStandbyMagic;
        gpio_deep_sleep_hold_en(); // retain backlight LOW through reset
        sleep_result = esp_deep_sleep_try_to_start(); // only returns on failure
        s_standby.magic = 0;
        gpio_deep_sleep_hold_dis();
        rtc_gpio_deinit(static_cast<gpio_num_t>(board::pins::accel_int1));
        rtc_gpio_deinit(static_cast<gpio_num_t>(board::pins::btn_mode));
    }
#else
    sleep_result = esp_sleep_enable_gpio_wakeup();
    if (sleep_result == ESP_OK && tap_wake) {
        sleep_result = gpio_wakeup_enable(
            static_cast<gpio_num_t>(board::pins::accel_int1), GPIO_INTR_HIGH_LEVEL);
    }
    if (sleep_result == ESP_OK) {
        sleep_result = gpio_wakeup_enable(
            static_cast<gpio_num_t>(board::pins::btn_mode), GPIO_INTR_LOW_LEVEL);
    }
    if (sleep_result == ESP_OK) sleep_result = esp_light_sleep_start();
    gpio_wakeup_disable(static_cast<gpio_num_t>(board::pins::accel_int1));
    gpio_wakeup_disable(static_cast<gpio_num_t>(board::pins::btn_mode));
#endif
    const std::uint32_t cause = esp_sleep_get_wakeup_causes();

    // ---- resume ------------------------------------------------------------
    mark_activity();
    g_awake_since_ms = esp_timer_get_time() / 1000;
    g_last_sleep_ms = (esp_timer_get_time() - entered_us) / 1000;
    g_last_wake = sleep_result == ESP_OK ? cause : 0;
    ESP_LOGI(kTag, "power: slept=%lld ms, causes=0x%lx, MODE=%d INT1=%d, result=%s",
             static_cast<long long>((esp_timer_get_time() - entered_us) / 1000),
             static_cast<unsigned long>(cause),
             gpio_get_level(static_cast<gpio_num_t>(board::pins::btn_mode)),
             gpio_get_level(static_cast<gpio_num_t>(board::pins::accel_int1)),
             esp_err_to_name(sleep_result));
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    if (s_acc) {
        if (auto r = s_acc->disable_tap_wake(); !r) {
            ESP_LOGW(kTag, "accel tap-wake clear failed: errc %d",
                     static_cast<int>(r.error()));
        }
    }
    if (s_adc) {
        if (auto r = s_adc->power_up(&delay_ms); !r) {
            ESP_LOGW(kTag, "adc power_up failed: errc %d",
                     static_cast<int>(r.error()));
        }
    }
    {
        std::lock_guard lk(g_mtx);
        xQueueReset(g_btn_q);
        g_tare_button.reset(
            gpio_get_level(static_cast<gpio_num_t>(board::pins::btn_tare)) == 0);
        g_mode_button.reset(
            gpio_get_level(static_cast<gpio_num_t>(board::pins::btn_mode)) == 0);
        g_app.wake(scale::clock_ms{esp_timer_get_time() / 1000});
    }
    sensor_lk.unlock();
    g_peripherals_paused.store(false);
    // gpio_wakeup_enable reprogrammed the pins to level mode — restore the
    // awake-time edge interrupts and re-unmask the ISRs.
    gpio_set_intr_type(static_cast<gpio_num_t>(board::pins::accel_int1),
                       GPIO_INTR_POSEDGE);
    gpio_intr_enable(static_cast<gpio_num_t>(board::pins::accel_int1));
    gpio_intr_enable(static_cast<gpio_num_t>(board::pins::btn_mode));
    for (const int pin : {board::pins::btn_tare, board::pins::btn_mode}) {
        gpio_set_intr_type(static_cast<gpio_num_t>(pin), GPIO_INTR_ANYEDGE);
    }
    gpio_hold_dis(static_cast<gpio_num_t>(board::pins::lcd_bl));
    // The LCD has an RC power-on reset on the 3V3 rail — a supply dip
    // during sleep resets the controller (registers + GRAM gone, display
    // defaults to off). And a transaction frozen across the sleep boundary
    // can wedge the SPI queue — so rebuild the whole io/panel pair.
    ui::display_init();
    ui::display_power(true);
    ui::render_pause(false);
    if (const esp_err_t e = ble::start(s_ble_src); e != ESP_OK) {
        ESP_LOGW(kTag, "ble restart failed: %s", esp_err_to_name(e));
    }
    beep(40);
}

void power_task(void*) {
    mark_activity();   // boot counts as activity
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        bool timer_active;
        {
            std::lock_guard lk(g_mtx);
            timer_active = g_app.state().timer_state == scale::brew_timer::state::running;
        }
        // Charging or native USB host connected -> stay awake; this
        // keeps USB-Serial/JTAG alive so the board stays flashable.
        const bool powered = g_charging.load(std::memory_order_relaxed) ||
                             usb_serial_jtag_is_connected();
        const int64_t idle = esp_timer_get_time() / 1000 -
                             g_last_activity_ms.load(std::memory_order_relaxed);
        if (g_sleep_request.exchange(false) ||
            (!powered && !timer_active && idle >= kIdleTimeoutMs)) {
            enter_sleep();
        }
    }
}

/// Li-ion open-circuit % approximation — piecewise-linear between typical
/// resting points. Reads a bit low under load (voltage sags); the spread
/// sampler + EMA in battery_task already take out the transient part.
float battery_pct_of_mv(int mv) {
    struct Pt { int mv; float pct; };
    static constexpr Pt kCurve[] = {
        {3300,   0}, {3450,   5}, {3550,  10}, {3600,  15},
        {3650,  25}, {3700,  35}, {3750,  45}, {3800,  55},
        {3850,  65}, {3900,  75}, {4000,  85}, {4100,  95}, {4200, 100},
    };
    if (mv <= kCurve[0].mv) return 0.f;
    for (int i = 1; i < std::size(kCurve); ++i) {
        if (mv <= kCurve[i].mv) {
            const Pt &lo = kCurve[i - 1], &hi = kCurve[i];
            return lo.pct + (hi.pct - lo.pct) * (mv - lo.mv) / (hi.mv - lo.mv);
        }
    }
    return 100.f;
}

void battery_task(void*) {
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .clk_src  = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_unit_handle_t adc = nullptr;
    if (adc_oneshot_new_unit(&unit_cfg, &adc) != ESP_OK) {
        ESP_LOGE(kTag, "adc oneshot init failed");
        vTaskDelete(nullptr);
    }
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc, ADC_CHANNEL_8, &chan_cfg);   // GPIO9 = ADC1_CH8

    adc_cali_handle_t cali = nullptr;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = ADC_CHANNEL_8,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali) != ESP_OK) {
        cali = nullptr;
        ESP_LOGW(kTag, "no adc cali; battery pct approximate");
    }

    // R13/R14 100k/100k divider -> 50 k source impedance: a single oneshot
    // read droops and jitters on the sampling cap. And the reads must be
    // spread, not back-to-back: BLE conn events (~50 ms) and LCD bursts sag
    // VBAT by tens of mV, so a microsecond-long burst lands wholly inside
    // or outside a sag and the median flips between two plateaus (~10%).
    // ~100 ms spans at least two connection intervals; the median then
    // rejects the sag samples. Slow EMA on top damps the rest. (A ~100 nF
    // cap on the divider midpoint would fix it in hardware.)
    constexpr int kSamples    = 13;
    constexpr int kSampleGapMs = 8;
    int          raws[kSamples] = {};
    float        pct_f = -1.f;

    // Internal chip temperature — raw-capture 'C' lines alongside the
    // TMP102 'T' lines on the same ~2 s cadence.
    temperature_sensor_handle_t chip_ts = nullptr;
    {
        const temperature_sensor_config_t ts_cfg =
            TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&ts_cfg, &chip_ts) != ESP_OK) {
            ESP_LOGW(kTag, "chip temp sensor unavailable; no C lines");
            chip_ts = nullptr;
        }
    }

    for (;;) {
        if (g_peripherals_paused.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        for (int i = 0; i < kSamples; ++i) {
            adc_oneshot_read(adc, ADC_CHANNEL_8, &raws[i]);
            vTaskDelay(pdMS_TO_TICKS(kSampleGapMs));
        }
        std::qsort(raws, kSamples, sizeof(int),
                   [](const void* a, const void* b) {
                       return *static_cast<const int*>(a) - *static_cast<const int*>(b);
                   });
        int mv = 0;
        if (cali) {
            adc_cali_raw_to_voltage(cali, raws[kSamples / 2], &mv);
        } else {
            mv = raws[kSamples / 2] * 3100 / 4095;   // crude fallback
        }
        // TP4057 STAT is open-drain, low while charging (R11 pulls up).
        const bool charging =
            gpio_get_level(static_cast<gpio_num_t>(board::pins::chrg_stat)) == 0;
        // While charging the terminal sits ~80 mV above the cell's resting
        // voltage (charge current × internal+trace R). Estimate against the
        // resting voltage so unplugging doesn't produce a visible % step.
        const int vbat = mv * 2 - (charging ? 80 : 0);
        g_battery_mv = mv * 2; // actual terminal estimate, before charge correction
        const float pct = battery_pct_of_mv(vbat);
        pct_f = pct_f < 0 ? pct : pct_f * 0.8f + pct * 0.2f;
        // Sticky display: sub-2% wander (charger float, ADC noise) must not
        // flicker the readout — update in 2% steps, except hitting the rails.
        const int rounded = static_cast<int>(pct_f + 0.5f);
        static int shown = -1;
        if (shown < 0 || std::abs(rounded - shown) >= 2 ||
            (rounded == 100 && shown < 100) || rounded == 0) {
            shown = rounded;
        }
        g_battery_pct = shown;
        g_charging = charging;
        // TMP102 beside the load cell -> thermal drift model input.
        std::optional<float> temperature;
        std::unique_lock sensor_lk(g_sensor_mtx);
        if (s_tmp && !g_peripherals_paused.load()) {
            if (auto t = s_tmp->read_celsius(); t) temperature = *t;
        }
        sensor_lk.unlock();
        if (temperature && !g_peripherals_paused.load()) {
            {
                std::lock_guard lk(g_mtx);
                g_app.set_temperature(*temperature);
            }
            if (g_raw.load(std::memory_order_relaxed)) {
                char line[40];
                snprintf(line, sizeof(line), "T,%lld,%.3f\n",
                         static_cast<long long>(esp_timer_get_time()),
                         static_cast<double>(*temperature));
                fputs(line, stdout);
            }
        }
        if (chip_ts && g_raw.load(std::memory_order_relaxed)) {
            std::optional<float> chip_c;
            {
                std::lock_guard sensor(g_sensor_mtx);
                if (!g_peripherals_paused.load() &&
                    temperature_sensor_enable(chip_ts) == ESP_OK) {
                    float value = 0.f;
                    if (temperature_sensor_get_celsius(chip_ts, &value) == ESP_OK) chip_c = value;
                    temperature_sensor_disable(chip_ts);
                }
            }
            if (chip_c) {
                char line[40];
                snprintf(line, sizeof(line), "C,%lld,%.2f\n",
                         static_cast<long long>(esp_timer_get_time()),
                         static_cast<double>(*chip_c));
                fputs(line, stdout);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ---- ui bridge -----------------------------------------------------------------

void ui_refresh(void*) {
    ui::model m;
    {
        std::lock_guard lk(g_mtx);
        m.snap          = g_app.state();
        m.display_value = g_app.display_value();
    }
    m.battery_pct = g_battery_pct.load();
    m.charging    = g_charging.load();
    ui::update(m);
}

// ---- BLE bridge ---------------------------------------------------------------
// Frame/opcode layout lives in scale_proto/proto.hpp — the single ABI
// shared with the web app (WASM decode) and the Korvo remote.

proto::state ble_snapshot(void*) {
    scale::app::snapshot s;
    float                dv;
    {
        std::lock_guard lk(g_mtx);
        s  = g_app.state();
        dv = g_app.display_value();
    }
    return {
        .grams         = s.grams,
        .flow_gps      = s.flow_gps,
        .display_value = dv,
        .pitch_deg     = s.pitch_deg,
        .roll_deg      = s.roll_deg,
        .timer_ms =
            static_cast<std::uint32_t>(s.timer_elapsed.count()),
        .battery_pct = static_cast<std::int8_t>(g_battery_pct.load()),
        .unit        = static_cast<std::uint8_t>(s.u),
        .mode        = static_cast<std::uint8_t>(s.m),
        .timer_state = static_cast<std::uint8_t>(s.timer_state),
        .seq         = 0,          // assigned by the push task
        .stable      = s.stable,
        .tared       = s.tared,
        .calibrated  = s.calibrated,
        .charging    = g_charging.load(),
    };
}

void ble_command(void*, const proto::command& cmd) {
    mark_activity();
    if (cmd.o == proto::op::sleep) {
        std::lock_guard lk(g_mtx);
        if (g_app.sleeping()) return;
        // Freeze immediately: power_task may be up to a second away,
        // and a queued web auto-timer command must not start a brew then.
        g_app.prepare_sleep(scale::clock_ms{esp_timer_get_time() / 1000});
        g_sleep_request.store(true, std::memory_order_relaxed);
        return;
    }
    scale::calibration cal{};   // copied under the lock, saved after —
    bool               save_cal = false;   // an NVS write is too slow
    {                                        // to hold g_mtx through it
        std::lock_guard lk(g_mtx);
        if (g_app.sleeping()) return;
        switch (cmd.o) {
            case proto::op::tare:
                g_app.tare();
                raw_event("tare");
                break;
            case proto::op::timer_toggle:
                g_app.tare_long(scale::clock_ms{esp_timer_get_time() / 1000});
                raw_event("timer_toggle");
                break;
            case proto::op::timer_reset:
                g_app.timer_reset();
                raw_event("timer_reset");
                break;
            case proto::op::mode:
                g_app.next_mode();
                raw_event("mode");
                break;
            case proto::op::unit:
                g_app.set_unit(cmd.arg == 1 ? scale::unit::ounce
                                            : scale::unit::gram);
                raw_event("unit");
                break;
            case proto::op::cal_zero:
                // Calibration wizard: empty-pan zero capture, persisted
                // to NVS.
                g_app.cal_zero();
                raw_event("cal_zero");
                cal      = g_app.inner().calibration_data();
                save_cal = true;
                break;
            case proto::op::cal_span:
                raw_event("cal_span");
                if (g_app.cal_span(cmd.arg / 100.0f)) {   // centigrams
                    cal      = g_app.inner().calibration_data();
                    save_cal = true;
                }
                break;
            default:
                break;
        }
    }
    if (save_cal) {
        save_calibration(cal);
        ESP_LOGI(kTag, "cal saved: %.2f counts/g",
                 static_cast<double>(cal.counts_per_gram));
    }
}

} // namespace

extern "C" void app_main() {
    const bool standby_resume = esp_reset_reason() == ESP_RST_DEEPSLEEP &&
                                s_standby.magic == kStandbyMagic;
    const auto wake_cause = esp_sleep_get_wakeup_causes();
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
        // EXT0/EXT1 leave these pads in RTC mode. Restore normal buttons
        // and interrupts before configuring the digital GPIO driver.
        rtc_gpio_deinit(static_cast<gpio_num_t>(board::pins::accel_int1));
        rtc_gpio_deinit(static_cast<gpio_num_t>(board::pins::btn_mode));
        gpio_deep_sleep_hold_dis();
        gpio_set_level(static_cast<gpio_num_t>(board::pins::lcd_bl), 0);
        gpio_set_direction(static_cast<gpio_num_t>(board::pins::lcd_bl), GPIO_MODE_OUTPUT);
        gpio_hold_dis(static_cast<gpio_num_t>(board::pins::lcd_bl));
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    // USB-Serial-JTAG console: install the real driver (interrupt-driven
    // RX/TX ring buffers) and point stdio at it — without this the VFS
    // never drains host->device RX, so console keys ('t'/'z'/'r') are
    // dead. TX safety on a stalled/absent host is in the VFS layer
    // itself: a full tx buffer blocks once ≤50 ms then drops bytes until
    // space frees; is_connected()==false fails writes immediately, so
    // the scale never stalls when running on battery.
    usb_serial_jtag_driver_config_t usj_cfg =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usj_cfg.tx_buffer_size = 4096;   // raw stream: ~180 B/s W + ~150 B/s A
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usj_cfg));
    usb_serial_jtag_vfs_use_driver();

    if (const esp_err_t e = nvs_flash_init();
        e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Restore before any sampling task can consume the new clock origin.
    if (standby_resume) {
        g_app.restore_standby(s_standby.app, scale::clock_ms{esp_timer_get_time() / 1000});
        g_last_sleep_ms = std::max<std::int64_t>(
            0, (wall_us() - s_standby.entered_wall_us) / 1000);
        g_last_wake = wake_cause;
        ESP_LOGI(kTag, "power: deep wake=%s, slept=%lld ms, tare/timer restored",
                 wake_name(wake_cause), static_cast<long long>(g_last_sleep_ms.load()));
    } else {
        scale::calibration c;
        if (load_calibration(c)) {
            g_app.load_calibration(c);
            ESP_LOGI(kTag, "calibration loaded: %.1f counts/g",
                     static_cast<double>(c.counts_per_gram));
        }
    }
    s_standby.magic = 0; // a later watchdog/reset must not restore stale state
    g_awake_since_ms = esp_timer_get_time() / 1000;

    // ---- i2c bus + sensor devices ------------------------------------------
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = static_cast<gpio_num_t>(board::pins::i2c_sda),
        .scl_io_num        = static_cast<gpio_num_t>(board::pins::i2c_scl),
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority     = 0,
        .trans_queue_depth = 0,
        .flags             = {.enable_internal_pullup = false, .allow_pd = false},
    };
    i2c_master_bus_handle_t bus = nullptr;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    auto add_dev = [&bus](std::uint8_t addr, std::optional<bus::i2c_dev_esp>& slot) {
        if (auto d = bus::i2c_dev_esp::create(bus, addr)) {
            slot = std::move(*d);
        } else {
            ESP_LOGE(kTag, "i2c dev 0x%02x add failed", addr);
        }
    };
    add_dev(board::i2c_addr::nau7802, s_adc_dev);
    add_dev(board::i2c_addr::lis2dw12, s_acc_dev);
    add_dev(board::i2c_addr::tmp102, s_tmp_dev);
    if (s_adc_dev) s_adc.emplace(*s_adc_dev);
    if (s_acc_dev) s_acc.emplace(*s_acc_dev);
    if (s_tmp_dev) s_tmp.emplace(*s_tmp_dev);

    if (s_adc) {
        if (auto r = s_adc->init({}, &delay_ms); r) {
            ESP_LOGI(kTag, "NAU7802 up (rev %u expected)", 0x0F);
        } else {
            ESP_LOGE(kTag, "NAU7802 init failed: errc %d", static_cast<int>(r.error()));
        }
    }
    if (s_acc) {
        if (auto r = s_acc->init(); r) {
            ESP_LOGI(kTag, "LIS2DW12 up");
            // Tap ODR is 400 Hz only during standby; awake measurement
            // remains 100 Hz. Poll deltas already count real motion.
            (void)s_acc->disable_tap_wake();
        } else {
            ESP_LOGE(kTag, "LIS2DW12 init failed: errc %d", static_cast<int>(r.error()));
        }
    }
    // Probe the TMP102 once: s3.1 does not fit it, and polling a dead
    // address every 2 s just burns bus time. Boards that have it get the
    // full drift-comp path; boards that don't read it never again.
    if (s_tmp) {
        if (auto t = s_tmp->read_celsius(); t) {
            ESP_LOGI(kTag, "TMP102 %.2f C", static_cast<double>(*t));
        } else {
            ESP_LOGW(kTag, "TMP102 not fitted — thermal comp off");
            s_tmp.reset();
        }
    }

    // ---- gpio --------------------------------------------------------------
    const gpio_config_t inputs = {
        .pin_bit_mask = (1ull << board::pins::adc_drdy) |
                        (1ull << board::pins::accel_int1) |
                        (1ull << board::pins::btn_tare) |
                        (1ull << board::pins::btn_mode) |
                        (1ull << board::pins::chrg_stat),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,   // external pull-ups on board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&inputs));

    const gpio_config_t outputs = {
        .pin_bit_mask = 1ull << board::pins::buzz,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&outputs));

    // ---- tasks ---------------------------------------------------------------
    g_btn_q = xQueueCreate(8, sizeof(int));
    xTaskCreate(adc_task, "adc", 4096, nullptr, 8, &g_adc_task);
    xTaskCreate(accel_task, "accel", 3072, nullptr, 4, &g_accel_task);
    xTaskCreate(button_task, "btn", 3072, nullptr, 6, nullptr);
    xTaskCreate(battery_task, "batt", 3072, nullptr, 3, nullptr);
    xTaskCreate(console_task, "console", 3072, nullptr, 2, nullptr);
    xTaskCreate(power_task, "power", 3072, nullptr, 2, nullptr);
    ESP_LOGI(kTag, "console: 't' = CSV telemetry, 'z' = tare, 'r' = raw capture, 'p' = power");

    gpio_install_isr_service(0);
    gpio_set_intr_type(static_cast<gpio_num_t>(board::pins::adc_drdy), GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(static_cast<gpio_num_t>(board::pins::adc_drdy), drdy_isr, nullptr);
    for (const int pin : {board::pins::btn_tare, board::pins::btn_mode}) {
        gpio_set_intr_type(static_cast<gpio_num_t>(pin), GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(static_cast<gpio_num_t>(pin), btn_isr,
                             reinterpret_cast<void*>(static_cast<intptr_t>(pin)));
    }
    gpio_set_intr_type(static_cast<gpio_num_t>(board::pins::accel_int1),
                       GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(static_cast<gpio_num_t>(board::pins::accel_int1),
                         int1_isr, nullptr);

    // ---- app + ui -------------------------------------------------------------
    // TODO: expose cal_zero()/cal_span()+save_calibration() via a button menu
    // or a USB console command once the bring-up flow settles.

    ui::port_init();
    ui::set_refresh(ui_refresh, nullptr);   // ui::create() runs on the LVGL task

    // BLE peripheral: state notify + command write (proto.hpp ABI).
    // Non-fatal — the scale works headless if it fails.
    if (const esp_err_t e = ble::start(s_ble_src); e != ESP_OK) {
        ESP_LOGW(kTag, "ble start failed: %s", esp_err_to_name(e));
    }

    ESP_LOGI(kTag, "scale-adc-s3 firmware up");
}
