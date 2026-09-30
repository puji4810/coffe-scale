#pragma once

/// Scale UI on the ST7789 (native 172x320, used rotated as 320x172 landscape).
///
/// Two halves:
///   port layer  — ui::port_init(), platform specific (esp_lcd+lvgl on target,
///                 SDL in the sim). Brings up lv_init + an lv_display_t.
///   screen      — ui::create()/ui::update(), pure LVGL calls, identical on
///                 both platforms. Must run on the LVGL thread.

#include "scale/app.hpp"

namespace ui {

inline constexpr int hor_res = 320;
inline constexpr int ver_res = 172;

/// Everything the screen needs for one repaint.
struct model {
    scale::app::snapshot snap;
    float                display_value;  // weight in snap.u units
    int                  battery_pct;    // 0..100, <0 = unknown
    bool                 charging;
};

/// Platform bring-up: display hardware + lv_init + lv_display_t.
void port_init();

/// Build the scale screen on the active LVGL screen. LVGL thread only.
void create();

/// Repaint from model. LVGL thread only.
void update(const model& m);

/// Register a hook the port invokes periodically (every ~30 ms) on the LVGL
/// thread — this is where the platform should push fresh state via update().
void set_refresh(void (*fn)(void* ctx), void* ctx);

/// Runs the pending refresh hook; the port's LVGL task calls this.
void housekeeping();

/// Blank/unblank the panel + backlight for the power manager. Panel RAM
/// survives DISPON-off, so the previous frame returns instantly on wake.
void display_power(bool on);

/// Re-run the panel init sequence and force a full repaint. Needed on
/// wake-from-sleep: the LCD has an RC power-on reset on the 3V3 rail, so
/// a supply dip can reset it (registers back to defaults, display off)
/// while the SoC slept — a plain DISPON is then not enough.
void display_init();

/// Freeze/resume the LVGL task around system sleep. Pausing drains the
/// SPI queue first — a DMA transfer frozen mid-flight by light sleep can
/// lose its completion interrupt and wedge the driver queue for good.
void render_pause(bool paused);

} // namespace ui
