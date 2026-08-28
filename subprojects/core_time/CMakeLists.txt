# core_time — phase 1 of the step: calendar advance, seasons, daylight,
# weather (stage 2 of the plan). Owns the time-and-weather slot; core_sim is
# PUBLIC because the module's boundary is that slot's interface.

kolkhoz_add_module(core_time
    time_system.cpp)

target_link_libraries(core_time
    PUBLIC core_common core_sim
    PRIVATE core_tables core_log)
