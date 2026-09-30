file(GLOB srcs "src/*.cpp")
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl esp_lcd esp_timer
                                esp_driver_spi esp_driver_gpio
                                scale_core board)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
