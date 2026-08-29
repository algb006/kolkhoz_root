/// @file
/// @brief The accountant's placement algorithm — the internal seam of
/// core_labor between the day model (labor_system.cpp) and the placement
/// itself (assignment.cpp).
/// @threading SINGLE_THREADED
/// Pure functions of their inputs, called from the labor sub-step of the
/// decisions slot on the sim thread. No world state, no tables, no RNG:
/// determinism is total ordering over the inputs, and unit tests feed the
/// structs directly.
///
/// Design source: society design §1 (what the accountant weighs and how his
/// level degrades the placement), time design §7 (the road limit), the boss
/// digest 2026-08-29 §2.4 (surplus workers idle and earn nothing). The
/// job-priority order and the crew caps are the core's own decisions,
/// manual/65-labor-model.md §4.

#ifndef CORE_LABOR_ASSIGNMENT_H_
#define CORE_LABOR_ASSIGNMENT_H_

#include <cstdint>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/labor_state.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One job opening of the day, in placement terms. The caller
/// (labor_system.cpp) builds these from fields and herds; the algorithm
/// never learns where they came from beyond the target ids it copies into
/// assignments.
struct AssignmentJob {
  WorkKind kind = WorkKind::kNone;

  FieldId field;  ///< Valid for field kinds; copied into WorkAssignment.

  HerdId herd;  ///< Valid for kHerdCare; copied into WorkAssignment.

  Vec2 position;  ///< Where the work is; drives travel time.

  /// Game man-days of demand left (the seam value at morning). Caps the
  /// useful crew: nobody is placed beyond what the day can consume.
  float work_days_remaining = 0.0F;

  /// Whole days until this job's calendar window closes (sowing window,
  /// harvest before snow). Smaller = more urgent; the placement fills
  /// urgent jobs first. Daily work that expires tonight — barn care —
  /// passes 0; windowless seasonal work passes 255. At equal urgency the
  /// kind decides (harvest > sowing > plowing > harrowing > care).
  std::uint8_t window_days_left = 255;
};

/// @brief One available worker, in placement terms.
struct AssignmentCandidate {
  /// Row index in the current residents table; echoed back in the result.
  std::uint32_t resident_row = 0;

  Vec2 home;  ///< Where the day starts and ends; drives travel time.

  /// Expected norm-days delivered over a full standard day at this
  /// worker's age, health, rest, education (computed by the caller from
  /// resident state; > 0).
  float efficiency = 1.0F;

  /// Current rest, 0-100. Low rest makes a worker a poor pick (experienced
  /// accountant) and predicts a walk-off.
  Metric rest = 70.0F;

  /// The agriculture skill blend, 0-100: what "skill" means for Epoch-I
  /// field work (education design §11; simple work weighs strength over
  /// diplomas, so the caller blends earned skill with stamina).
  Metric skill = 0.0F;

  /// The start-canon lock (livestock design §5): hosts a kolkhoz horse, so
  /// only horse works may take him. IsHorseWork(kind) tells which.
  bool horse_locked = false;
};

/// @brief The day's placement parameters, from config and calendar.
struct AssignmentParams {
  /// Hours of the daylight work window today (sunrise to sunset shrunk as
  /// the model dictates); a worker's usable hours are these minus twice
  /// his travel.
  float window_hours = 10.0F;

  /// Game hours of one-way walking per kilometre of straight-line
  /// distance: path factor / (real walking speed / kClockScale). The
  /// canonical 5 km/h and factor 1.3 give ~3.1 — which puts the 2-hour
  /// limit at ~640 m straight-line, the design's "on foot that is ~800 m"
  /// of path (time design §7).
  float walk_hours_per_km = 3.1F;

  /// One-way travel limit in game hours (~2, time design §7): a job
  /// farther than this from a worker's home cannot take him at all.
  float travel_limit_hours = 2.0F;

  /// Hours of work behind one norm man-day: delivered norm-days =
  /// worked hours x efficiency / this.
  float standard_day_hours = 10.0F;

  /// Adult kolkhoz horses available today. Every worker placed on a horse
  /// work consumes one from this shared pool; the pool caps those crews.
  std::uint32_t draught_horses = 0;

  /// Placement quality 0-3 (society design §1): 0 = naive "whoever is
  /// there", 1 = skill and strength, 2 = plus road and fatigue, 3 = master
  /// (phase 1: as 2 — pair synergy is a STUB).
  std::uint8_t placement_level = 0;
};

/// @brief Index value meaning "left idle today" in the result.
inline constexpr std::uint32_t kNoJobAssigned = 0xFFFFFFFFU;

/// @brief Places the day's workers over the day's jobs.
/// @param jobs       The openings; order irrelevant (the algorithm orders
///                   deterministically by urgency, then kind, then target
///                   id — never by input position).
/// @param candidates The workforce; order irrelevant likewise (ties break
///                   by resident_row, the stable identity).
/// @param params     The day's parameters.
/// @return Per candidate (same order as `candidates`): the index into
///         `jobs` he works today, or kNoJobAssigned — surplus hands idle
///         and earn nothing (a trudoden is a work norm, not attendance).
/// @note Pure and deterministic: equal inputs give the equal vector on any
///       platform. No RNG — even the naive level uses stable order, not
///       chance.
std::vector<std::uint32_t> PlanDayAssignments(const std::vector<AssignmentJob>& jobs,
                                              const std::vector<AssignmentCandidate>& candidates,
                                              const AssignmentParams& params);

}  // namespace core

#endif  // CORE_LABOR_ASSIGNMENT_H_
