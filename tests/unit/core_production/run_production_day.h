/// @file
/// @brief One production step as the world's decisions slot runs it, for the
/// production tests that drive the system by hand: the fields' day first
/// (IProductionSystem::OpenTodaysFieldPhases, before the placement since
/// 0.37.234), then the production decisions. Before 0.37.234 the tests called
/// RunProductionDecisions alone, which ran the fields' day itself.
/// @threading SINGLE_THREADED
#pragma once

#include "core_common/world_state.h"
#include "core_production/production_system.h"

/// @brief The fields' day, then the production decisions, on one step.
/// @param system The production system under test.
/// @param previous The state before the step (read).
/// @param current The state being written.
inline void RunProductionDay(core::IProductionSystem& system,
                             const core::WorldState& previous,
                             core::WorldState& current) {
  system.OpenTodaysFieldPhases(previous, current);
  system.RunProductionDecisions(previous, current);
}
