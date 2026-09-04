# core_world — the composition root: genesis of the starting settlement,
# wiring of the StepPhaseSet, and the two composite sequential slots —
# decisions (phase 3: accountant from core_labor, demography from
# core_residents, production choices from core_production, in a fixed order)
# and events (phase 7: a STUB until the event system exists).
#
# The one module allowed to name every other; nothing depends on it except
# the executables in tests/ and src/.

kolkhoz_add_module(core_world
    genesis.cpp
    world.cpp)

target_link_libraries(core_world
    PUBLIC core_common core_sim
    PRIVATE core_catalog core_tables core_time core_residents core_production
            core_logistics core_labor core_construction core_log)
