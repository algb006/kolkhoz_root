/// @file district_reputation.h
/// @brief THE RATING EARNED BEYOND THE PLAN (#1043; district.md «Рейтинг райкома
///        упирается в потолок»): the year's own overfulfilment and a district
///        visit's recovery raise chairman.raikom_reputation, beside the plan's
///        own verdict (district_plan.h, JudgePlan); every writer of the rating
///        books what it moved in the year's ledger (save 166).
/// @threading SINGLE_THREADED - the year's turn and the district's visit
///        (phase 6).
///
/// WHY (host's measure of 17 October 2026): by year 12 the rating stood at 98
/// in every canon and novice village, spread 0 - the plan met every year and
/// +4 a year ran to the ceiling, so the bands measured time, not play. boss's
/// decision on econ's reading: the plan met gives less, and the rating is
/// earned beyond it, each gain capped - no pump.
///
/// THE STEPS ARE campaign.csv's (production_config.h): zero by default, so
/// today's world stands until host's pair. The ledger's grounds are booked
/// whatever the cells.

#ifndef CORE_PRODUCTION_DISTRICT_REPUTATION_H_
#define CORE_PRODUCTION_DISTRICT_REPUTATION_H_

#include <cstdint>

#include "core_catalog/limit_catalog.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The year's overfulfilment the rating may count, tonnes of grain
///        equivalent, >= 0: over the SOWING PLAN'S positions only - grain and
///        potato, not milk nor the winter milk outside the plan (boss, the
///        #1043 thread [6]; the limit points keep them,
///        PlanOverfulfilGrainTonnes) - less the short positions' deduction by
///        the limit's own factor, less the grain the district LENT this year
///        (YearLedger::goods_loan_taken): bought grain delivered above the plan
///        buys no rating («a cumulative counter that a sale can raise is a
///        pump»). THE ONE OUTSIDE DOOR TODAY: the barter is yard to yard, the
///        night trader takes from the stores, the plan trade lowers the due,
///        and the limit catalogue's seed_grain_lot is UNPRICED - the day it gets
///        a price it is a second door into the stores and comes off here too.
float OwnOverfulfilGrainTonnes(const ProductionConfig& config, const WorldState& current);

/// @brief The overfulfilment's tiers REACHED, 0..3, by the limit's own three
///        (limit_catalog.h, OverfulfilPoints): any tonne over is the first,
///        past overfulfil_tier1_t the second, past tier1 + tier2 the third.
std::uint32_t OverfulfilTiersReached(const LimitCatalog& catalog, float grain_tonnes);

/// @brief At the year's turn, beside JudgePlan: books the own over-tonnes and
///        the tiers they reach (YearLedger), and steps the rating by
///        plan_overfulfil_reputation_per_tier a tier, capped by
///        plan_overfulfil_reputation_max, clamped to the scale.
void StepReputationBeyondPlan(const ProductionConfig& config, WorldState& current);

/// @brief A district visit that found nothing: counted in the year's book; and,
///        ONLY while a recovery is owed (ChairmanState::reputation_recovery_owed
///        - a failed plan year or a found discrepancy since), the rating steps
///        by district_visit_clean_reputation and the debt clears (boss on
///        econ's estimate, the #1043 pair thread [7]).
void StepReputationForCleanVisit(const ProductionConfig& config, WorldState& current);

/// @brief A failed plan year or a found discrepancy: the next clean visit
///        owes its recovery step.
void OweReputationRecovery(WorldState& current);

/// @brief Books in the year's ledger what a loss took off the rating since
///        `before` (the seizure, the plan trade).
void BookReputationLoss(WorldState& current, float before);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_REPUTATION_H_
