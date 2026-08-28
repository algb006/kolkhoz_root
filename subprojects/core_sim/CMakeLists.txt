# core_sim — the step engine: the two state buffers, the seven phases with
# barriers, the worker pool and the mandatory single-thread mode (contract in
# include/core_sim/step.h; implementation is stage 1, task O2).
#
# Subsystems plug in through the phase interfaces; the engine never names a
# subject module. enkiTS is added here as a PRIVATE dependency by task O2,
# once it is vendored into depends/.

kolkhoz_add_module(core_sim
    placeholder.cpp)

target_link_libraries(core_sim
    PUBLIC core_common
    PRIVATE core_log)
