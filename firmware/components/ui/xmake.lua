target("ui")
    set_kind("static")
    add_deps("scale_core", "board")
    add_packages("lvgl")
    add_includedirs("include", {public = true})
    -- ui_port_esp.cpp is ESP-only; the sim supplies its own port.
    add_files("src/scale_ui.cpp")
