#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "link.hpp"
#include "lvgl.h"

namespace {

// warm bench-instrument palette — crema amber on near-black
constexpr std::uint32_t kBg        = 0x0E0C0A;
constexpr std::uint32_t kPanel     = 0x191512;
constexpr std::uint32_t kLine      = 0x2E2620;
constexpr std::uint32_t kFg        = 0xF2EDE6;
constexpr std::uint32_t kDim       = 0x9A8F82;
constexpr std::uint32_t kAmber     = 0xE39A3B;
constexpr std::uint32_t kAmberLo   = 0xB0702A;   // pressed fill / dim accent
constexpr std::uint32_t kInk       = 0x171009;   // text on filled amber
constexpr std::uint32_t kGreen     = 0x4FBF7F;
constexpr std::uint32_t kRed       = 0xD9574A;
constexpr std::uint32_t kRedLo     = 0x3A1712;   // pressed fill on danger btn
constexpr std::uint32_t kDotOff    = 0x3A332C;
constexpr std::uint32_t kBtnBg     = 0x241F1A;
constexpr std::uint32_t kBtnHi     = 0x352C23;
constexpr std::uint32_t kBtnTop    = 0x3D352B;   // key-cap edge highlight

constexpr const char* kUnits[] = {"g", "oz"};
constexpr const char* kModes[] = {"WEIGH", "BREW"};

struct {
    lv_obj_t* weight;
    lv_obj_t* unit;
    lv_obj_t* mode;
    lv_obj_t* stable_dot;
    lv_obj_t* stable_txt;
    lv_obj_t* tare;
    lv_obj_t* timer;
    lv_obj_t* flow;
    lv_obj_t* batt_fill;
    lv_obj_t* batt_txt;
    lv_obj_t* level_dot;
    lv_obj_t* conn_dot;
    lv_obj_t* conn;
    lv_obj_t* offline;
    lv_obj_t* chart;
    lv_chart_series_t* ser_w;
    lv_chart_series_t* ser_f;
    lv_obj_t* timer_btn;
    lv_obj_t* timer_lbl;
    uint32_t  last_seq = 0;
    float     last_disp = 1e9f;   // dedup label writes
    float     last_flow = 1e9f;
    long      last_timer = -1;
    int       last_batt = -1;
    int       last_timer_state = -1;
    bool      last_chg = false;
} w;

// Shared press transition for the bottom-bar buttons — a fast ease-out on
// the surface/skin changes makes the touch read as a physical key dip.
lv_style_transition_dsc_t s_btn_tr;

void btn_transition_init() {
    static const lv_style_prop_t props[] = {
        LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, LV_STYLE_TRANSLATE_Y,
        LV_STYLE_PROP_INV};
    lv_style_transition_dsc_init(&s_btn_tr, props, lv_anim_path_ease_out,
                                 90, 0, nullptr);
}

lv_obj_t* mk_label(lv_obj_t* parent, const lv_font_t* font,
                   std::uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

lv_obj_t* mk_dot(lv_obj_t* parent, int size, std::uint32_t color) {
    lv_obj_t* d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, size, size);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    return d;
}

void on_btn_cmd(lv_event_t* e) {
    const char* c = static_cast<const char*>(lv_event_get_user_data(e));
    if (std::strcmp(c, "unit_toggle") == 0) {
        // the wire protocol wants the *target* unit, not a toggle
        net::send_cmd(net::latest().unit == 0 ? "unit1" : "unit0");
    } else {
        net::send_cmd(c);
    }
}

// CLEAR: wipe the local curve and reset the scale's brew timer.
void on_clear(lv_event_t*) {
    lv_chart_set_all_values(w.chart, w.ser_w, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(w.chart, w.ser_f, LV_CHART_POINT_NONE);
    lv_chart_refresh(w.chart);
    net::send_cmd("reset");
}

/// Bottom-bar key. `accent` colours the caption + the pressed border;
/// `filled` gives the primary action (TARE) a solid amber key, `danger`
/// swaps the pressed fill for a deep red instead of the neutral lift.
/// `cb`+`ud` run on CLICKED — most keys send a wire command via on_btn_cmd.
lv_obj_t* mk_btn_ex(lv_obj_t* parent, const char* txt, std::uint32_t accent,
                    bool filled, bool danger, lv_event_cb_t cb, void* ud) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, lv_pct(100));
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_transition(b, &s_btn_tr, LV_STATE_DEFAULT);
    if (filled) {
        lv_obj_set_style_bg_color(b, lv_color_hex(accent), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(kAmberLo),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, lv_color_hex(0xF6B45E), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(accent),
                                      LV_STATE_PRESSED);
    } else {
        lv_obj_set_style_bg_color(b, lv_color_hex(kBtnBg), 0);
        lv_obj_set_style_bg_color(
            b, lv_color_hex(danger ? kRedLo : kBtnHi), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, lv_color_hex(kBtnTop), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(accent),
                                      LV_STATE_PRESSED);
    }
    // the dip: drop 2 px while held
    lv_obj_set_style_translate_y(b, 2, LV_STATE_PRESSED);

    lv_obj_t* l = mk_label(b, &lv_font_montserrat_24,
                           filled ? kInk : accent);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

lv_obj_t* mk_btn(lv_obj_t* parent, const char* txt, const char* cmd,
                 std::uint32_t accent = kFg, bool filled = false,
                 bool danger = false) {
    return mk_btn_ex(parent, txt, accent, filled, danger, on_btn_cmd,
                     const_cast<char*>(cmd));
}

// Vertical strip of axis tick labels, one per horizontal grid line
// (SPACE_BETWEEN lands them on the 5 hdiv lines). `flush_end` right-aligns
// the left column against the plot, left-aligns the right one.
void mk_axis_col(lv_obj_t* parent, const char* const* labels, int n,
                 std::uint32_t color, bool flush_end) {
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, lv_pct(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    const lv_flex_align_t cross =
        flush_end ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START;
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, cross, cross);
    for (int i = 0; i < n; i++) {
        lv_obj_t* l = mk_label(col, &lv_font_montserrat_14, color);
        lv_label_set_text(l, labels[i]);
    }
}

uint32_t batt_color(int pct, bool charging) {
    if (charging) return kAmber;
    if (pct > 50) return kGreen;
    if (pct > 20) return kAmber;
    return kRed;
}

} // namespace

void ui::create() {
    btn_transition_init();

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(kFg), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // ---- header -------------------------------------------------------------
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, lv_pct(100), 52);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x140F0B), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(kLine), 0);
    lv_obj_set_style_pad_hor(hdr, 20, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* logo = mk_label(hdr, &lv_font_montserrat_28, kAmber);
    lv_label_set_text(logo, "coffee scale");
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, 0, 0);

    // header right cluster — a flex row so every label/icon reflows when
    // its neighbour's width changes (align_to is one-shot in LVGL 9 and
    // would leave the pieces frozen at create-time positions)
    lv_obj_t* hdr_r = lv_obj_create(hdr);
    lv_obj_remove_style_all(hdr_r);
    lv_obj_set_size(hdr_r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr_r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr_r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr_r, 10, 0);
    lv_obj_align(hdr_r, LV_ALIGN_RIGHT_MID, 0, 0);

    w.conn_dot = mk_dot(hdr_r, 8, kDotOff);
    w.conn = mk_label(hdr_r, &lv_font_montserrat_14, kDim);
    lv_label_set_text(w.conn, "offline");

    // battery icon + pct, rightmost
    lv_obj_t* bbox = lv_obj_create(hdr_r);
    lv_obj_remove_style_all(bbox);
    lv_obj_set_size(bbox, 26, 13);
    lv_obj_set_style_radius(bbox, 3, 0);
    lv_obj_set_style_border_width(bbox, 1, 0);
    lv_obj_set_style_border_color(bbox, lv_color_hex(kDim), 0);
    lv_obj_t* tip = lv_obj_create(bbox);
    lv_obj_remove_style_all(tip);
    lv_obj_set_size(tip, 2, 5);
    lv_obj_set_style_bg_color(tip, lv_color_hex(kDim), 0);
    lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);
    lv_obj_align(tip, LV_ALIGN_RIGHT_MID, 3, 0);
    w.batt_fill = lv_obj_create(bbox);
    lv_obj_remove_style_all(w.batt_fill);
    lv_obj_set_height(w.batt_fill, 7);
    lv_obj_set_style_bg_color(w.batt_fill, lv_color_hex(kGreen), 0);
    lv_obj_set_style_bg_opa(w.batt_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w.batt_fill, 2, 0);
    lv_obj_align(w.batt_fill, LV_ALIGN_LEFT_MID, 2, 0);

    w.batt_txt = mk_label(hdr_r, &lv_font_montserrat_14, kDim);
    lv_label_set_text(w.batt_txt, "");   // no "Text" flash before 1st frame

    // ---- left panel: vitals --------------------------------------------------
    lv_obj_t* left = lv_obj_create(scr);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, 352, 480 - 52 - 96 - 14);
    lv_obj_align(left, LV_ALIGN_TOP_LEFT, 16, 60);
    lv_obj_set_style_bg_color(left, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(left, 12, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_color(left, lv_color_hex(kLine), 0);
    lv_obj_set_style_pad_all(left, 20, 0);

    w.mode = mk_label(left, &lv_font_montserrat_14, kDim);
    lv_label_set_text(w.mode, "WEIGH");
    lv_obj_align(w.mode, LV_ALIGN_TOP_LEFT, 0, 0);

    // weight hero — flex row keeps the unit glued to the digits' baseline
    // no matter how wide the number grows ("150.0" vs "0.0")
    lv_obj_t* wrow = lv_obj_create(left);
    lv_obj_remove_style_all(wrow);
    lv_obj_set_size(wrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wrow, 8, 0);
    lv_obj_align(wrow, LV_ALIGN_LEFT_MID, 0, -34);
    w.weight = mk_label(wrow, &lv_font_montserrat_48, kFg);
    w.unit = mk_label(wrow, &lv_font_montserrat_24, kDim);
    lv_label_set_text(w.unit, "g");

    // status row: dot + word, then a TARED tag when set — same flex trick
    lv_obj_t* srow = lv_obj_create(left);
    lv_obj_remove_style_all(srow);
    lv_obj_set_size(srow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(srow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(srow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(srow, 10, 0);
    lv_obj_align(srow, LV_ALIGN_LEFT_MID, 0, 26);
    w.stable_dot = mk_dot(srow, 10, kDotOff);
    w.stable_txt = mk_label(srow, &lv_font_montserrat_14, kDim);
    lv_label_set_text(w.stable_txt, "settling");
    w.tare = mk_label(srow, &lv_font_montserrat_14, kDim);

    // timer + flow on the bottom edge of the panel
    w.timer = mk_label(left, &lv_font_montserrat_28, kAmber);
    lv_label_set_text(w.timer, "0:00.0");
    lv_obj_align(w.timer, LV_ALIGN_BOTTOM_LEFT, 0, -44);
    w.flow = mk_label(left, &lv_font_montserrat_24, kDim);
    lv_obj_align(w.flow, LV_ALIGN_BOTTOM_LEFT, 0, -10);

    // level bubble, bottom-right of the vitals panel
    lv_obj_t* ring = lv_obj_create(left);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 44, 44);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(kDim), 0);
    lv_obj_align(ring, LV_ALIGN_BOTTOM_RIGHT, 0, -8);
    w.level_dot = mk_dot(ring, 10, kDim);
    lv_obj_align(w.level_dot, LV_ALIGN_CENTER, 0, 0);

    // ---- right panel: chart with axis scales ---------------------------------
    // panel wraps [weight scale][chart][flow scale]; tick labels sit on the
    // 5 hdiv lines. Axis ranges chosen so labels come out round:
    // weight 0..400 g, flow 0..16 g/s (pushed x10 for sub-gram resolution).
    lv_obj_t* cwrap = lv_obj_create(scr);
    lv_obj_remove_style_all(cwrap);
    lv_obj_set_size(cwrap, 800 - 388, 480 - 52 - 96 - 14);
    lv_obj_align(cwrap, LV_ALIGN_TOP_RIGHT, -16, 60);
    lv_obj_set_style_bg_color(cwrap, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(cwrap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cwrap, 12, 0);
    lv_obj_set_style_border_width(cwrap, 1, 0);
    lv_obj_set_style_border_color(cwrap, lv_color_hex(kLine), 0);
    lv_obj_set_style_pad_hor(cwrap, 6, 0);
    lv_obj_set_style_pad_ver(cwrap, 8, 0);
    lv_obj_set_flex_flow(cwrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cwrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cwrap, 4, 0);

    static const char* const kWAx[] = {"400", "300", "200", "100", "0"};
    mk_axis_col(cwrap, kWAx, 5, kFg, /*flush_end=*/true);

    w.chart = lv_chart_create(cwrap);
    lv_obj_set_flex_grow(w.chart, 1);
    lv_obj_set_height(w.chart, lv_pct(100));
    lv_obj_set_style_bg_opa(w.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w.chart, 0, 0);
    lv_obj_set_style_pad_hor(w.chart, 2, 0);
    lv_obj_set_style_pad_ver(w.chart, 0, 0);
    lv_chart_set_type(w.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(w.chart, 400);          // 40 s @ 10 Hz push
    lv_chart_set_range(w.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 400);
    lv_chart_set_range(w.chart, LV_CHART_AXIS_SECONDARY_Y, 0, 160);
    // graticule: div lines ride on PART_MAIN, series width on PART_ITEMS
    lv_chart_set_div_line_count(w.chart, 5, 9);
    lv_obj_set_style_line_color(w.chart, lv_color_hex(0x241E18),
                                LV_PART_MAIN);
    lv_obj_set_style_line_width(w.chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(w.chart, 2, LV_PART_ITEMS);
    w.ser_w = lv_chart_add_series(w.chart, lv_color_hex(kFg),
                                  LV_CHART_AXIS_PRIMARY_Y);
    w.ser_f = lv_chart_add_series(w.chart, lv_color_hex(kAmber),
                                  LV_CHART_AXIS_SECONDARY_Y);

    static const char* const kFAx[] = {"16", "12", "8", "4", "0"};
    mk_axis_col(cwrap, kFAx, 5, kAmber, /*flush_end=*/false);

    // ---- bottom controls ------------------------------------------------------
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 800 - 32, 72);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(bar, 10, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x140F0B), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 12, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(kLine), 0);
    lv_obj_set_style_pad_ver(bar, 9, 0);
    lv_obj_set_style_pad_hor(bar, 10, 0);

    mk_btn(bar, "Tare",  "tare", kAmber, /*filled=*/true);
    w.timer_btn = mk_btn(bar, "Timer", "long", kFg);
    w.timer_lbl = lv_obj_get_child(w.timer_btn, 0);
    mk_btn(bar, "Mode",  "mode");
    // CLEAR wipes the curve locally and resets the brew timer on the scale
    mk_btn_ex(bar, "Clear", kFg, false, false, on_clear, nullptr);
    mk_btn(bar, "g / oz", "unit_toggle", kDim);
    mk_btn(bar, "Sleep",  "sleep", kRed, false, /*danger=*/true);

    // offline overlay — the scale kills its AP in low power, so this is a
    // normal resting state, not an error.
    w.offline = lv_obj_create(scr);
    lv_obj_remove_style_all(w.offline);
    lv_obj_set_size(w.offline, 420, 90);
    lv_obj_set_style_bg_color(w.offline, lv_color_hex(0x140F0B), 0);
    lv_obj_set_style_bg_opa(w.offline, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w.offline, 12, 0);
    lv_obj_set_style_border_width(w.offline, 1, 0);
    lv_obj_set_style_border_color(w.offline, lv_color_hex(kAmber), 0);
    lv_obj_center(w.offline);
    lv_obj_t* off_lbl = mk_label(w.offline, &lv_font_montserrat_24, kDim);
    lv_label_set_text(off_lbl, "scale asleep - tap scale to wake");
    lv_obj_center(off_lbl);
    lv_obj_add_flag(w.offline, LV_OBJ_FLAG_HIDDEN);
}

void ui::update() {
    std::uint32_t seq = 0;
    const net::snapshot s = net::latest(&seq);
    const bool on = net::online();

    lv_label_set_text(w.conn, on ? "live" : "offline");
    lv_obj_set_style_text_color(w.conn, lv_color_hex(on ? kGreen : kDim), 0);
    lv_obj_set_style_bg_color(w.conn_dot, lv_color_hex(on ? kGreen : kDotOff), 0);
    // flags API (not lv_obj_set_hidden) — the host sim builds LVGL 9.5
    if (on) lv_obj_add_flag(w.offline, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_remove_flag(w.offline, LV_OBJ_FLAG_HIDDEN);

    if (seq == w.last_seq) return;
    w.last_seq = seq;

    // skip label writes whose value didn't change — each set_text_fmt is a
    // heap alloc + invalidate + render, no point churning 20x/s on a latch
    if (s.display_value != w.last_disp) {
        w.last_disp = s.display_value;
        lv_label_set_text_fmt(w.weight, "%.1f",
                              static_cast<double>(s.display_value));
    }
    lv_label_set_text(w.unit, kUnits[s.unit & 1]);
    lv_label_set_text(w.mode, kModes[s.mode & 1]);

    lv_obj_set_style_bg_color(w.stable_dot,
        lv_color_hex(s.stable ? kGreen : kDotOff), 0);
    lv_label_set_text(w.stable_txt, s.stable ? "stable" : "settling");
    lv_obj_set_style_text_color(w.stable_txt,
        lv_color_hex(s.stable ? kGreen : kDim), 0);
    lv_label_set_text(w.tare, s.tared ? "tared" : "");

    if (s.timer_ms != w.last_timer) {
        w.last_timer = s.timer_ms;
        lv_label_set_text_fmt(w.timer, "%ld:%02ld.%ld", s.timer_ms / 60000,
                              s.timer_ms / 1000 % 60, s.timer_ms / 100 % 10);
        lv_obj_set_style_text_color(
            w.timer, lv_color_hex(s.timer_state == 1 ? kAmber : kDim), 0);
    }

    // TIMER key lights amber while the brew clock runs — same cue as the
    // timer readout, so the button reads as a stateful control.
    if (s.timer_state != w.last_timer_state) {
        w.last_timer_state = s.timer_state;
        const bool run = s.timer_state == 1;
        lv_obj_set_style_border_color(
            w.timer_btn, lv_color_hex(run ? kAmber : kBtnTop), 0);
        lv_obj_set_style_text_color(
            w.timer_lbl, lv_color_hex(run ? kAmber : kFg), 0);
    }

    if (s.flow_gps != w.last_flow) {
        w.last_flow = s.flow_gps;
        lv_label_set_text_fmt(w.flow, "%.1f g/s",
                              static_cast<double>(s.flow_gps));
        lv_obj_set_style_text_color(
            w.flow,
            lv_color_hex(std::fabs(s.flow_gps) >= 0.3f ? kAmber : kDim), 0);
    }

    if (s.battery_pct != w.last_batt || s.charging != w.last_chg) {
        w.last_batt = s.battery_pct;
        w.last_chg  = s.charging;
        if (s.battery_pct >= 0) {
            const int pct = std::min(s.battery_pct, 100);
            lv_obj_set_width(w.batt_fill, 2 + 20 * pct / 100);
            lv_obj_set_style_bg_color(
                w.batt_fill, lv_color_hex(batt_color(pct, s.charging)), 0);
            lv_label_set_text_fmt(w.batt_txt, "%s%d%%",
                                  s.charging ? "+" : "", pct);
        } else {
            lv_obj_set_width(w.batt_fill, 0);
            lv_label_set_text(w.batt_txt, "");
        }
    }

    const float tilt = std::max(std::fabs(s.pitch_deg), std::fabs(s.roll_deg));
    const int dx = std::clamp(static_cast<int>(s.roll_deg * 1.6f), -14, 14);
    const int dy = std::clamp(static_cast<int>(s.pitch_deg * 1.6f), -14, 14);
    lv_obj_align(w.level_dot, LV_ALIGN_CENTER, dx, dy);
    lv_obj_set_style_bg_color(
        w.level_dot,
        lv_color_hex(!s.stable ? kDotOff : tilt < 2.f ? kGreen : kAmber), 0);

    // push to the chart at ~10 Hz — halving the render load of the biggest
    // invalidated region buys the PSRAM bus slack for the LCD DMA
    if (seq % 2 == 0) {
        lv_chart_set_next_value(w.chart, w.ser_w,
                                static_cast<int32_t>(s.grams));
        lv_chart_set_next_value(w.chart, w.ser_f,
                                static_cast<int32_t>(s.flow_gps * 10));
    }
}
