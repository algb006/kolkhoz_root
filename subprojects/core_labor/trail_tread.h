/// @file
/// @brief The morning's trodden paths (#2072; core_common/trail_state.h): the day's decay
///        before the placement, the placed walkers' tread after it, and the road index
///        re-trailed - the labour's two steps of the trail rules.
/// @threading SINGLE_THREADED
/// Phase 3 (decisions and assignment), inside the morning placement.

#ifndef CORE_LABOR_TRAIL_TREAD_H_
#define CORE_LABOR_TRAIL_TREAD_H_

#include <cstdint>
#include <span>

#include "core_common/trail_state.h"
#include "core_common/world_state.h"

namespace core {

struct AssignmentCandidate;
struct AssignmentJob;

/// @brief Rules 2-4 once a morning: writes the rules' weight and visible strength into the
///        table (the loader and the session read them there), decays every trail by today's
///        snow (snow_cover_days, today's precipitation), and re-trails the index.
void DecayTrailsThisMorning(const TrailRules& rules, WorldState& current);

/// @brief Rule 1 for the morning's placed walkers: each candidate the plan gave a job,
///        walking (rides_horse 0 and in no cart), treads the open-ground legs of his way from
///        home to the job - one pass a day. Rule 9: a leg whose midpoint lies on a field with
///        a standing crop treads nothing. Re-trails the index after.
/// @param plan Per candidate: the job index, or kNoJobAssigned (PlanDayAssignments).
/// @param rides_horse Per candidate, 1 on a horse; may be shorter (then nobody rides).
/// @param rides_cart_with Per candidate, a driver's candidate index or kNoJobAssigned.
void TreadPlacedWalkers(const TrailRules& rules,
                        WorldState& current,
                        std::span<const AssignmentJob> jobs,
                        std::span<const AssignmentCandidate> candidates,
                        std::span<const std::uint32_t> plan,
                        std::span<const std::uint8_t> rides_horse,
                        std::span<const std::uint32_t> rides_cart_with);

}  // namespace core

#endif  // CORE_LABOR_TRAIL_TREAD_H_
