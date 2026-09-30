#pragma once
/// Display+touch bring-up via the board BSP (espressif/esp32_s31_korvo_1):
/// 800x480 RGB LCD + GT1151 touch + LVGL port task, one call.
///
/// The BSP owns the LVGL tick/handler loop — guard every LVGL call with
/// bsp_display_lock()/bsp_display_unlock() from app code.

#include "lvgl.h"

namespace display {

bool init();

extern lv_display_t* disp;
extern lv_indev_t*   touch;

} // namespace display
