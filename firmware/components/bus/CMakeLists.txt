file(GLOB srcs "src/*.cpp" "src/*.c")
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
