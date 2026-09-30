// SDL2 port: implements ui::port_init() for the desktop preview — an SDL
// window + streaming RGB565 texture as the LVGL flush target, plus a pointer
// indev for the mouse. No LVGL SDL driver needed; this is ~150 lines and is
// version-robust.

#include "sdl_port.hpp"

#include "ui/ui.hpp"

#include <SDL.h>

#include "lvgl.h"

namespace {

SDL_Window*   s_win;
SDL_Renderer* s_ren;
SDL_Texture*  s_tex;
bool          s_quit = false;

// Partial render buffers, quarter screen like the target.
constexpr std::size_t kBufPixels = ui::hor_res * 40;
lv_color_t s_buf1[kBufPixels];
lv_color_t s_buf2[kBufPixels];

void flush_cb(lv_display_t* disp, const lv_area_t* area, std::uint8_t* px_map) {
    SDL_Rect r{area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1};
    // Row pitch must come from the active draw buffer: LVGL reshapes it
    // to the area's width (STRIDE_AUTO), and lv_color_t is the 3-byte
    // API colour, NOT the 2-byte RGB565 framebuffer pixel — using
    // sizeof(lv_color_t) here scrambles every row.
    const auto* db    = lv_display_get_buf_active(disp);
    const int   pitch = db ? static_cast<int>(db->header.stride) : r.w * 2;
    SDL_UpdateTexture(s_tex, &r, px_map, pitch);
    SDL_RenderCopy(s_ren, s_tex, nullptr, nullptr);
    SDL_RenderPresent(s_ren);
    lv_display_flush_ready(disp);
}

std::uint32_t tick_cb() { return SDL_GetTicks(); }

void mouse_read(lv_indev_t*, lv_indev_data_t* d) {
    int x, y;
    const auto b = SDL_GetMouseState(&x, &y);
    d->point.x   = static_cast<std::int16_t>(x / 2);   // window is 2x zoom
    d->point.y   = static_cast<std::int16_t>(y / 2);
    d->state     = (b & SDL_BUTTON_LMASK) ? LV_INDEV_STATE_PRESSED
                                        : LV_INDEV_STATE_RELEASED;
}

} // namespace

void ui::port_init() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

    s_win = SDL_CreateWindow("coffee-scale sim", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, ui::hor_res * 2,
                             ui::ver_res * 2, SDL_WINDOW_SHOWN);
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(s_ren, ui::hor_res, ui::ver_res);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, ui::hor_res, ui::ver_res);

    lv_init();
    lv_tick_set_cb(&tick_cb);
    lv_display_t* disp = lv_display_create(ui::hor_res, ui::ver_res);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, &flush_cb);

    lv_indev_t* mouse = lv_indev_create();
    lv_indev_set_type(mouse, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse, &mouse_read);
}

std::uint32_t sim::sdl_poll() {
    std::uint32_t keys = key_none;
    SDL_Event     e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: keys |= key_quit; break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: keys |= key_quit; break;
                    case SDLK_t:      keys |= key_tare; break;
                    case SDLK_l:      keys |= key_long; break;
                    case SDLK_m:      keys |= key_mode; break;
                    default: break;
                }
                break;
            default: break;
        }
    }
    return keys;
}

void ui::display_power(bool) {}   // no panel/backlight in the sim
void ui::display_init() {}
void ui::render_pause(bool) {}
