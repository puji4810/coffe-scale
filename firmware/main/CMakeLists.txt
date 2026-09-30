idf_component_register(SRCS "app_main.cpp" "ble_link.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES board bus scale_core scale_proto nau7802 lis2dw12
                                tmp102 ui bt
                                esp_driver_i2c esp_driver_gpio esp_adc esp_timer
                                nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
