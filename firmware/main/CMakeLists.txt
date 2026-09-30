idf_component_register(SRCS "app_main.cpp" "web_server.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES board bus scale_core nau7802 lis2dw12 tmp102 ui
                                esp_driver_i2c esp_driver_gpio esp_adc esp_timer
                                nvs_flash esp_http_server esp_wifi esp_netif
                                esp_event littlefs)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)

# Web bundle -> littlefs image, flashed alongside the app by idf.py flash.
# web/root is populated by web/pack.sh (gzipped index/app/wasm); if it is
# absent the image step is skipped and the HTTP handler serves a hint page.
set(web_root "${CMAKE_SOURCE_DIR}/web/root")
if(EXISTS "${web_root}/index.html")
    littlefs_create_partition_image(littlefs "${web_root}" FLASH_IN_PROJECT)
else()
    message(WARNING "web/root is empty — run web/pack.sh before flashing "
                    "if the on-device UI is wanted")
endif()
