package("lvgl")

    set_homepage("https://lvgl.io")
    set_description("Light and Versatile Graphics Library")
    set_license("MIT")

    add_urls("https://github.com/lvgl/lvgl/archive/refs/tags/$(version).tar.gz",
             "https://github.com/lvgl/lvgl.git")
    add_versions("v9.5.0", "34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6")

    -- Single source of truth for LVGL configuration: the project's config/lv_conf.h
    -- is used BOTH to build the library and (via -DLV_CONF_PATH) to compile
    -- consumer code, so they can never drift apart.
    add_configs("conf", {description = "Absolute path to lv_conf.h", type = "string"})

    add_deps("cmake")

    on_load(function (package)
        package:add("links", "lvgl")
        -- Installed layout is include/lvgl/{lvgl.h,src/...}; expose that dir so
        -- `#include "lvgl.h"` resolves, matching the ESP-IDF managed component,
        -- plus the parent so `<lvgl/lvgl.h>` also works.
        package:add("includedirs", "include/lvgl", "include")
        local conf = package:config("conf")
        if conf then
            package:add("defines", 'LV_CONF_PATH="' .. conf .. '"')
        end
    end)

    on_install(function (package)
        local conf = package:config("conf")
        assert(conf and os.isfile(conf),
            "lvgl: pass a valid lv_conf.h via add_requires(..., {configs = {conf = ...}})")
        -- lv_conf_internal.h resolves ../../lv_conf.h relative to src/, and the
        -- build also picks up a top-level lv_conf.h; plant both copies.
        os.cp(conf, "lv_conf.h")
        os.cp(conf, path.join("src", "lv_conf.h"))
        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:has_cfuncs("lv_version_info", {includes = "lvgl.h"}))
    end)
package_end()
