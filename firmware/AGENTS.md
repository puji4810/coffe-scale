# firmware — scale-adc-s3 (ESP32-S3 coffee scale)

Target board: `pcb/s3.1` (ESP32-S3-WROOM-1-N16R8, NAU7802 with **external
AVDD** on the VDD_ADC rail, LIS2DW12, TMP102, ST7789 SPI LCD, 2 buttons,
buzzer, VBAT sense).

## Two build systems (both required)

- **xmake** is the developer-facing organiser: host unit tests, C++23, SDL
  desktop simulator, package management (doctest, LVGL 9.5.0 via the local
  recipe in `xmake/lvgl_pkg.lua`).
- **ESP-IDF CMake** is kept as thin component `CMakeLists.txt` files (glob
  the same `src/`, `include/` dirs) because only `idf.py` can produce the
  flashable image (bootloader, partition table, ldgen).

## Commands

```bash
# host: configure / build / test / simulate
xmake f -m release            # once
xmake build
xmake run unit_tests          # doctest: 65 cases
xmake run sim                 # SDL window; T=tare L=long-tare M=mode Esc=quit

# firmware image
source /opt/esp-idf/export.sh
idf.py -B build-esp32s3 set-target esp32s3   # once
idf.py -B build-esp32s3 build                # -> coffee_scale.bin

# web WASM modules (needs emscripten + cmake on PATH)
web/wasm/build.sh             # -> web/dist/{scale_core,scale_screen}.{mjs,wasm}
cd web && node smoke.mjs      # headless check of both modules

# pack the on-device web root + build + flash (first flash after the
# partition change rewrites the table; littlefs auto-formats on first boot)
web/pack.sh                   # -> web/root (plain + .gz, baked into littlefs.bin)
idf.py -B build-esp32s3 build flash
# device: SoftAP "coffee-scale" (pass "coffeebrew") -> http://192.168.4.1
```

## Layout

- `components/scale_core` — pure C++23 logic: median + selectable LPF
  (adaptive EMA / Bessel-2 biquad / Savitzky–Golay), plateau snap
  (quiet median window — spread AND least-squares slope <= 0.5 g/s over
  the last `snap_window` samples, so slow pours don't qualify — plus LPF
  still trailing -> jump the filter state to the input mean; lagging
  filters only), stability, tare,
  two-point calibration, units, brew timer, regression flow rate,
  zero tracker, thermal drift model, tilt (IMU quiet + pitch/roll),
  `diag` telemetry snapshot + CSV format, `scale::app` state,
  display deadbands (0.05 g on `grams()`, 0.1 g/s on `flow_gps()` —
  pin sub-division noise to exact 0, kill the "-0.0" flicker) + a
  ±30 g/s flow clip (step loads otherwise report ~200 g/s while the
  regression window walks past the step) + a display quantiser with
  hysteresis (`display_grams()`/`display_value()`, 0.1 g steps that only
  re-round past half-step + 0.02 g; `grams()` stays continuous for
  flow/telemetry) + pour-lag compensation (display adds the
  regression-fit-vs-LPF lag back — the fit evaluated at the newest
  timestamp — gated on |flow| 0.3→1 g/s and only while the fit leads the
  filter along the flow direction) + a display latch
  (freezes the shown weight after 300 ms of system-stable OR 300 ms with
  the quantised readout itself calm — the latter keeps a bench fan's
  vibration from flickering the digit; the value-calm path is suppressed
  above |flow| 0.5 g/s; releases the moment the continuous value drifts
  past the same half-step + hysteresis bound, so a stale digit can't
  survive — e.g. the residual zero-track pulls back to 0;
  `latch_hold <= 0` disables). Flow
  regresses the median-domain weight over a fixed 1.0 s window (~0.85 s
  rise / ~0.9 s stop tail — the adaptive-window mechanism remains but
  ships disabled, min span == span: short windows spiked the readout at
  high pour rates). Unfed pipelines
  read 0, not the unprimed-filter phantom value. No HW deps.
  `scale::push(counts, now)` takes a real monotone timestamp — flow
  regression and the zero-track hold run on it, so dropped DRDY edges
  don't distort rates. `system_stable = loadcell_stable && IMU quiet`
  gates zero tracking; brew mode freezes the tracker entirely.
- `components/bus` — `bus::i2c_device` concept + `bus::i2c_dev_esp` (ESP-IDF
  i2c_master). Named `bus`, NOT `hal`: `hal` collides with ESP-IDF's own
  component and breaks the build.
- `components/{nau7802,lis2dw12,tmp102}` — header-only drivers, templated on
  `bus::i2c_device`, `std::expected` error returns.
- `components/board` — `board::pins` / `board::i2c_addr`, the only place bound
  to the PCB rev.
- `components/ui` — LVGL screens + `ui_port_esp.cpp` (ST7789 via esp_lcd) +
  `ui_port_sdl.cpp` (desktop).
- `main/app_main.cpp` — tasks: adc (DRDY ISR→notify, optional per-sample
  CSV over USB-serial-JTAG), accel (~20 Hz LIS2DW12 poll), button
  (debounce, short tare / long tare_long / mode), battery (ADC1_CH8 +
  chrg_stat + TMP102), console ('t' toggles CSV telemetry, 'z' = tare),
  power (idle->light sleep), lvgl.
  Power: after 5 min without "new activity" (weight step >4000 raw counts
  off a slow baseline, IMU motion >150 mg, button, console/web command —
  NOT just "number didn't change") the firmware blanks the LCD +
  backlight, stops SoftAP/httpd, powers down NAU7802 and light-sleeps.
  Wake sources: LIS2DW12 tap on INT1 (IO13, latched level — needs
  CTRL7.interrupts_enable + CTRL4.int1_tap + CTRL3.LIR, cleared by
  reading ALL_INT_SRC/TAP_SRC) and MODE button (IO18, low level).
  btn_tare (IO48) is NOT an RTC pin and cannot wake. Light sleep keeps
  RAM + LVGL + filters; the brew timer running suppresses auto-sleep.
- `tests/` — doctest + `mock_i2c` register-level driver tests.
- `sim/` — SDL2 LVGL simulator with synthetic weight signal.
- `web/` — self-hosted web UI. `wasm/build.sh` builds two ES6 modules into
  `web/dist/`: `scale_core` (embind `ScaleApp` around `scale::app` — same
  injected-ms `feed()` contract) and `scale_screen` (the real
  `ui::create`/`ui::update` + LVGL compiled with `config/lv_conf.h`,
  flushing RGB565→RGBA into a staging frame for canvas blit). `pack.sh`
  assembles `web/root/` (plain + .gz) which `main/CMakeLists.txt` bakes
  into `littlefs.bin` via `littlefs_create_partition_image(FLASH_IN_PROJECT)`.
  `index.html`/`app.js` = dev harness (demo pour + mirror mode);
  `smoke.mjs` = node smoke test.
- `main/web_server.cpp` — SoftAP `coffee-scale`/`coffeebrew` @192.168.4.1,
  littlefs mounted at `/littlefs`, statics prefer `<path>.gz` +
  `Content-Encoding: gzip`, unknown paths fall back to `/index.html`
  (SPA + captive-portal). `/ws` pushes the snapshot JSON at 20 Hz and
  accepts `{"cmd": tare|long|reset|mode|unit0|unit1|calzero|calspan:<g>|sleep}`
  (`reset` = brew timer back to 0:0.0, any mode)
  (cal* persist to NVS) — field names are the same wire ABI the wasm
  modules use (`snap, displayValue, batteryPct, charging`). Push
  enumerates clients via `httpd_get_client_list` +
  `httpd_ws_get_fd_info` — stateless, no fd bookkeeping.
- `web/` is also the standalone static app (same files): vendored uPlot
  chart (weight+flow dual axis), screen-mirror canvas, brew auto-record
  → jsonl download + replay, two-point cal wizard, demo mode. No CDN —
  works on device AND as a GitHub Pages deploy (`web/deploy-gh-pages.sh`;
  note https pages can't open ws:// to the LAN device — demo/replay only
  there).
- `partitions.csv` — dual OTA ~3.9 MB each + `littlefs` subtype 8.1 MB.
- `korvo1-remote/` — separate ESP-IDF project: ESP32-S31-Korvo-1 V1.1 +
  4.3" 800x480 RGB LCD sub-board acting as a touch remote. Joins the
  scale's SoftAP as STA and speaks the existing `/ws` JSON protocol —
  the scale needs no code changes. Requires IDF ≥6.1 (S31 unsupported on
  the 6.0.x used for the scale); own `sdkconfig.defaults`, own build dir
  (`idf.py -B build-s31`). LCD/touch/LVGL come from the
  `espressif/esp32_s31_korvo_1` BSP (touch is GT1151) — no pin map to
  maintain; LVGL calls must run under `bsp_display_lock/unlock`.
  Host preview: `xmake build -P korvo1-remote remote_sim` +
  `xmake run -P korvo1-remote remote_sim` (xmake treats `-P` as a global
  flag — it goes after the action). `sim/link_stub.cpp` plays a synthetic
  scale so buttons do real things; keys T/L/M/U, O toggles the offline
  overlay, Esc quits.

## Gotchas

- IDF 6: flash-size symbol is `ESPTOOLPY_FLASHSIZE_16MB` (not `*_16M`).
- Repo without commits breaks `git_describe` → `PROJECT_VER` is set in the
  top `CMakeLists.txt`.
- `esp32s3` oneshot ADC clk type is RTC (`ADC_RTC_CLK_SRC_DEFAULT`).
- `gpio_num_t` fields need `static_cast` from `board::pins` (plain `int`).
- IDF compiles components with `-Werror=missing-field-initializers`;
  designated initializers must follow declaration order.
- LCD reset is RC power-on only (no GPIO); ST7789 needs gap x=34, swap_xy,
  mirror(x) for 320x172 landscape on this module — verify on hardware.
- s3.1 powers NAU7802 AVDD externally (U7 HT7533 → VDD_ADC, shared with
  load-cell E+ and REFP): PU_CTRL AVDDS must stay 0. Use
  `nau7802_ldo::external` (the default), never a VLDO voltage.
- `/ws` needs `CONFIG_HTTPD_WS_SUPPORT=y` (in sdkconfig.defaults AND the
  generated sdkconfig — defaults only apply to a fresh sdkconfig).
- `esp_vfs_littlefs_conf_t` gained a `blockdev` member in IDF 6 — the
  designated init must list it (`nullptr`) before the flag bitfields.
- IDF 6 httpd: wildcard matching is `cfg.uri_match_fn =
  httpd_uri_match_wildcard`, not the old `uri_match_wildcard` bool.
