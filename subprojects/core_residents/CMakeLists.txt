# core_residents — the people (stage 3 of the plan): needs, food and cold,
# households and personal plots, demography, family satisfaction. Owns two
# parallel slots — needs (phase 2, by family) and metrics (phase 6, by
# family); its structural work (births, deaths, marriages) runs inside the
# sequential decisions phase through the subsystem interface (task F4).

kolkhoz_add_module(core_residents
    residents_system.cpp
    life_config.cpp
    food_config.cpp
    family_exchange.cpp
    vitals.cpp)

target_link_libraries(core_residents
    PUBLIC core_common core_sim
    PRIVATE core_tables core_log)
