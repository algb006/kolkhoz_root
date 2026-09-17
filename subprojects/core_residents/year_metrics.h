/// @file
/// @brief What the year's book needs from the households and nothing else:
/// the settlement's days of food, and the two columns the era-readiness
/// index reads off the families (epochs design §6; boss, parcels 128-132).
/// @threading SINGLE_THREADED
/// Called from the sequential decisions slot of core_residents, once a day.
///
/// WHY THESE LIVE HERE AND NOT WITH THE COUNTER THAT READS THEM. The index
/// is summed by core_world, which is the one module allowed to name every
/// other — but a ledger column filled from outside the module that owns its
/// rule is the first half of two homes for one number. Satisfaction and the
/// household plot are this module's rules, so this module books them.

#ifndef CORE_RESIDENTS_YEAR_METRICS_H_
#define CORE_RESIDENTS_YEAR_METRICS_H_

#include "core_common/world_state.h"
#include "food_config.h"
#include "life_config.h"

namespace core {

/// @brief Game days the settlement's food covers at today's need — stores
/// and family pantries together, through calories, because the norm is a
/// grain EQUIVALENT and a tonne of potatoes is not a tonne of rye.
///
/// STORES AND PANTRIES BOTH. A count of the stores alone goes short every
/// spring while the larders are full, and a forecast that cries wolf is one
/// nobody reads.
///
/// @return kStockForecastHorizonDays when the stock outlasts the horizon,
///         and the same saturation when nobody eats — an empty village does
///         not run out.
float SettlementFoodDays(const FoodConfig& food, const LifeConfig& life, const WorldState& world);

/// @brief Books the day into the year's book: every family's satisfaction,
/// one person-day for every able-bodied villager, and — on the first of
/// December — the days of food the wintering starts with.
///
/// READ AFTER THE METRICS PHASE HAS RUN, so the satisfaction sampled is the
/// one today's meal and today's rest produced. One reading per family per
/// day, summed with its own count beside it because the village gains and
/// loses families all year and a mean kept in one float would weigh a
/// January of twenty-one households against a December of forty.
///
/// @param world The day's world; only `ledger.current` is written.
void AccumulateYearMetrics(const FoodConfig& food, const LifeConfig& life, WorldState& world);

}  // namespace core

#endif  // CORE_RESIDENTS_YEAR_METRICS_H_
