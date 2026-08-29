/// @file
/// @brief WorkKind and WorkAssignment — the per-resident labor state.
/// @threading PARALLEL_READONLY
/// The assignment block is embedded in ResidentRow (resident_state.h) and
/// follows its discipline: in phase 1 every write is sequential — the labor
/// sub-step of the decisions slot assigns in the morning, drains hourly and
/// closes accrual at day end, all on the sim thread. Parallel phases may
/// read it from `previous` like any resident field.
///
/// Design sources: time design §6-§8 and §11 (the workday by the sun, the
/// road limit, the fatigue walk-off), society design §1 (the accountant's
/// placement), labor-payment §2 (a trudoden is a WORK NORM, not attendance:
/// an assigned but idle worker earns nothing — the boss digest of
/// 2026-08-29 §2.4 confirms), livestock design §5 (a resident hosting a
/// kolkhoz horse takes only horse work until the kolkhoz yard exists).
///
/// The model itself — what generates jobs, how the day window and the norm
/// conversion work — is the core's own decision, manual/65-labor-model.md.

#ifndef CORE_COMMON_LABOR_STATE_H_
#define CORE_COMMON_LABOR_STATE_H_

#include <cstdint>

#include "core_common/ids.h"

namespace core {

/// @brief What a resident is assigned to today. The four field kinds mirror
/// the working phases of FieldPhase (land_state.h); kHerdCare is the daily
/// barn work at a unit-standing herd. Values are also the row keys of the
/// work-rate table (tables/labor.csv): the trudoden rate and the hardness of
/// each kind are balance data, never code.
enum class WorkKind : std::uint8_t {
  kNone = 0,   ///< Not assigned today (or walked off; see ResidentRow docs).
  kPlowing,    ///< Horse work: needs an adult kolkhoz horse teamed in.
  kHarrowing,  ///< Horse work, like plowing.
  kSowing,     ///< By hand in Epoch I.
  kHarvest,    ///< By hand in Epoch I; the heaviest window of the year.
  kHerdCare,   ///< Feeding, milking, mucking at a unit-standing herd.
};

inline constexpr std::uint32_t kWorkKindCount = 6;

/// @brief True for kinds that harness a horse: the crew is capped by adult
/// kolkhoz horses, and residents hosting a kolkhoz horse at their yard are
/// assignable ONLY to these kinds (start canon, livestock design §5).
constexpr bool IsHorseWork(WorkKind kind) {
  return kind == WorkKind::kPlowing || kind == WorkKind::kHarrowing;
}

/// @brief The assignment block of one resident. Plain data.
/// Exactly one target id is valid, matching the kind: a field for the four
/// field kinds, a herd for kHerdCare, neither for kNone. Travel time and
/// eligibility are NOT stored — they are pure functions of positions and
/// state (state model law: derived values are recomputed, never cached in
/// state).
struct WorkAssignment {
  WorkKind kind = WorkKind::kNone;

  FieldId field;  ///< Valid for the field kinds; invalid otherwise.

  HerdId herd;  ///< Valid for kHerdCare; invalid otherwise.

  /// Norm-days of output delivered since the day started, in game man-days
  /// of the assigned kind. Accumulated hourly while working; converted into
  /// trudodni on the family account at day close (rate x delivered), then
  /// reset. Survives a mid-day walk-off: partial output is still paid.
  float worked_norm_days_today = 0.0F;
};

}  // namespace core

#endif  // CORE_COMMON_LABOR_STATE_H_
