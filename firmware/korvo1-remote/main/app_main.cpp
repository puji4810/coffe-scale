/// korvo1-remote — touch remote display/controller for the coffee scale.
///
///   link      BLE central: scan for the scale's service UUID, connect,
///             subscribe to the 20 Hz state notifications (proto.hpp)
///   display   BSP: 800x480 RGB LCD + GT1151 touch + LVGL (esp_lvgl_port
///             owns the LVGL task/tick — all LVGL calls under
///             bsp_display_lock/unlock)
///   ui        vitals + curve + command buttons

#include "bsp/esp32_s31_korvo_1.h"
#include "display.hpp"
#include "link.hpp"
#include "ui.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace {

constexpr char kTag[] = "remote";

void ui_task(void*) {
    ESP_LOGI(kTag, "ui task alive");
    int tries = 0;
    while (!bsp_display_lock(pdMS_TO_TICKS(1000))) {
        if (++tries % 5 == 0) ESP_LOGW(kTag, "lvgl lock timeout x%d", tries);
    }
    ui::create();
    bsp_display_unlock();
    ESP_LOGI(kTag, "ui created");
    for (;;) {
        if (bsp_display_lock(pdMS_TO_TICKS(100))) {
            ui::update();
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(50));   // 20 Hz matches the BLE push rate
    }
}

} // namespace

extern "C" void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());

    net::start();                     // BLE scan/connect, self-healing

    if (!display::init()) {
        ESP_LOGE(kTag, "display init failed — check the SUB3 board/ribbon");
        return;
    }

    xTaskCreate(ui_task, "ui", 8192, nullptr, 4, nullptr);
    ESP_LOGI(kTag, "korvo1-remote up");
}
