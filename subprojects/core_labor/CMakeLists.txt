# core_labor — labor (stage 5 of the plan): work assignments, the accountant's
# placement algorithm, workday by the sun, trudodni accrual. Runs inside the
# sequential decisions phase through the subsystem interface (task F4), so it
# owns no slot of its own and does not depend on core_sim.

kolkhoz_add_module(core_labor
    assignment.cpp
    labor_config.cpp
    labor_day.cpp
    labor_system.cpp
    posts.cpp
    work_orders.cpp)

target_link_libraries(core_labor
    PUBLIC core_common
    PRIVATE core_tables core_log)
