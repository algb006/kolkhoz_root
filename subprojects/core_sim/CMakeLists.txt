# core_sim — the step engine: the two state buffers, the seven phases with
# barriers, the worker pool and the mandatory single-thread mode (contract in
# include/core_sim/step.h; implementation is stage 1, task O2).
#
# Subsystems plug in through the phase interfaces; the engine never names a
# subject module. enkiTS (depends/enkiTS) is the sanctioned scheduler and
# stays PRIVATE: no enkiTS type appears in include/core_sim.

kolkhoz_add_module(core_sim
    step_engine.cpp)

target_link_libraries(core_sim
    PUBLIC core_common
    PRIVATE core_log enkiTS)
