/// @file
/// @brief ILaborSystem — the boundary of the labor subsystem.
/// @threading SINGLE_THREADED
/// The subsystem owns no phase slot: all its work runs sequentially inside
/// the decisions slot (phase 3), on the sim thread, called by core_world.
/// It therefore never sees worker threads and does not know core_sim.
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState.
///
/// Responsibilities (stage 5 of the plan): the accountant's placement of
/// workers over the day's work list — skill, distance, fatigue, discipline;
/// applying deferred job changes at day boundaries; the workday bounded by
/// daylight; trudodni accrual to family accounts. Assignments themselves are
/// fields of the resident rows (current and pending job), not a table of
/// their own.

#ifndef CORE_LABOR_LABOR_SYSTEM_H_
#define CORE_LABOR_LABOR_SYSTEM_H_

#include <memory>

#include "core_common/world_state.h"

namespace core {

class ITableSet;  // Defined in core_tables (stage 1, task F5).

/// @brief The labor subsystem: assignments and pay inside the decisions slot.
class ILaborSystem {
 public:
  virtual ~ILaborSystem() = default;

  /// @brief Assignments: the labor sub-step of the decisions slot.
  /// Called by core_world every step (phase 3, sim thread), first in the
  /// fixed order of that slot (manual/54-modules.md, §3) — placement runs
  /// before demography and production decisions so the day's workforce is
  /// settled when they look at it. Runs every tick; day-boundary work
  /// (deferred job changes, accrual close-out) gates itself.
  virtual void RunAssignmentDecisions(const WorldState& previous, WorldState& current) = 0;
};

/// @brief Creates the labor subsystem.
/// @param tables Balance tables (work rates, norms, thresholds); non-owning,
///               must outlive the returned object.
/// Implemented in core_labor (stage 5 of the plan; no-op STUB — task O0).
std::unique_ptr<ILaborSystem> CreateLaborSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_LABOR_LABOR_SYSTEM_H_
