// ESP port of ui::port_init(): SPI + esp_lcd ST7789 + LVGL display + LVGL task.
// Only built in the ESP-IDF build (xmake lists only scale_ui.cpp).

#include "ui/ui.hpp"

#include "board/pins.hpp"

#include <atomic>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace {

const char* kTag = "ui";

esp_lcd_panel_handle_t    s_panel;
esp_lcd_panel_io_handle_t s_io;
lv_display_t*          s_disp;
TaskHandle_t           s_lvgl_task;
std::atomic<bool>      s_full_redraw{false};   // consumed on the LVGL thread
std::atomic<bool>      s_flush_pending{false}; // a color DMA is in flight

// LVGL render buffers: two 40-line chunks in internal RAM (DMA-able for
// esp_lcd SPI writes). RGB565, 320 * 40 * 2 B = 25 kB each — don't size by
// lv_color_t, which is 24-bit in LVGL 9 regardless of the display format.
constexpr std::size_t kBufPixels = ui::hor_res * 40;
alignas(4) std::uint16_t s_buf1[kBufPixels];
alignas(4) std::uint16_t s_buf2[kBufPixels];

void flush_cb(lv_display_t*, const lv_area_t* area, std::uint8_t* px_map) {
    s_flush_pending.store(true);
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, px_map);
    // No lv_display_flush_ready() here: the SPI DMA still owns px_map.
    // It is signaled from trans_done_cb once the last chunk is on the wire —
    // otherwise LVGL re-renders into the buffer mid-transfer and flushed
    // areas come out as colorful garbage.
}

bool trans_done_cb(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*,
                   void*) {
    s_flush_pending.store(false);
    lv_display_flush_ready(s_disp);
    return false;
}

std::uint32_t tick_cb() { return static_cast<std::uint32_t>(esp_timer_get_time() / 1000); }

void lvgl_task(void*) {
    ui::create();   // per ui.hpp: LVGL widget calls only on the LVGL thread
    int n = 0;
    for (;;) {
        if (s_full_redraw.exchange(false)) {
            lv_obj_invalidate(lv_screen_active());
        }
        lv_timer_handler();
        if (++n % 4 == 0) {
            ui::housekeeping();   // ~40 ms refresh of model -> widgets
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

} // namespace

/// ST7789 register sequence — applied to a fresh panel AND re-run after
/// light sleep (the RC power-on reset can reset the controller on wake).
void panel_init_seq() {
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    // 172x320 glass centered on the 240x320 controller RAM -> 34 px column
    // gap. Gaps are applied to CASET/RASET, NOT to logical x/y: with
    // swap_xy (MADCTL MV) CASET addresses the 320-row axis and RASET the
    // 240-column axis, so the column gap belongs in y_gap. On x_gap it
    // shifts the row window out of range (wraps) and leaves RAM columns
    // 172..205 unwritten -> power-on garbage ("snow") along one edge.
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 0, 34));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));   // -> 320x172 landscape
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
}

/// (Re)create the esp_lcd io + panel pair and run the ST7789 init sequence.
/// Called from port_init() and again after light sleep: a transaction frozen
/// across the sleep boundary can wedge spi_master's queue for good, and the
/// only reliable way back to a clean driver state is fresh handles.
void panel_create() {
    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num       = static_cast<gpio_num_t>(board::pins::lcd_cs),
        .dc_gpio_num       = static_cast<gpio_num_t>(board::pins::lcd_dc),
        .spi_mode          = 0,
        .pclk_hz           = 20'000'000,
        .trans_queue_depth = 4,
        .on_color_trans_done = &trans_done_cb,
        .user_ctx            = nullptr,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
        .cs_ena_pretrans     = 0,
        .cs_ena_posttrans    = 0,
        .flags               = {},
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io, &s_io));

    // LCD_RST is a local RC power-on reset on this board — no GPIO drives it.
    const esp_lcd_panel_dev_config_t cfg = {
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,  // flip to BGR on hardware if colors swap
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,  // LVGL emits RGB565 little-endian; BIG would byte-swap every pixel (text AA turns into colorful noise)
        .bits_per_pixel = 16,
        .reset_gpio_num = GPIO_NUM_NC,
        .vendor_config  = nullptr,
        .flags          = {},
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));   // no-op without a reset GPIO
    panel_init_seq();
}

void ui::port_init() {
    // ---- SPI bus + ST7789 ------------------------------------------------
    const spi_bus_config_t bus = {
        .mosi_io_num     = board::pins::lcd_mosi,
        .miso_io_num     = -1,
        .sclk_io_num     = board::pins::lcd_sck,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .data4_io_num    = -1,
        .data5_io_num    = -1,
        .data6_io_num    = -1,
        .data7_io_num    = -1,
        .data_io_default_level = false,
        .max_transfer_sz = static_cast<int>(kBufPixels * sizeof(std::uint16_t)) + 8,
        .flags           = 0,
        .isr_cpu_id      = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags      = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    panel_create();

    // Backlight: plain GPIO level control.
    gpio_set_direction(static_cast<gpio_num_t>(board::pins::lcd_bl), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(board::pins::lcd_bl), 1);

    // ---- LVGL ------------------------------------------------------------
    lv_init();
    lv_tick_set_cb(&tick_cb);
    s_disp = lv_display_create(hor_res, ver_res);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, &flush_cb);

    xTaskCreate(lvgl_task, "lvgl", 8 * 1024, nullptr, 4, &s_lvgl_task);
    ESP_LOGI(kTag, "ui port up: %dx%d RGB565", hor_res, ver_res);
}

/// Suspend rendering around system sleep and drain the SPI queue: a DMA
/// transaction frozen mid-flight by light sleep can lose its completion
/// interrupt and wedge the esp_lcd transfer queue for good — every later
/// draw_bitmap then fails silently and the panel shows stale garbage.
void ui::render_pause(bool paused) {
    if (!s_lvgl_task) return;
    if (paused) {
        vTaskSuspend(s_lvgl_task);
        for (int i = 0; i < 50 && s_flush_pending.load(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));   // ~4 ms per 40-line chunk
        }
    } else {
        vTaskResume(s_lvgl_task);
    }
}

void ui::display_power(bool on) {
    esp_lcd_panel_disp_on_off(s_panel, on);   // DISPON off: GRAM retained
    gpio_set_level(static_cast<gpio_num_t>(board::pins::lcd_bl), on ? 1 : 0);
}

void ui::display_init() {
    // Wake path: the old io/panel pair may hold a wedged transaction queue —
    // drop it and start clean rather than trusting the DMA to complete.
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        esp_lcd_panel_io_del(s_io);
    }
    panel_create();
    // Any flush that was in flight is gone with the old queue — release LVGL
    // and force a full repaint (re-init may have scrambled GRAM anyway).
    s_flush_pending.store(false);
    if (s_disp) {
        lv_display_flush_ready(s_disp);
    }
    s_full_redraw.store(true);
}
