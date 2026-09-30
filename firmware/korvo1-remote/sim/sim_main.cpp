// Host preview of the korvo1-remote UI: 800x480 SDL window fed by the link
// stub. Mouse = touch (buttons really drive the stub scale); keys mirror the
// web UI shortcuts: T=tare L=timer M=mode U=unit O=toggle offline Esc=quit.

#include "link.hpp"
#include "ui.hpp"

#include <SDL.h>
#include <cstdio>

#include "lvgl.h"

namespace {

constexpr int kW = 800, kH = 480;

SDL_Window*   s_win;
SDL_Renderer* s_ren;
SDL_Texture*  s_tex;

constexpr std::size_t kBufPixels = kW * 40;
lv_color_t s_buf1[kBufPixels];
lv_color_t s_buf2[kBufPixels];

void flush_cb(lv_display_t* disp, const lv_area_t* area, std::uint8_t* px) {
    SDL_Rect r{area->x1, area->y1, area->x2 - area->x1 + 1,
               area->y2 - area->y1 + 1};
    const auto* db = lv_display_get_buf_active(disp);
    const int pitch = db ? static_cast<int>(db->header.stride) : r.w * 2;
    SDL_UpdateTexture(s_tex, &r, px, pitch);
    SDL_RenderCopy(s_ren, s_tex, nullptr, nullptr);
    SDL_RenderPresent(s_ren);
    lv_display_flush_ready(disp);
}

std::uint32_t tick_cb() { return SDL_GetTicks(); }

void mouse_read(lv_indev_t*, lv_indev_data_t* d) {
    int x, y;
    const auto b = SDL_GetMouseState(&x, &y);
    d->point.x = static_cast<std::int16_t>(x);
    d->point.y = static_cast<std::int16_t>(y);
    d->state = (b & SDL_BUTTON_LMASK) ? LV_INDEV_STATE_PRESSED
                                    : LV_INDEV_STATE_RELEASED;
}

} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    s_win = SDL_CreateWindow("korvo1-remote preview", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, kW, kH, SDL_WINDOW_SHOWN);
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED);
    if (!s_ren)
        s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(s_ren, kW, kH);
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGB565,
                              SDL_TEXTUREACCESS_STREAMING, kW, kH);

    lv_init();
    lv_tick_set_cb(&tick_cb);
    lv_display_t* disp = lv_display_create(kW, kH);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, &flush_cb);
    lv_indev_t* mouse = lv_indev_create();
    lv_indev_set_type(mouse, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse, &mouse_read);

    net::start();   // stub seeds battery/cal flags for the preview
    ui::create();

    // headless screenshot mode: REMOTE_SIM_SHOT=out.bmp renders ~30 frames
    // against the synthetic scale, dumps the composited frame, and exits.
    const char* shot = SDL_getenv("REMOTE_SIM_SHOT");
    if (shot) {
        for (int i = 0; i < 30; ++i) {
            ui::update();
            lv_timer_handler();
            SDL_Delay(16);
        }
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
            0, kW, kH, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surf && SDL_RenderReadPixels(s_ren, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                         surf->pixels, surf->pitch) == 0) {
            SDL_SaveBMP(surf, shot);
            std::printf("shot -> %s\n", shot);
        }
        SDL_FreeSurface(surf);
        SDL_Quit();
        return 0;
    }

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: quit = true; break;
                    case SDLK_t: net::send_cmd("tare"); break;
                    case SDLK_l: net::send_cmd("long"); break;
                    case SDLK_m: net::send_cmd("mode"); break;
                    case SDLK_u:
                        net::send_cmd(net::latest().unit == 0 ? "unit1"
                                                              : "unit0");
                        break;
                    case SDLK_o: net::sim_toggle_offline(); break;
                    default: break;
                }
            }
        }
        ui::update();
        lv_timer_handler();
        SDL_Delay(16);
    }
    SDL_Quit();
    return 0;
}
