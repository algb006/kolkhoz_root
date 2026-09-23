/// @file
/// @brief Whether a resident is away in the district at a tick — the one
/// question every reader of ResidentRow::away_* asks (district_car_state.h).
/// @threading PARALLEL_READONLY
/// A pure function of one row and a tick: no state, readable from any phase
/// and any thread — the family meal asks it from the parallel needs slot.
///
/// WHY SHARED. Being away touches four modules that may not name each other:
/// labor does not send him to work, the family's table does not feed him,
/// the metrics do not judge him, and the boundary says where he is. Four
/// copies of "is the tick before his return" would drift the day one of them
/// forgot the hour.

#ifndef CORE_COMMON_AWAY_IN_DISTRICT_H_
#define CORE_COMMON_AWAY_IN_DISTRICT_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/district_car_state.h"
#include "core_common/resident_state.h"

namespace core {

/// @brief The tick the resident is home again: `away_until_day` at
/// `away_until_hour`. Meaningful only while he is away — a reason that is
/// neither kNone nor kAwaitingAmbulance (the wait has no term).
constexpr Tick AwayUntilTick(const ResidentRow& resident) {
  return (static_cast<Tick>(resident.away_until_day) * kTicksPerDay) + resident.away_until_hour;
}

/// @brief Whether the resident is out of the village at `now`: in the
/// district or on the road home from its border.
///
/// THE RETURN TICK ITSELF STILL COUNTS AS AWAY, and that is the fix of a
/// one-tick gap (the static loop of 23 September): the production decisions
/// that bring him home — or first set him walking — run AFTER the family
/// meal and labor in the same step, so a strict `<` showed him home to every
/// earlier reader of that tick, and labor could give a man still on the road
/// his day's work. He is home from the tick after production clears it.
constexpr bool AwayInDistrict(const ResidentRow& resident, Tick now) {
  return resident.away_reason != static_cast<std::uint8_t>(AwayReason::kNone) &&
         resident.away_reason != static_cast<std::uint8_t>(AwayReason::kAwaitingAmbulance) &&
         now <= AwayUntilTick(resident);
}

/// @brief Whether the resident takes no work at `now`: away in the district,
/// or lying at home for the car that is coming (boss seq 1, answer 3 — from
/// the sending to the car there are no working hours, so none go unpaid when
/// it takes him). The question every reader that HANDS OUT WORK asks; the
/// family's table and the layer ask AwayInDistrict, because a man waiting
/// for the car is at home and eats at home.
constexpr bool OffWork(const ResidentRow& resident, Tick now) {
  return resident.away_reason == static_cast<std::uint8_t>(AwayReason::kAwaitingAmbulance) ||
         AwayInDistrict(resident, now);
}

/// @brief Whether he is on the last stretch of it — walking in from the
/// district's border (the layer draws him on the road, Whereabouts
/// kOnTheRoad) — rather than in the district itself.
constexpr bool WalkingHomeFromDistrict(const ResidentRow& resident, Tick now) {
  return AwayInDistrict(resident, now) && resident.away_walk_hours > 0 &&
         now + resident.away_walk_hours >= AwayUntilTick(resident);
}

}  // namespace core

#endif  // CORE_COMMON_AWAY_IN_DISTRICT_H_
