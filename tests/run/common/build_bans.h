/// @file
/// @brief The run bot asks the placement bans before it orders a build (#1788;
/// boss #1886: «the site pickers ask the bans first and step to the next
/// candidate») - as a player sees the ban layer before he marks.
/// @threading SINGLE_THREADED
/// Test-side code, used on the thread that owns the simulation.
///
/// ONE DOOR FOR EVERY PICKER: the run sets the simulation once (SetBuildBans),
/// and every picker's free place comes through BuildablePlot, which is the
/// core's FreePlot with the build order's own ban reading
/// (ISimulation::BuildBansToday) as its extra test.

#ifndef TESTS_RUN_COMMON_BUILD_BANS_H_
#define TESTS_RUN_COMMON_BUILD_BANS_H_

#include <atomic>
#include <functional>
#include <iostream>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/plot.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

/// The simulation the bans are asked of; null - no bans asked (a run that
/// never set it, as before #1788). THREAD-LOCAL: population_curve's walkers
/// live their villages on threads of their own, each its own simulation.
inline const core::ISimulation*& BuildBansSimulation() {
  thread_local const core::ISimulation* simulation = nullptr;
  return simulation;
}

/// @brief Sets the simulation every picker asks (once per village).
inline void SetBuildBans(const core::ISimulation* simulation) {
  BuildBansSimulation() = simulation;
}

/// @brief Whether a build of `type` at `place` meets no placement ban today.
inline bool BuildAllowed(core::UnitTypeId type, core::Vec2 place) {
  const core::ISimulation* const simulation = BuildBansSimulation();
  if (simulation == nullptr) {
    // NOT ASKED, SAID ALOUD (19 Nov 2026: host's plan700 never set it, its pickers
    // chose blind on 402/403 and the core refused the late houses kRuleForbids -
    // a third of the houses gone by y13 and no line said why). Once a process.
    static std::atomic<bool> warned{false};
    if (!warned.exchange(true)) {
      std::cerr << "BUILD_BANS NOT ASKED: a picker ran with no run::SetBuildBans on its "
                   "thread - sites are chosen blind and the core refuses banned ones "
                   "(kRuleForbids); set it once a village after the simulation is built\n";
    }
    return true;
  }
  return simulation->BuildBansToday(type, place) == 0;
}

/// @brief The core's FreePlot, with the placement bans asked of each candidate.
inline core::Vec2 BuildablePlot(const core::UnitTable& units,
                                const core::PlotRules& rules,
                                const core::Vec2& wanted,
                                float radius,
                                core::UnitTypeId type) {
  return core::FreePlot(
      units, rules, wanted, radius, [type](core::Vec2 place) { return BuildAllowed(type, place); });
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_BUILD_BANS_H_
