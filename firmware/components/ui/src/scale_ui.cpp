#include "ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "lvgl.h"

namespace {

struct Widgets {
    lv_obj_t* weight;
    lv_obj_t* unit_lbl;
    lv_obj_t* mode;
    lv_obj_t* stable_dot;
    lv_obj_t* tare_lbl;
    lv_obj_t* timer;
    lv_obj_t* batt;        // "%" text, left of the icon
    lv_obj_t* batt_box;    // icon: body + fill + tip
    lv_obj_t* batt_fill;
    lv_obj_t* flow;        // bottom-right pour rate
    lv_obj_t* level_dot;   // bubble inside the top-center ring
};

Widgets w;

void (*s_refresh)(void*) = nullptr;
void*  s_refresh_ctx     = nullptr;

constexpr std::uint32_t kBg     = 0x0B0B0B;
constexpr std::uint32_t kFg     = 0xF5F5F5;
constexpr std::uint32_t kDim    = 0x8A8A8A;
constexpr std::uint32_t kAmber  = 0xE8A33D;
constexpr std::uint32_t kGreen  = 0x3DD68C;
constexpr std::uint32_t kRed    = 0xE5534B;
constexpr std::uint32_t kDotOff = 0x3A3A3A;

/// Battery fill colour: green >50%, amber 20–50% or charging, red below.
std::uint32_t batt_color(int pct, bool charging) {
    if (charging) return kAmber;
    if (pct > 50) return kGreen;
    if (pct > 20) return kAmber;
    return kRed;
}

} // namespace

void ui::set_refresh(void (*fn)(void*), void* ctx) {
    s_refresh     = fn;
    s_refresh_ctx = ctx;
}

void ui::housekeeping() {
    if (s_refresh) {
        s_refresh(s_refresh_ctx);
    }
}

void ui::create() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(kFg), 0);

    // top-left: mode
    w.mode = lv_label_create(scr);
    lv_obj_set_style_text_font(w.mode, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w.mode, lv_color_hex(kDim), 0);
    lv_obj_align(w.mode, LV_ALIGN_TOP_LEFT, 8, 6);

    // top-centre: level bubble — a ring with a dot that walks with tilt.
    // Tilt matters to a load cell: off-level reads light.
    lv_obj_t* ring = lv_obj_create(scr);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 22, 22);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(kDim), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_50, 0);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 8);

    w.level_dot = lv_obj_create(ring);
    lv_obj_remove_style_all(w.level_dot);
    lv_obj_set_size(w.level_dot, 6, 6);
    lv_obj_set_style_radius(w.level_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(w.level_dot, lv_color_hex(kDim), 0);
    lv_obj_set_style_bg_opa(w.level_dot, LV_OPA_COVER, 0);
    lv_obj_align(w.level_dot, LV_ALIGN_CENTER, 0, 0);

    // top-right: battery icon + % text. Icon = body outline + fill + tip.
    w.batt = lv_label_create(scr);
    lv_obj_set_style_text_font(w.batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w.batt, lv_color_hex(kDim), 0);
    lv_obj_align(w.batt, LV_ALIGN_TOP_RIGHT, -40, 6);

    w.batt_box = lv_obj_create(scr);
    lv_obj_remove_style_all(w.batt_box);
    lv_obj_set_size(w.batt_box, 26, 13);
    lv_obj_align(w.batt_box, LV_ALIGN_TOP_RIGHT, -8, 6);

    lv_obj_t* body = lv_obj_create(w.batt_box);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 20, 11);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(kDim), 0);
    lv_obj_align(body, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* tip = lv_obj_create(w.batt_box);
    lv_obj_remove_style_all(tip);
    lv_obj_set_size(tip, 2, 5);
    lv_obj_set_style_bg_color(tip, lv_color_hex(kDim), 0);
    lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);
    lv_obj_align(tip, LV_ALIGN_LEFT_MID, 21, 0);

    w.batt_fill = lv_obj_create(w.batt_box);
    lv_obj_remove_style_all(w.batt_fill);
    lv_obj_set_size(w.batt_fill, 16, 7);
    lv_obj_set_style_radius(w.batt_fill, 1, 0);
    lv_obj_set_style_bg_color(w.batt_fill, lv_color_hex(kGreen), 0);
    lv_obj_set_style_bg_opa(w.batt_fill, LV_OPA_COVER, 0);
    lv_obj_align(w.batt_fill, LV_ALIGN_LEFT_MID, 2, 0);

    // center: weight digits + unit
    w.weight = lv_label_create(scr);
    lv_obj_set_style_text_font(w.weight, &lv_font_montserrat_48, 0);
    lv_label_set_text(w.weight, "---.-");
    lv_obj_align(w.weight, LV_ALIGN_RIGHT_MID, -56, -12);

    w.unit_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(w.unit_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(w.unit_lbl, lv_color_hex(kDim), 0);
    lv_label_set_text(w.unit_lbl, "g");
    lv_obj_align(w.unit_lbl, LV_ALIGN_RIGHT_MID, -10, 4);

    // left: stability dot + tare marker
    w.stable_dot = lv_obj_create(scr);
    lv_obj_set_size(w.stable_dot, 12, 12);
    lv_obj_set_style_radius(w.stable_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(w.stable_dot, 0, 0);
    lv_obj_set_style_bg_color(w.stable_dot, lv_color_hex(kDotOff), 0);
    lv_obj_align(w.stable_dot, LV_ALIGN_LEFT_MID, 12, -12);

    w.tare_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(w.tare_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w.tare_lbl, lv_color_hex(kDim), 0);
    lv_obj_align(w.tare_lbl, LV_ALIGN_LEFT_MID, 8, 8);

    // bottom-left: brew timer
    w.timer = lv_label_create(scr);
    lv_obj_set_style_text_font(w.timer, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(w.timer, lv_color_hex(kAmber), 0);
    lv_obj_align(w.timer, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_label_set_text(w.timer, "0:00.0");

    // bottom-right: flow rate
    w.flow = lv_label_create(scr);
    lv_obj_set_style_text_font(w.flow, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w.flow, lv_color_hex(kDim), 0);
    lv_obj_align(w.flow, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
}

void ui::update(const model& m) {
    lv_label_set_text_fmt(w.weight, "%.1f", static_cast<double>(m.display_value));
    lv_label_set_text(w.unit_lbl, scale::label(m.snap.u));
    lv_label_set_text(w.mode, m.snap.m == scale::app::mode::brew ? "BREW" : "WEIGH");

    lv_obj_set_style_bg_color(w.stable_dot,
                            lv_color_hex(m.snap.stable ? kGreen : kDotOff), 0);
    lv_label_set_text(w.tare_lbl, m.snap.tared ? "TARE" : "");

    const long ms = static_cast<long>(m.snap.timer_elapsed.count());
    lv_label_set_text_fmt(w.timer, "%ld:%02ld.%ld", ms / 60000, ms / 1000 % 60,
                          ms / 100 % 10);
    lv_obj_set_style_text_color(
        w.timer,
        lv_color_hex(m.snap.timer_state == scale::brew_timer::state::running ? kAmber
                                                                           : kDim),
        0);

    lv_label_set_text_fmt(w.flow, "%.1f g/s",
                          static_cast<double>(m.snap.flow_gps));
    lv_obj_set_style_text_color(
        w.flow,
        lv_color_hex(std::fabs(m.snap.flow_gps) >= 0.3f ? kAmber : kDim), 0);

    // Level bubble: roll -> x, pitch -> y, ~0.8 px/deg clamped to the ring.
    // Only meaningful while still — dim it when the unit is moving.
    const float tilt = std::max(std::fabs(m.snap.pitch_deg),
                                std::fabs(m.snap.roll_deg));
    const int dx = std::clamp(static_cast<int>(m.snap.roll_deg * 0.8f), -7, 7);
    const int dy = std::clamp(static_cast<int>(m.snap.pitch_deg * 0.8f), -7, 7);
    lv_obj_align(w.level_dot, LV_ALIGN_CENTER, dx, dy);
    lv_obj_set_style_bg_color(
        w.level_dot,
        lv_color_hex(!m.snap.stable ? kDotOff : tilt < 2.f ? kGreen : kAmber), 0);

    if (m.battery_pct >= 0) {
        lv_label_set_text_fmt(w.batt, "%s%d%%",
                              m.charging ? "+" : "", m.battery_pct);
        lv_obj_set_width(w.batt_fill,
                         std::max(2, 16 * std::min(m.battery_pct, 100) / 100));
        lv_obj_set_style_bg_color(
            w.batt_fill, lv_color_hex(batt_color(m.battery_pct, m.charging)), 0);
        lv_obj_remove_flag(w.batt_box, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(w.batt, "");
        lv_obj_add_flag(w.batt_box, LV_OBJ_FLAG_HIDDEN);
    }
}
