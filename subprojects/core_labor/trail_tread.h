/// @file
/// @brief The morning's trodden paths (#2072; core_common/trail_state.h): the day's decay
///        before the placement and the placed people's tread after it - the labour's two steps
///        of the trail rules.
/// @threading SINGLE_THREADED
/// Phase 3 (decisions and assignment), inside the morning placement.

#ifndef CORE_LABOR_TRAIL_TREAD_H_
#define CORE_LABOR_TRAIL_TREAD_H_

#include <cstdint>
#include <span>
#include <vector>

#include "core_common/ids.h"
#include "core_common/trail_state.h"
#include "core_common/world_state.h"

namespace core {

struct AssignmentCandidate;

/// @brief Rules 2-4 once a morning: lays yesterday's pending tread (WorldState::walks, save
///        229) on the trails, writes the rules' weight, visible strength and ground into the
///        table (the loader and the session read them there), decays every trail by today's
///        snow, keeps `yesterday` as WorldState::walks.yesterday, and RE-DATES the index with
///        those walks priced by the route - the only place the day's trails take effect, so
///        yesterday's tread counts from this morning and today's from tomorrow's.
/// @param yesterday PlacedWalksOf the world before StartDay cleared yesterday's work.
void DecayTrailsThisMorning(const TrailRules& rules,
                            WorldState& current,
                            std::vector<WalkPair> yesterday);

/// @brief The placed walkers' ways in `current` as its work stands (call BEFORE the morning
///        clears it): who walks (WorkRidesOut false) home → work, who rides from the horse
///        yard home → yard, every other rider none - the tread's own predicates.
std::vector<WalkPair> PlacedWalksOf(const WorldState& current, LivestockKindId horse_kind);

/// @brief Rule 1 for the morning's placed people, after their work is written: who walks
///        (WorkRidesOut false) treads his lattice route home → work; who rides from the horse
///        yard treads his walk home → yard; every other rider treads nothing (rule 8). Rule 9:
///        an edge whose midpoint lies on a field with a standing crop treads nothing. Every
///        route is taken on the index as it stood this morning, before any edge is trodden, and
///        the index is NOT re-dated here: today's tread changes tomorrow's ways (static review
///        of 23 Nov 2026: it changed today's travel times). The passes go to
///        WorldState::walks.pending, not the trails (save 229): the next morning lays them.
/// @param plan Per candidate: the job index, or kNoJobAssigned (PlanDayAssignments).
/// @param horse_kind The horses' kind: where the yard's horses stand (HorseYardPositionOf).
void TreadPlacedWalkers(const TrailRules& rules,
                        WorldState& current,
                        std::span<const AssignmentCandidate> candidates,
                        std::span<const std::uint32_t> plan,
                        LivestockKindId horse_kind);

}  // namespace core

#endif  // CORE_LABOR_TRAIL_TREAD_H_
