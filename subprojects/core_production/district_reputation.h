/// @file district_reputation.h
/// @brief THE RATING EARNED BEYOND THE PLAN (#1043; district.md «Рейтинг райкома
///        упирается в потолок»): the year's own overfulfilment and a clean
///        district visit raise chairman.raikom_reputation, beside the plan's
///        own verdict (district_plan.h, JudgePlan).
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
/// today's world stands until host's pair.

#ifndef CORE_PRODUCTION_DISTRICT_REPUTATION_H_
#define CORE_PRODUCTION_DISTRICT_REPUTATION_H_

#include <cstdint>

#include "core_catalog/limit_catalog.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The year's overfulfilment the rating may count: `overfulfil_tonnes`
///        (PlanOverfulfilGrainTonnes, the limit points' own figure) less the
///        grain the district LENT this year (YearLedger::goods_loan_taken, in
///        grain equivalent), floored at nought. Grain bought into the stores
///        and delivered above the plan buys no rating («a cumulative counter
///        that a sale can raise is a pump»; boss, the #1043 thread [2]-[3]).
///        THE ONE OUTSIDE DOOR TODAY: the barter is yard to yard, the night
///        trader takes from the stores, the plan trade lowers the due, and
///        the limit catalogue's seed_grain_lot is UNPRICED - the day it gets a
///        price it is a second door into the stores and comes off here too.
/// @return Tonnes of grain equivalent, >= 0.
float OwnOverfulfilGrainTonnes(const ProductionConfig& config,
                               const WorldState& current,
                               float overfulfil_tonnes);

/// @brief The overfulfilment's tiers REACHED, 0..3, by the limit's own three
///        (limit_catalog.h, OverfulfilPoints): any tonne over is the first,
///        past overfulfil_tier1_t the second, past tier1 + tier2 the third.
std::uint32_t OverfulfilTiersReached(const LimitCatalog& catalog, float grain_tonnes);

/// @brief At the year's turn, beside JudgePlan: the rating steps by
///        plan_overfulfil_reputation_per_tier per tier the year's OWN
///        overfulfilment reaches, capped by plan_overfulfil_reputation_max,
///        clamped to the metric's scale.
/// @param overfulfil_tonnes PlanOverfulfilGrainTonnes, read before the plan's
///        verdict hands the next year's plan down.
void StepReputationBeyondPlan(const ProductionConfig& config,
                              WorldState& current,
                              float overfulfil_tonnes);

/// @brief A district visit that found nothing, whatever called it: the rating
///        steps by district_visit_clean_reputation, clamped (boss, the #1043
///        thread [2] (b)).
void StepReputationForCleanVisit(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_REPUTATION_H_
