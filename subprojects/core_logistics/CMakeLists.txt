# core_logistics — the logistics slot (phase 5). In project phase 1 this is a
# declared STUB: delivery is instant (plan, §11а). The module exists so the
# slot has an owner and project phase 2 fills the body without rewiring.

kolkhoz_add_module(core_logistics
    placeholder.cpp)

target_link_libraries(core_logistics
    PUBLIC core_common core_sim
    PRIVATE core_log)
