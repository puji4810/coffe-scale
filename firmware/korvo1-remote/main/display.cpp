#include "display.hpp"

#include "bsp/esp32_s31_korvo_1.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_touch_gt1151.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace {

constexpr char kTag[] = "disp";

} // namespace

lv_display_t* display::disp  = nullptr;
lv_indev_t*   display::touch = nullptr;

bool display::init() {
    // Same path as bsp_display_start() but the GT1151 touch probe is gated
    // on i2c_master_probe — the touch io layer passes an infinite timeout,
    // so a dead/absent touch controller would hang bsp_display_start().
    // (On this board the touch bus is shared with the camera module's SCCB
    // on GPIO0/1 — with the camera attached the bus is dead and the probe
    // is what keeps boot alive.)
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), kTag, "lvgl_port_init failed");

    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    const bsp_display_config_t disp_cfg = {};
    ESP_RETURN_ON_ERROR(bsp_display_new(&disp_cfg, &panel, &io),
                        kTag, "bsp_display_new failed");
    esp_lcd_panel_disp_on_off(panel, true);

    lvgl_port_display_cfg_t lcfg = {};
    lcfg.io_handle = io;
    lcfg.panel_handle = panel;
    lcfg.buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES;  // assert requires >0
    lcfg.hres = BSP_LCD_H_RES;
    lcfg.vres = BSP_LCD_V_RES;
    lcfg.color_format = LV_COLOR_FORMAT_RGB565;
    lcfg.flags.direct_mode = true;    // draw straight into the PSRAM fb
    lvgl_port_display_rgb_cfg_t rcfg = {};
    rcfg.flags.avoid_tearing = true;  // two driver fbs (BSP_LCD_RGB_BUFFER_NUMS=2)
#if CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_MODE
    rcfg.flags.bb_mode = true;        // LCD scans from internal SRAM, not PSRAM
#endif
    disp = lvgl_port_add_disp_rgb(&lcfg, &rcfg);
    ESP_RETURN_ON_FALSE(disp != nullptr, false, kTag, "lvgl_port_add_disp_rgb failed");

    // touch: probe first, only bind if the GT1151 actually answers
    bsp_i2c_init();
    i2c_master_bus_handle_t i2c = bsp_i2c_get_handle();
    uint16_t tp_addr = 0;
    // GT1151 address is latched from the INT pin during reset: 0x14 or 0x5D
    const uint16_t addrs[] = {ESP_LCD_TOUCH_IO_I2C_GT1151_ADDRESS, 0x5D, 0x14};
    for (uint16_t a : addrs) {
        if (i2c && i2c_master_probe(i2c, a, 100) == ESP_OK) { tp_addr = a; break; }
    }
    if (tp_addr) {
        esp_lcd_touch_handle_t tp = nullptr;
        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = BSP_LCD_H_RES;
        tp_cfg.y_max = BSP_LCD_V_RES;
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;
        tp_cfg.int_gpio_num = GPIO_NUM_NC;
        esp_lcd_panel_io_handle_t tp_io = nullptr;
        // same as ESP_LCD_TOUCH_IO_I2C_GT1151_CONFIG(), spelled out so the
        // -Werror=missing-field-initializers build is happy
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
        tp_io_cfg.dev_addr = tp_addr;
        tp_io_cfg.scl_speed_hz = 100000;
        tp_io_cfg.control_phase_bytes = 1;
        tp_io_cfg.lcd_cmd_bits = 16;
        tp_io_cfg.flags.disable_control_phase = 1;
        if (esp_lcd_new_panel_io_i2c(i2c, &tp_io_cfg, &tp_io)
                == ESP_OK &&
            esp_lcd_touch_new_i2c_gt1151(tp_io, &tp_cfg, &tp) == ESP_OK) {
            lvgl_port_touch_cfg_t tcfg = {};
            tcfg.disp = disp;
            tcfg.handle = tp;
            touch = lvgl_port_add_touch(&tcfg);
        }
        if (!touch) ESP_LOGW(kTag, "GT1151 answered probe but init failed — display only");
    } else {
        ESP_LOGW(kTag, "GT1151 not answering on i2c — display only");
    }
    return true;
}
