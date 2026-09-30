#!/usr/bin/env bash
# Build the web WASM modules:
#   dist/scale_core.mjs   — scale::app logic (header-only scale_core), the
#                           shared brain for any JS frontend
#   dist/scale_screen.mjs — the real LVGL ui::create/update code running under
#                           Emscripten; blit its framebuffer to a <canvas> for
#                           a pixel-faithful preview/mirror of the ST7789
#
# LVGL comes from the repo's managed component (same version the target
# links) and is compiled with the same config/lv_conf.h via LV_CONF_PATH,
# so the preview cannot drift from the device rendering.
#
# Requires: emscripten on PATH (emcc/em++/emcmake/emmake), cmake, ninja|make.

set -euo pipefail
cd "$(dirname "$0")"
WASM_DIR=$PWD
FW=$(cd ../.. && pwd)
LVGL=$FW/managed_components/lvgl__lvgl
CONF=$FW/config/lv_conf.h
CORE_INC=$FW/components/scale_core/include
UI_DIR=$FW/components/ui
OUT=$WASM_DIR/../dist
LVGL_BUILD=$FW/build/wasm/lvgl
LIBLVGL=$LVGL_BUILD/lib/liblvgl.a

mkdir -p "$OUT" "$LVGL_BUILD"

# --- LVGL static lib (incremental via cmake) -------------------------------
# LVGL's cmake insists on lv_conf.h inside its tree (same copies the xmake
# recipe plants); still the project's config/lv_conf.h stays the source of
# truth — it is copied in on every build, never edited there.
cp "$CONF" "$LVGL/lv_conf.h"
cp "$CONF" "$LVGL/src/lv_conf.h"
emcmake cmake -S "$LVGL" -B "$LVGL_BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLV_CONF_PATH="$CONF" \
    -DLV_CONF_BUILD_DISABLE_EXAMPLES=ON \
    -DLV_CONF_BUILD_DISABLE_DEMOS=ON \
    -DLV_CONF_BUILD_DISABLE_THORVG_INTERNAL=ON
emmake cmake --build "$LVGL_BUILD" -j"$(nproc)"

# --- module flags -----------------------------------------------------------
OUT_FLAGS=(
    -O2 -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sFILESYSTEM=0
)
CORE_FLAGS=(-std=c++23 -DNDEBUG -lembind -I"$CORE_INC")

em++ "${CORE_FLAGS[@]}" "${OUT_FLAGS[@]}" \
    "$WASM_DIR/bindings_core.cpp" -o "$OUT/scale_core.js"

em++ "${CORE_FLAGS[@]}" "${OUT_FLAGS[@]}" \
    "-DLV_CONF_PATH=\"$CONF\"" -I"$LVGL" \
    -I"$UI_DIR/include" -I"$WASM_DIR" \
    "$WASM_DIR/bindings_screen.cpp" "$WASM_DIR/ui_port_wasm.cpp" \
    "$UI_DIR/src/scale_ui.cpp" "$LIBLVGL" \
    -o "$OUT/scale_screen.js"

# .mjs so browsers AND node treat them as ES modules; the .wasm payloads keep
# their names and sit alongside.
mv -f "$OUT/scale_core.js" "$OUT/scale_core.mjs"
mv -f "$OUT/scale_screen.js" "$OUT/scale_screen.mjs"

ls -la "$OUT"
