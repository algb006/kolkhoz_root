# core_production — land and production (stage 4 of the plan): fields, crops
# and fertility, unit work cycles, buffers and stores, herds. Owns the
# production slot (phase 4, parallel by unit and field). Worker productivity
# is read from resident state in core_common — no dependency on
# core_residents, by design.

kolkhoz_add_module(core_production
    production_system.cpp
    production_config.cpp
    field_haul.cpp
    herd_system.cpp
    stable_horses.cpp
    stock_lights.cpp)

target_link_libraries(core_production
    PUBLIC core_common core_sim
    PRIVATE core_tables core_log)
