# core_save — the save format: a WorldState to bytes and back, definition
# ids remapped by table key on load (contract in include/core_save/save.h,
# decision in manual/67-save-format.md; stage 7, task F1 designs it, task O1
# implements it).
#
# Infrastructure tier, a leaf: it needs the state (core_common) and the live
# tables' keys (core_tables), knows no subsystem, and nothing in the core
# depends on it — the executables and, later, the UE side call it.

kolkhoz_add_module(core_save
    placeholder.cpp)

target_link_libraries(core_save
    PUBLIC core_common
    PRIVATE core_tables core_log)
