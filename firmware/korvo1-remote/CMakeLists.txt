# ESP32-S31-Korvo-1 V1.1 remote display/controller for the coffee scale.
# Standalone ESP-IDF project — separate target (esp32s31), own build dir.
#
# Requires ESP-IDF >= 6.1 (S31 SoC support is not in 6.0.x).
#
#   source /opt/esp-idf/export.sh          # needs a 6.1+ checkout
#   idf.py -B build-s31 set-target esp32s31
#   idf.py -B build-s31 build flash monitor
#
# LCD/touch/pin-map all come from the espressif/esp32_s31_korvo_1 BSP —
# bsp_display_start() does panel + GT1151 + LVGL in one call.
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.0")
# scale_proto lives in the parent firmware project — the wire ABI is
# shared verbatim between scale, remote and the web WASM bindings.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components/scale_proto")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(korvo1_remote)
