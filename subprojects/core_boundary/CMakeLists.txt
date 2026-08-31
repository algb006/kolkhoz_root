# core_boundary — the boundary of the core: one session object over the
# assembled simulation through which the presentation (the game, the UE
# panels, the run tool with a bot policy) advances time, reads the completed
# state and the signals derived from it, issues and cancels orders, drains
# events and keeps the command journal (contract in
# include/core_boundary/session.h, decision in manual/70-boundary.md;
# project phase 2, task A1 designed it; the implementing task fills it).
#
# Infrastructure tier, a leaf: it needs the state (core_common), the engine
# contract it wraps (core_sim) and the live tables for the few derived
# signals that have a balance knob (core_tables). It names no subsystem —
# it moves rows in the order book and reads the outbox, and every rule of
# what an order means stays with the subsystem that consumes it. Nothing in
# the core depends on it; the executables and the UE side do.

kolkhoz_add_module(core_boundary
    placeholder.cpp)

target_link_libraries(core_boundary
    PUBLIC core_common core_sim
    PRIVATE core_tables core_log)
