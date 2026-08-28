/// @file
/// @brief IResidentsSystem — the boundary of the people subsystem.
/// @threading PARALLEL_WRITE
/// The subsystem's parallel phases — needs (slot 2) and metrics (slot 6) —
/// write world state from many workers, each over its own range of family
/// rows, strictly under the buffer law (core_sim/step.h). Its demography
/// sub-step runs sequentially inside the decisions slot (phase 3) on the sim
/// thread; accessors and factory are wiring-time, sim thread only.
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState. Scratch buffers inside a phase are fine, but nothing carried
/// between steps; ISimulation::ResetWorld needs no callback here.
///
/// Responsibilities (stage 3 of the plan): resident needs — food from the
/// family pantry, rest, cold, mood; family satisfaction aggregates; and the
/// structural work of demography — births, deaths, marriages, migration.
/// The resident and family row layouts are designed at stage 3, behind this
/// interface; the interface does not change with them.

#ifndef CORE_RESIDENTS_RESIDENTS_SYSTEM_H_
#define CORE_RESIDENTS_RESIDENTS_SYSTEM_H_

#include <memory>

#include "core_sim/step.h"

namespace core {

class ITableSet;  // Defined in core_tables (stage 1, task F5).

/// @brief The people subsystem: owner of step slots 2 and 6 and of the
/// demography sub-step of the decisions slot.
class IResidentsSystem {
 public:
  virtual ~IResidentsSystem() = default;

  /// @brief The needs slot implementation (phase 2, parallel by family).
  /// Valid for the lifetime of the system; wired into StepPhaseSet::needs.
  virtual IParallelPhase& NeedsPhase() = 0;

  /// @brief The metrics slot implementation (phase 6, parallel by family).
  /// Valid for the lifetime of the system; wired into StepPhaseSet::metrics.
  virtual IParallelPhase& MetricsPhase() = 0;

  /// @brief Demography: the residents' sub-step of the decisions slot.
  /// Called by core_world every step (phase 3, sim thread), second in the
  /// fixed order of that slot (manual/54-modules.md, §3). The only place
  /// where resident and family rows are appended or removed — births,
  /// deaths, marriages, migration. Runs every tick; the implementation
  /// itself gates daily work to day boundaries.
  virtual void RunDemographyDecisions(const WorldState& previous, WorldState& current) = 0;
};

/// @brief Creates the people subsystem.
/// @param tables Balance tables (consumption norms, demography rates);
///               non-owning, must outlive the returned object.
/// Implemented in core_residents (stage 3 of the plan; no-op STUB — task O0).
std::unique_ptr<IResidentsSystem> CreateResidentsSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_RESIDENTS_RESIDENTS_SYSTEM_H_
