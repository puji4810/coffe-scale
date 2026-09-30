# Thin ESP-IDF project wrapper.
#
# xmake (xmake.lua) is the project organiser for host development: component
# source lists live in each component's xmake.lua and the CMakeLists here only
# glob the same directories, so the two can not drift. `idf.py build` is kept
# for producing the actual esp32s3 image (bootloader, partition table, ldgen).
cmake_minimum_required(VERSION 3.16)

# No git tags yet; set the version explicitly so git_describe is skipped.
set(PROJECT_VER "0.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(coffee_scale)
