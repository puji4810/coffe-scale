idf_component_register(SRCS "app_main.cpp" "link.cpp" "display.cpp" "ui.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES lvgl esp_timer bt scale_proto nvs_flash
                                espressif__esp32_s31_korvo_1
                       PRIV_REQUIRES espressif__esp_lvgl_port
                                espressif__esp_lcd_touch_gt1151
                                esp_driver_i2s esp_driver_gpio)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
