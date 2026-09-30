idf_component_register(INCLUDE_DIRS "include" REQUIRES bus)
target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_23)
