kolkhoz_add_unit_test(core_production main.cpp)

# The herd day and the parsed configuration are internal seams of the module
# (herd_system.h, production_config.h); the tests feed the structs directly,
# so the module's source dir is on the include path.
target_include_directories(unit_core_production
    PRIVATE "${CMAKE_SOURCE_DIR}/subprojects/core_production")

# The verdict on the district's plan is checked against the SHIPPED tables and
# not against a fixture: what that check is about is that the figures the game
# ships CAN be missed, and a fixture would be checking figures nobody plays.
target_compile_definitions(unit_core_production
    PRIVATE "KOLKHOZ_TABLES_DIR=\"${CMAKE_SOURCE_DIR}/tables\"")
