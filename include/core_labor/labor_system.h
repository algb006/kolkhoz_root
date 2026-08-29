/// @file
/// @brief ILaborSystem — the boundary of the labor subsystem.
/// @threading SINGLE_THREADED
/// The subsystem owns no phase slot: all its work runs sequentially inside
/// the decisions slot (phase 3), on the sim thread, called by core_world
/// first in that slot's fixed order. It never sees worker threads and does
/// not know core_sim. Sequential by decision, not by accident: the data
/// volume (thousands of residents, hourly float updates) never justifies a
/// parallel phase (core rules §10 — parallelism must be earned by volume).
///
/// Subsystem law (state model, manual/52-state-model.md): implementations
/// hold configuration only — every fact about the simulated world lives in
/// WorldState.
///
/// Responsibilities (stage 5; the model is manual/65-labor-model.md):
///   * The day's job list from the world: field working phases (through the
///     work_days_remaining seam, land_state.h) and barn care of
///     unit-standing herds (care_days_remaining, herd_state.h).
///   * The accountant's placement each morning — skill, strength, road,
///     fatigue, at the configured placement quality; the horse-work
///     constraints of the start canon. Results land in ResidentRow::work.
///   * The hourly grind inside the daylight window: draining the job seams,
///     draining rest, the fatigue walk-off (rest under the critical
///     threshold sends the worker home — his own decision, unit rules §8).
///   * Day close: trudodni accrual to family accounts (rate x delivered
///     norm-days — a trudoden is a work norm, not attendance), the family's
///     household_hours, the yearly account burn at economic year end.
///
/// What stage 5 deliberately leaves out (STUB hooks, not mechanics):
/// lateness and absenteeism, chairman day-shortening commands, master-level
/// pair synergy, roads (travel is distance x a path factor).

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

  /// @brief The labor sub-step of the decisions slot.
  /// Called by core_world every step (phase 3, sim thread), first in the
  /// fixed order of that slot (manual/54-modules.md, §3) — placement runs
  /// before demography and production decisions so the day's workforce is
  /// settled when they look at it. Runs every tick and gates its own
  /// cadence: morning assignment at the day's first tick, hourly work only
  /// inside the daylight window, close-out at the day's last tick.
  /// @note Writes ResidentRow::work and rest, FieldRow::work_days_remaining,
  /// HerdRow::care_days_remaining, FamilyRow trudodni and household_hours.
  /// Never changes any table's shape.
  virtual void RunAssignmentDecisions(const WorldState& previous, WorldState& current) = 0;
};

/// @brief Creates the labor subsystem.
/// @param tables Balance tables; non-owning, must outlive the returned
///               object. Reads labor.csv (rates, thresholds, efficiency
///               factors) plus the norm columns of crops.csv, farming.csv
///               and livestock.csv (formats: manual/61-balance-tables.md).
/// @return nullptr when a present table is malformed (missing tables mean
///         the documented defaults, like every subsystem factory).
std::unique_ptr<ILaborSystem> CreateLaborSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_LABOR_LABOR_SYSTEM_H_
