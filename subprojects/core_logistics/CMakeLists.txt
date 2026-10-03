# core_logistics — the groom's logistics (routing stage B; transport design §11-12): the tasks
# of carting with their levels, the day's plan of the carts, the re-plan by event, the chairman's
# doors to the tasks. A subject module: it reads and writes the world's state in the decisions
# phase (3), after the labour sub-step, and names no other subject module.

kolkhoz_add_module(core_logistics
    logistics_config.cpp
    logistics_plan.cpp
    logistics_system.cpp
    logistics_tasks.cpp)

target_link_libraries(core_logistics
    PUBLIC core_common
    PRIVATE core_catalog core_tables core_log)
