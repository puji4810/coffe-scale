#pragma once
/// The remote's screen: 800x480 LVGL. Left = scale vitals (same visual
/// language as the ST7789 screen), right = rolling weight/flow curve,
/// bottom = touch controls that send wire-protocol commands.

#include "link.hpp"

namespace ui {

/// Build the screen on the active LVGL display. Call once on the LVGL task.
void create();

/// Repaint from the latest link snapshot; call on the LVGL task (~20 Hz).
void update();

} // namespace ui
