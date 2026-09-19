# core_production — land and production (stage 4 of the plan): fields, crops
# and fertility, unit work cycles, buffers and stores, herds. Owns the
# production slot (phase 4, parallel by unit and field). Worker productivity
# is read from resident state in core_common — no dependency on
# core_residents, by design.

kolkhoz_add_module(core_production
    production_system.cpp
    production_config.cpp
    field_haul.cpp
    field_work.cpp
    herd_life.cpp
    herd_system.cpp
    milk_cart.cpp
    stable_horses.cpp
    night_pasture.cpp
    production_alarms.cpp
    stock_lights.cpp
    timber_felling.cpp
    extraction_digging.cpp
    unit_production.cpp
    processing_shops.cpp
    demolition_stock.cpp
    district_limit.cpp
    mts_column.cpp
    district_visit.cpp
    district_plan.cpp
    production_orders.cpp
    field_removal.cpp)

target_link_libraries(core_production
    PUBLIC core_common core_sim
    PRIVATE core_catalog core_tables core_log)
