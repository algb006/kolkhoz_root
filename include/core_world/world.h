/// @file
/// @brief core_world boundary: world genesis and the assembled simulation.
/// @threading SINGLE_THREADED
/// Everything here is called from the sim thread: the factories at setup, and
/// the composite decisions/events slots that core_world implements internally
/// run in sequential phases. Worker threads never enter this module.
///
/// core_world is the composition root (manual/54-modules.md): it creates the
/// subsystems, wires the StepPhaseSet and owns the two composite sequential
/// slots. The decisions slot (phase 3) calls the subsystems' sub-steps in a
/// fixed order — assignments (core_labor), then demography (core_residents),
/// then production decisions (core_production); the events slot (phase 7) is
/// a STUB until the event system exists (project phase 3). Consumers — unit
/// tests, the balance run, later the UE layer — see only the two factories
/// below and the ISimulation they yield.

#ifndef CORE_WORLD_WORLD_H_
#define CORE_WORLD_WORLD_H_

#include <cstdint>
#include <memory>

#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace core {

class ITableSet;  // Defined in core_tables (stage 1, task F5).

/// @brief Creates the starting world: the settlement the campaign begins with.
/// The genesis of the design's start conditions — 80 residents, 21 yards,
/// 160 ha of arable land — built from the balance tables, deterministically
/// from the seed: same tables, same seed — same settlement.
/// @param tables     Balance tables; used during the call only.
/// @param world_seed Campaign seed; stored in the returned state.
/// @note STUB until stage 3: returns a world at day 0 with weather, epoch,
/// seed and plan defaults but no resident, family, field or unit rows —
/// exactly enough for the empty-world criterion of stage 1.
WorldState CreateStartWorld(const ITableSet& tables, std::uint64_t world_seed);

/// @brief Everything CreateStandardSimulation needs.
struct StandardSimulationConfig {
  /// Balance tables; non-owning — the caller keeps them alive for the whole
  /// lifetime of the returned simulation.
  const ITableSet* tables = nullptr;

  /// Campaign seed for CreateStartWorld and every derived random draw.
  std::uint64_t world_seed = 0;

  /// 0 = one worker per hardware core minus one; 1 = the mandatory
  /// verification mode. Results are identical for every value — that is the
  /// determinism check, not an option.
  std::uint32_t worker_count = 1;
};

/// @brief Creates the fully wired simulation: subsystems, phases, engine.
/// The returned object owns the subsystems and the step engine; destroying
/// it releases everything. World lifecycle from here on: AdvanceStep to run,
/// CompletedState to observe, ResetWorld to rewind — subsystems hold no
/// world state, so rewinding needs no notification (subsystem law,
/// manual/52-state-model.md).
/// Implemented in core_world (stage 1, task O2 wires the stubs of task O0).
std::unique_ptr<ISimulation> CreateStandardSimulation(const StandardSimulationConfig& config);

}  // namespace core

#endif  // CORE_WORLD_WORLD_H_
