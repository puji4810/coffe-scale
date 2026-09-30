// Emscripten port of ui::port_init(): LVGL renders into RGB565 partial
// buffers (same shape as the target), flush_cb expands each dirty area into
// an RGBA8888 staging frame that JS blits straight into a <canvas> via
// ImageData — so the preview shows the real RGB565 rendering, not a
// higher-depth re-render.

#include "wasm_port.hpp"

#include <cstddef>
#include <cstdint>

#include <emscripten.h>

#include "lvgl.h"
#include "ui/ui.hpp"

namespace {

// Quarter-screen partial buffers, matching ui_port_esp / sdl_port.
constexpr std::size_t kBufPixels = ui::hor_res * 40;
lv_color_t            s_buf1[kBufPixels];
lv_color_t            s_buf2[kBufPixels];

// Full-frame canvas staging: little-endian u32 = A<<24|B<<16|G<<8|R.
std::uint32_t s_rgba[ui::hor_res * ui::ver_res];
bool          s_dirty = false;

constexpr std::uint32_t expand5(std::uint32_t v) { return (v * 527 + 23) >> 6; }
constexpr std::uint32_t expand6(std::uint32_t v) { return (v * 259 + 33) >> 6; }

void flush_cb(lv_display_t* disp, const lv_area_t* area, std::uint8_t* px_map) {
    const int          w   = area->x2 - area->x1 + 1;
    const int          h   = area->y2 - area->y1 + 1;
    const auto*        db  = lv_display_get_buf_active(disp);
    const std::size_t  row = db ? db->header.stride / sizeof(std::uint16_t)
                                : static_cast<std::size_t>(w);
    const std::uint16_t* px = reinterpret_cast<const std::uint16_t*>(px_map);
    for (int y = 0; y < h; ++y) {
        const std::uint16_t* src = px + y * row;
        std::uint32_t*       dst =
            s_rgba + (area->y1 + y) * ui::hor_res + area->x1;
        for (int x = 0; x < w; ++x) {
            const std::uint16_t c  = src[x];
            const std::uint32_t r8 = expand5((c >> 11) & 0x1F);
            const std::uint32_t g8 = expand6((c >> 5) & 0x3F);
            const std::uint32_t b8 = expand5(c & 0x1F);
            dst[x] = 0xFF000000u | (b8 << 16) | (g8 << 8) | r8;
        }
    }
    s_dirty = true;
    lv_display_flush_ready(disp);
}

std::uint32_t tick_cb() {
    return static_cast<std::uint32_t>(emscripten_get_now());
}

} // namespace

void ui::port_init() {
    lv_init();
    lv_tick_set_cb(&tick_cb);
    lv_display_t* disp = lv_display_create(ui::hor_res, ui::ver_res);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, &flush_cb);
}

const std::uint32_t* ui::wasm::framebuffer() { return s_rgba; }

std::size_t ui::wasm::framebuffer_len() { return ui::hor_res * ui::ver_res; }

bool ui::wasm::take_dirty() {
    const bool d = s_dirty;
    s_dirty      = false;
    return d;
}

void ui::display_power(bool) {}   // no panel/backlight in wasm
void ui::display_init() {}
void ui::render_pause(bool) {}
