set_project("korvo1-remote")
set_version("0.1.0")

set_languages("c++23")
set_policy("build.warning", true)
add_rules("mode.debug", "mode.release")

-- Same LVGL recipe/config as the parent project: one lv_conf.h governs both
-- the host preview and the on-target build.
includes("../xmake/lvgl_pkg.lua")
add_requires("libsdl2")
add_requires("lvgl", {configs = {
    conf = path.join(os.projectdir(), "..", "config", "lv_conf.h"),
}})

-- Host preview of the remote UI (800x480 SDL window, mouse = touch).
-- main/link.cpp is ESP-only; sim/link_stub.cpp feeds a synthetic pour.
target("remote_sim")
    set_kind("binary")
    set_default(true)
    add_files("main/ui.cpp", "sim/sim_main.cpp", "sim/link_stub.cpp")
    add_includedirs("main", "../components/scale_proto/include")
    add_packages("lvgl", "libsdl2")
