kolkhoz_add_unit_test(core_world main.cpp)

# Genesis only reaches the herds and the start economy when the whole table
# set is there (genesis.cpp, BuildStartEconomy), so the guard tests copy the
# shipped tables and spoil one cell. The path is a definition rather than a
# working directory: unit tests are run from wherever ctest stands.
target_compile_definitions(unit_core_world
    PRIVATE "KOLKHOZ_TABLES_DIR=\"${CMAKE_SOURCE_DIR}/tables\"")
