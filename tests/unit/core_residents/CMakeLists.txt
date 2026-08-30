kolkhoz_add_unit_test(core_residents main.cpp)

# The exchange, the food configuration and the vitals window are internal
# seams of the module (family_exchange.h, food_config.h, vitals.h); their
# unit tests feed the structs directly, so the module's source dir is on the
# include path.
target_include_directories(unit_core_residents
    PRIVATE "${CMAKE_SOURCE_DIR}/subprojects/core_residents")
