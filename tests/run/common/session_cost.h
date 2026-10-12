// The game's cost of a day, measured THROUGH THE RELEASE'S DOOR (#700, boss 12 Oct 2026: «an
// instrument enters through the same door as the release»). thirty_years drives the simulation
// without a session, and the session refreshes the alarms, the porters, the stock lights and the
// forecast after EVERY step: at year 27 of the canon construction's alarms alone cost ~0.13 s a
// collection, twenty-four a day in the game - a cost class no run saw. Here a session is built over
// a copy of a world and stepped whole game days, each timed.
#ifndef KOLKHOZ_TESTS_RUN_COMMON_SESSION_COST_H_
#define KOLKHOZ_TESTS_RUN_COMMON_SESSION_COST_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include "core_boundary/session.h"
#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

struct SessionDayCost {
  /// False when the session could not be built - then nothing was measured.
  bool measured = false;
  /// The costliest of the days stepped, seconds of wall time for its 24 session steps.
  double worst_day_seconds = 0.0;
  /// Their mean.
  double mean_day_seconds = 0.0;
};

/// @brief Steps `days` game days of a session over a copy of `state` and times each.
inline SessionDayCost SessionDaySeconds(const core::ITableSet& tables,
                                        std::uint64_t seed,
                                        const core::WorldState& state,
                                        std::uint32_t days) {
  SessionDayCost cost;
  core::StandardSimulationConfig config;
  config.tables = &tables;
  config.world_seed = seed;
  config.worker_count = 1;
  config.stub_tables = core::StubTables::kRefused;
  std::unique_ptr<core::ISimulation> simulation = core::CreateStandardSimulation(config);
  if (simulation == nullptr) {
    return cost;
  }
  simulation->ResetWorld(state);
  core::SessionConfig session_config;
  session_config.tables = &tables;
  session_config.simulation = std::move(simulation);
  const std::unique_ptr<core::ISession> session = core::CreateSession(std::move(session_config));
  if (session == nullptr || days == 0) {
    return cost;
  }
  double sum = 0.0;
  for (std::uint32_t day = 0; day < days; ++day) {
    const auto began = std::chrono::steady_clock::now();
    for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
      session->AdvanceStep();
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    sum += seconds;
    cost.worst_day_seconds = seconds > cost.worst_day_seconds ? seconds : cost.worst_day_seconds;
  }
  cost.mean_day_seconds = sum / static_cast<double>(days);
  cost.measured = true;
  return cost;
}

}  // namespace run

#endif  // KOLKHOZ_TESTS_RUN_COMMON_SESSION_COST_H_
