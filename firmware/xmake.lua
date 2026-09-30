set_project("coffee-scale-fw")
set_version("0.1.0")

set_languages("c++23")
set_policy("build.warning", true)
add_rules("mode.debug", "mode.release")

-- In-tree package recipe: lvgl built with the project's config/lv_conf.h as
-- the single configuration source (see xmake/lvgl_pkg.lua).
includes("xmake/lvgl_pkg.lua")

add_requires("doctest")

option("ui_preview", {default = true, description = "Build the SDL desktop UI preview (pulls lvgl + sdl2)"})
if has_config("ui_preview") then
    add_requires("libsdl2")
    add_requires("lvgl", {configs = {
        conf = path.join(os.projectdir(), "config", "lv_conf.h"),
    }})
end

includes("components/board")
includes("components/scale_proto")
includes("components/bus")
includes("components/scale_core")
includes("components/nau7802")
includes("components/lis2dw12")
includes("components/tmp102")
includes("tests")

if has_config("ui_preview") then
    includes("components/ui")
    includes("sim")
end
