# core_common — the base: identifiers, units of measure, calendar arithmetic,
# geometry math, state-table operations, the RNG. The only module every other
# module may always use; itself depends on nothing.
#
# The state of the world lives in include/core_common/ as plain data
# (manual/52-state-model.md), so subsystems read each other's state through
# this module alone and never through each other.

kolkhoz_add_module(core_common
    alarm_state.cpp)
