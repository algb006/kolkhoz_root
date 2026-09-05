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

  UnitId unit;  ///< Valid for kConstruction: the site. Copied into WorkAssignment.

  /// At most this many workers on this job at once; 0 = no cap beyond the
  /// demand ceiling below. Only construction sites carry one — the build
  /// class's brigade (unit_levels.csv max_crew): without it a 250-day site
  /// takes every free hand in the village and "a couple of weeks for a
  /// brigade" becomes three days (construction design §8).

  Vec2 position;  ///< Where the work is; drives travel time.

  /// Game man-days of demand left (the seam value at morning). Caps the
  /// useful crew: nobody is placed beyond what the day can consume.
  float work_days_remaining = 0.0F;

  std::uint8_t max_crew = 0;

  /// True when this job goes out WITH A HORSE although its kind is not one
  /// of the two horse works: the meadow cut, mown and raked with the horse
  /// implements the district issued at the start (livestock design §5,
  /// farming design §5 — "scythes, sickles, HORSE MOWERS"). It changes two
  /// things and only two: the shoulder is measured at harness speed (time
  /// design §7 — "whoever rides out with a horse reaches farther than the
  /// man on foot"), and each worker placed takes one horse from the day's
  /// pool, like ploughing.
  bool harnessed = false;

  /// Whole days until this job's calendar window closes (sowing window,
  /// harvest before snow). Smaller = more urgent; the placement fills
  /// urgent jobs first. Daily work that expires tonight — barn care —
  /// passes 0; windowless seasonal work passes 255. At equal urgency the
  /// kind decides (care > harvest > sowing > plowing > harrowing).
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
  /// TURNS TAKE TURNS. The candidates who tie on everything else used to be
  /// broken apart by their ROW, ascending, and rows are handed out at
  /// birth — so the queue was in birth order and never moved. Measured on
  /// the shipped tables, where placement_level is 0 and therefore EVERY
  /// candidate ties at a score of zero: the first fifth of the roster idled
  /// 36 % of its working day and the last three fifths idled 93-96 %. The
  /// same measurement by age reads as "the young never work", and that is
  /// the same fact wearing a face — the young are simply the late rows.
  ///
  /// So the tie is broken on the row PLUS THE DAY, modulo the roster: the
  /// queue advances by one every morning and everybody's turn comes round.
  /// It costs no state, it is the same for one worker and for many, and it
  /// is what a tallyman handing out orders does — the design says there are
  /// no foremen optimising output (society design §1).
  std::uint32_t rotation = 0;

  /// People in the roster, the modulus of that rotation. Zero disables it
  /// and leaves the old fixed order, which is what a world with no
  /// residents wants and what every unit test that does not care gets.
  std::uint32_t roster = 0;

  /// Hours of the daylight work window today (sunrise to sunset shrunk as
  /// the model dictates); a worker's usable hours are these minus twice
  /// his travel.
  float window_hours = 10.0F;

  /// Game hours of one-way travel per kilometre of straight-line distance
  /// for a HAND order: path factor / (real walking speed / kClockScale).
  /// The canonical 5 km/h at factor 1.0 gives 2.4 — which puts a 2-hour
  /// limit at ~830 m, the design's "on foot that is ~800 m" (time §7).
  float walk_hours_per_km = 2.4F;

  /// The same for a HARNESSED order: the plowman rides out with his horse
  /// instead of walking (decision 103), so plowing and harrowing reach
  /// farther in the same hours. 12 km/h at factor 1.0 gives 1.0.
  float harness_hours_per_km = 1.0F;

  /// One-way travel limit in game hours: a job farther than this from a
  /// worker's home cannot take him at all. Four hours by decision 109, one
  /// rule for a unit's staff and an open field alike (time design §7); the
  /// threshold and the day's output measure the SAME shoulder, which is why
  /// the two speeds above serve both (decision 103).
  float travel_limit_hours = 4.0F;

  /// Less daylight than this left after the road, and the job is not worth
  /// walking to at all. ASSUMPTION.
  float min_usable_hours = 1.0F;

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
