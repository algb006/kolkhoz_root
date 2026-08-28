# core_tables — loading and holding the balance definition tables (stage 1,
# tasks F5 and O3). Infrastructure: subject modules read crop, resource and
# unit-type definitions through its interface; it knows nothing about them.

kolkhoz_add_module(core_tables
    placeholder.cpp)

target_link_libraries(core_tables
    PUBLIC core_common
    PRIVATE core_log)
