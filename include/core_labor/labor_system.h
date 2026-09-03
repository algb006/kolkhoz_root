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
/// Posts (project phase 2, task A7; manual/74-posts.md):
///   * The consumer of kAppoint and kDismiss — the order book's fourth
///     consumer after production, construction and (still unwired)
///     kAssignWork. A post is a standing appointment held on the resident's
///     row (ResidentRow::post), not a day's work order: it survives the day
///     and the save, and only another order ends it. Validation in the step
///     the order is read, application at the day's close (time design §11).
///   * What a post does each morning: the holder is out of the accountant's
///     pool and stands first on his own unit's daily work when the core
///     models it — the yard's herd care for the groom. Posts whose work is
///     not modelled keep the holder reserved and idle (STUB, by table).
///   * The horse lock of the start canon is read from ChairmanState::
///     horses_stabled: once the herd day has gathered the horses, no
///     householder is tied to horse work again (livestock design §5).
///   * The kYardWithoutGroom alarm (alarm_state.h), through CollectAlarms.
///
/// What stage 5 deliberately leaves out (STUB hooks, not mechanics):
/// lateness and absenteeism, chairman day-shortening commands, master-level
/// pair synergy, roads (travel is distance x a path factor).

#ifndef CORE_LABOR_LABOR_SYSTEM_H_
#define CORE_LABOR_LABOR_SYSTEM_H_

#include <memory>
#include <vector>

#include "core_common/alarm_state.h"
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
  /// HerdRow::care_days_remaining, FamilyRow trudodni and household_hours;
  /// ResidentRow::post and the status of kAppoint/kDismiss rows in the
  /// order book (task A7). Never changes any table's shape.
  virtual void RunAssignmentDecisions(const WorldState& previous, WorldState& current) = 0;

  /// @brief Appends the labor alarms that hold in `state` (task A7).
  /// Today one: kYardWithoutGroom — a built unit whose staff table names
  /// the groom's post, no holder, and kolkhoz horses still at private
  /// yards. A pure reading of the world: the same state gives the same
  /// list, and nothing is remembered between calls. Called by
  /// StandardSimulation::CollectAlarms first, before production; the
  /// boundary sorts the union (session.h, ActiveAlarms).
  /// @param state Any complete world — the current buffer between steps.
  virtual void CollectAlarms(const WorldState& state, std::vector<Alarm>& out) const = 0;
};

/// @brief Creates the labor subsystem.
/// @param tables Balance tables; non-owning, must outlive the returned
///               object. Reads labor.csv (rates, thresholds, efficiency
///               factors) plus the norm columns of crops.csv, farming.csv
///               and livestock.csv (formats: manual/61-balance-tables.md);
///               since task A7 also professions.csv (the posts: key,
///               education threshold, working age) and unit_staff.csv (which
///               unit type carries which post, and how many slots — empty
///               means unlimited). Without those two every kAppoint is
///               refused kRuleForbids: no post exists that can be filled.
/// @return nullptr when a present table is malformed (missing tables mean
///         the documented defaults, like every subsystem factory).
std::unique_ptr<ILaborSystem> CreateLaborSystem(const ITableSet& tables);

}  // namespace core

#endif  // CORE_LABOR_LABOR_SYSTEM_H_
