/// @file
/// @brief IProductionSystem — the boundary of the land-and-production subsystem.
/// @threading PARALLEL_WRITE
/// The production phase (slot 4) writes world state from many workers, each
/// over its own range of unit and field rows, strictly under the buffer law
/// (core_sim/step.h). The production-decisions sub-step runs sequentially
/// inside the decisions slot (phase 3) on the sim thread; accessor and
/// factory are wiring-time, sim thread only.
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState; nothing is carried between steps outside it.
///
/// Responsibilities (stage 4 of the plan): crop growth and harvest, field
/// fertility, unit work cycles with their input and output buffers, herds
/// and their feed. Worker productivity is read from resident state in
/// core_common — this module never names core_residents.

#ifndef CORE_PRODUCTION_PRODUCTION_SYSTEM_H_
#define CORE_PRODUCTION_PRODUCTION_SYSTEM_H_

#include <memory>

#include "core_sim/step.h"

namespace core {

class ITableSet;  // Defined in core_tables (stage 1, task F5).

/// @brief The land-and-production subsystem: owner of step slot 4 and of the
/// production sub-step of the decisions slot.
class IProductionSystem {
 public:
  virtual ~IProductionSystem() = default;

  /// @brief The production slot implementation (phase 4, parallel by unit
  /// and field). Valid for the lifetime of the system; wired into
  /// StepPhaseSet::production.
  virtual IParallelPhase& ProductionPhase() = 0;

  /// @brief Production decisions: the sub-step of the decisions slot.
  /// Called by core_world every step (phase 3, sim thread), third in the
  /// fixed order of that slot (manual/54-modules.md, §3). Sequential,
  /// structure-changing work of the domain: what each unit produces next
  /// (nomenclature), starting and closing cycles, seasonal transitions of
  /// fields. Runs every tick; daily work gates itself to day boundaries.
  virtual void RunProductionDecisions(const WorldState& previous, WorldState& current) = 0;
};

/// @brief Creates the production subsystem.
/// @param tables Balance tables (crops, unit types, livestock kinds, norms);
///               non-owning, must outlive the returned object.
/// Implemented in core_production (stage 4 of the plan; no-op STUB — task O0).
std::unique_ptr<IProductionSystem> CreateProductionSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_SYSTEM_H_
