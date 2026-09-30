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
xmake run unit_tests          # doctest: 73 cases
xmake run sim                 # SDL window; T=tare L=long-tare M=mode Esc=quit

# firmware image
source /opt/esp-idf/export.sh
idf.py -B build-esp32s3 set-target esp32s3   # once
idf.py -B build-esp32s3 build                # -> coffee_scale.bin

# web WASM modules (needs emscripten + cmake on PATH)
web/wasm/build.sh             # -> web/dist/{scale_core,scale_screen}.{mjs,wasm}
cd web && node smoke.mjs      # headless check of both modules + proto bindings
cd web && node ble_test.mjs   # ScaleLink reconnect/write-queue tests
web/site.sh _site             # assemble the GitHub Pages site from dist/

# firmware image + flash
idf.py -B build-esp32s3 build flash
# the web app is a static site (GitHub Actions → Pages); connect over
# Web Bluetooth ("Connect scale" button), Chrome/Edge only
```

## Layout

- `components/scale_core` — pure C++23 logic: median + selectable LPF
  (adaptive EMA / Bessel-2 biquad / Savitzky–Golay), plateau snap
  (quiet median window — spread AND least-squares slope <= 0.5 g/s over
  the last `snap_window` samples, so slow pours don't qualify — plus LPF
  still trailing -> jump the filter state to the input mean; lagging
  filters only), stability, tare,
  two-point calibration, units, brew timer, Kalman flow rate,
  zero tracker, thermal drift model, tilt (IMU quiet + pitch/roll),
  `diag` telemetry snapshot + CSV format, `scale::app` state,
  display deadbands (0.05 g on `grams()`, 0.3 g/s on `flow_gps()` —
  pin sub-division noise to exact 0, kill the "-0.0" flicker) + a
  ±30 g/s flow clip + a display quantiser with
  hysteresis (`display_grams()`/`display_value()`, 0.1 g steps that only
  re-round past half-step + 0.02 g; `grams()` stays continuous for
  flow/telemetry) + pour-lag compensation (display adds the
  Kalman-weight-vs-LPF lag back, gated on |flow| 0.3→1 g/s and only while
  the estimate leads the filter along the flow direction) + a display latch
  (freezes the shown weight after 300 ms of system-stable OR 300 ms with
  the quantised readout itself calm — the latter keeps a bench fan's
  vibration from flickering the digit; the value-calm path is suppressed
  above |flow| 0.5 g/s; releases the moment the continuous value drifts
  past the same half-step + hysteresis bound, so a stale digit can't
  survive — e.g. the residual zero-track pulls back to 0;
  `latch_hold <= 0` disables). Flow (`flow_kf`) is a constant-velocity
  Kalman filter on the median-domain weight, observed on two channels:
  `pre` (2x cascaded 8 Hz Butterworth) feeds the Kalman measurement,
  `fast` (single 16 Hz Butterworth) feeds detection only. Impact and
  tracking are separate evidence paths: a |innovation| trip (4 g on
  pre, 3 g on fast), a nonlinear windowed-mean jump, or a clean fitted
  slope past 22 g/s contradicting the state while un-boosted (blind
  tracking must not chase what no pour sustains) gates — rewind
  0.1 s, hold the captured flow decaying to 0 over 1.0 s, and after a
  minimum hold re-anchor once the trailing 0.3 s fit AND its newest
  half are calm (<1.5 g / ~1.05 g residual); the resume flow is the
  half-window slope when it's flat, or when the WHOLE gate window is
  one clean ramp agreeing with it and the oscillation streak is quiet,
  else held-or-0 — pouring through a gate keeps pouring, a stopped
  pour doesn't resurrect stale flow, a sustained wiggle can't adopt
  its instantaneous slope. Tracking speed is dynamic, never via
  configure() (that resets): same-sign innovation persistence + clean
  window slope evidence snaps f_ to the fit (bounded, damped ~15%,
  steep claims need longer proof) and runs q_boost until settled,
  then a mid-level q_track rides a confirmed slope for ~0.3 s; the
  boosted flow stays capped to the shallower adjacent-window slope so
  post-boost momentum can't overshoot. Slope-sign flips and a growing
  hold-off suppress re-entry so handling wiggles can't latch.
  Boost aborts into the gate when the signal is already flat — a
  gently-placed mass, not a pour. IMU |delta| EMA only vetoes boost.
  A dt >40 ms re-primes the prefilters and drops boost/track evidence.
  Stored times rebase cleanly: elapsed math tolerates values crossing
  zero and "never" sentinels sit at -1e9, never -1.
  Unfed pipelines
  read 0, not the unprimed-filter phantom value. No HW deps.
  `scale::push(counts, now)` takes a real monotone timestamp — the Kalman
  prediction and the zero-track hold run on it, so dropped DRDY edges
  don't distort rates. `system_stable = loadcell_stable && IMU quiet`
  gates zero tracking; brew mode freezes the tracker entirely.
- `components/scale_proto` — header-only wire ABI (`proto.hpp`): GATT
  UUIDs, the 20-byte little-endian state frame, and the command encoding.
  Shared by firmware, the WASM bindings and (later) the Korvo remote —
  no HW deps, C++23 std only.
- `components/bus` — `bus::i2c_device` concept + `bus::i2c_dev_esp` (ESP-IDF
  i2c_master). Named `bus`, NOT `hal`: `hal` collides with ESP-IDF's own
  component and breaks the build.
- `components/{nau7802,lis2dw12,tmp102}` — header-only drivers, templated on
  `bus::i2c_device`, `std::expected` error returns.
- `components/board` — `board::pins` / `board::i2c_addr`, the only place bound
  to the PCB rev.
- `components/ui` — LVGL screens + `ui_port_esp.cpp` (ST7789 via esp_lcd) +
  `ui_port_sdl.cpp` (desktop).
- `main/app_main.cpp` — tasks: adc (DRDY ISR→notify with the edge's
  esp_timer captured in the ISR — the stored low word MUST be read
  before esp_timer_get_time() or a mid-sequence edge unwraps ~71.6 min
  backwards — notification is only a wake hint: CR is verified before
  every read since NAU7802 latches the last conversion on a premature
  read, ~2-period 30 ms timeout that falls back to polling the CR bit,
  timestamps clamped non-decreasing for the estimator, model fed before
  the optional per-sample CSV so logging can't delay it, i2c/feed/
  interval stats logged every ~20 s, and BLE cal writes to NVS outside
  the model lock), accel (100 Hz LIS2DW12 poll, feed_accel/motion
  decimated to 20 Hz), button
  (debounce, short tare / long tare_long / mode), battery (ADC1_CH8 +
  chrg_stat + TMP102), console ('t' toggles CSV telemetry, 'z' = tare,
  'r' toggles the raw capture stream — `W`/`A`/`E` lines for
  `tools/capture.py` + `tools/replay`, plus `T,<t_us>,<tmp102_c>` and
  `C,<t_us>,<chip_c>` on the ~2 s battery_task cadence),
  power (idle->light sleep), lvgl.
  Power: after 5 min without "new activity" (weight step >4000 raw counts
  off a slow baseline, IMU motion >150 mg, button, console/BLE command —
  NOT just "number didn't change") the firmware blanks the LCD +
  backlight, stops BLE, powers down NAU7802 and light-sleeps.
  Wake sources: LIS2DW12 tap on INT1 (IO13, latched level — needs
  CTRL7.interrupts_enable + CTRL4.int1_tap + CTRL3.LIR, cleared by
  reading ALL_INT_SRC/TAP_SRC) and MODE button (IO18, low level).
  btn_tare (IO48) is NOT an RTC pin and cannot wake. Light sleep keeps
  RAM + LVGL + filters; the brew timer running suppresses auto-sleep.
- `tests/` — doctest + `mock_i2c` register-level driver tests.
- `sim/` — SDL2 LVGL simulator with synthetic weight signal.
- `main/ble_link.cpp` — NimBLE peripheral "coffee-scale". GAP: flags +
  service UUID in the adv packet, complete name in the scan response;
  100 ms interval for 30 s after start/disconnect then 500 ms; keeps
  advertising while connections < `BT_NIMBLE_MAX_CONNECTIONS` (3, so a
  browser and the Korvo can share). GATT service `c0ffee00-…-8c01`:
  state char `…8c02` (read + notify, 20-byte `proto` frame pushed at
  20 Hz by a dedicated task to each subscribed conn via
  `ble_gatts_notify_custom`), command char `…8c03` (write +
  write-no-response → `proto::decode_command` → app callbacks; bad
  frames → `BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN`). Open — no
  pairing/bonding. `ble::stop()` tears the whole stack down —
  `nimble_port_stop` → `nimble_port_freertos_deinit` →
  `nimble_port_deinit` — because the modem domain powers down in light
  sleep anyway; re-init cycles are supported on 6.0.2 (nimble_port only
  releases CLASSIC_BT memory). `ble::start()` re-inits everything.
  Wire ABI lives in `components/scale_proto/proto.hpp`.
- `web/` — static web UI (GitHub Pages via `.github/workflows/pages.yml`;
  `site.sh <out>` assembles it from `dist/`). `wasm/build.sh` builds two
  ES6 modules into `web/dist/`: `scale_core` (embind `ScaleApp` around
  `scale::app` — same injected-ms `feed()` contract) and `scale_screen`
  (the real `ui::create`/`ui::update` + LVGL compiled with
  `config/lv_conf.h`, flushing RGB565→RGBA into a staging frame for
  canvas blit; also exports `decodeFrame`/`encodeCommand`/`bleUuids`
  straight from scale_proto). `ble.js` = DOM-free Web Bluetooth
  `ScaleLink` (requestDevice picker, `getDevices`/`watchAdvertisements`
  auto-connect where the browser allows it, infinite 1→10 s backoff
  reconnect on link loss, serialized writes). `app.js` wires it to the
  mirror canvas + controls; vendored uPlot chart, brew auto-record →
  jsonl download + replay, two-point cal wizard. `smoke.mjs` +
  `ble_test.mjs` = node tests. Pages setup: repo Settings → Pages →
  Source: GitHub Actions.
- `partitions.csv` — dual OTA ~3.9 MB each; the `littlefs` slot is kept
  only because shrinking the table would move NVS (calibration must
  survive) — nothing mounts it.
- `korvo1-remote/` — separate ESP-IDF project: ESP32-S31-Korvo-1 V1.1 +
  4.3" 800x480 RGB LCD sub-board acting as a touch remote. It is a NimBLE
  **central**: passive scan for `proto::kServiceUuid128` in adv packets →
  connect (30–50 ms itvl, 4 s supervision) → discover service/chars/CCCD →
  subscribe → `proto::decode` each notify into `net::snapshot`; any
  discovery error or disconnect rescans forever. Commands go out via
  `ble_gattc_write_no_rsp_flat` (`net::send(proto::command)`). Pulls
  `scale_proto` via `EXTRA_COMPONENT_DIRS` in its top CMakeLists — the ABI
  is shared verbatim. Requires IDF ≥6.1 (S31 unsupported on the 6.0.x used
  for the scale) and **every idf.py call needs `--preview`**; own
  `sdkconfig.defaults`, own build dir (`idf.py --preview -B build-s31`).
  LCD/touch/LVGL come from the `espressif/esp32_s31_korvo_1` BSP (touch is
  GT1151) — no pin map to maintain; LVGL calls must run under
  `bsp_display_lock/unlock`.
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
- BT Kconfig symbol names drift between IDF 6.0.2 and master: controller
  sleep is `BT_CTRL_MODEM_SLEEP` on 6.0.2 vs `BT_CTRL_SLEEP_ENABLE` on
  master, controller mode is `BT_CTRL_MODE_EFF`/`HCI_MODE_VHCI` vs
  `BTDM_CTRL_MODE_*`. The NimBLE-layer names (`BT_NIMBLE_ROLE_*`,
  `BT_NIMBLE_MAX_CONNECTIONS`, `BT_NIMBLE_ATT_PREFERRED_MTU`) are stable.
- ESP32-S31 (korvo1-remote) is a preview target — every `idf.py` call
  needs `--preview`.
- `sdkconfig` is committed, so `sdkconfig.defaults` alone changes nothing
  — update both, then `idf.py -B build-esp32s3 reconfigure`.
- TMP102 is NOT fitted on the s3.1 build — the firmware probes it once at
  boot, logs "TMP102 not fitted — thermal comp off" and never reads it
  again (thermal compensation stays off). Boards that have it get the
  full path.
