# core_log — logging to file and console (stage 1, task O4). Infrastructure:
# any module may use it, it uses nothing but the base.

kolkhoz_add_module(core_log
    log.cpp)

target_link_libraries(core_log
    PUBLIC core_common)
