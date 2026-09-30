/**
 * lv_conf.h — host/sim build of LVGL 9.
 *
 * Only overrides are listed; lv_conf_internal.h supplies defaults for
 * everything else. Keep in sync with the LV_* sdkconfig entries used by the
 * ESP-IDF build (see firmware/sdkconfig.defaults).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16                 /* ST7789 is RGB565 */

#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (64U * 1024U)

#define LV_USE_OS 0

#define LV_USE_FLOAT 1                    /* needed for "%.1f" label formats */

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LOG 0

/* No drivers/demos/examples in the host build — the sim supplies its own
 * SDL flush backend so the same lv_conf works for lib + app. */
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif /* LV_CONF_H */
