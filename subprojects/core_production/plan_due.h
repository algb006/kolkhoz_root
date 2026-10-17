/// @file plan_due.h
/// @brief ONE HOME FOR A PLAN POSITION'S DUE (#1036): what the district asks
///        of a position, for the letter and for every forecast that holds
///        stores against it.
/// @threading SINGLE_THREADED - the year's turn (phase 6) and the plan's
///        lamps and holds (phases 3-6), pure reads of the world.
///
/// WHY ONE HOME (boss, the #1036 thread [2]): the January letter
/// (AnnouncePlan), the next year's hold (NextYearHold through
/// NextPlanOwedGrams), the turn's seal (TurnPlanSealOf) and the uncovered lamp
/// (CoveredPositions) all priced a position by its fixed share of the worked
/// ha. A split moved in the letter alone would ask the village one figure
/// while it held its stores for another.
///
/// THE RULE (district.md «Раскладка по культурам — по плану сева этого
/// года»; econ's plan-by-sown-mix §2):
/// - (a) the grain TOTAL stays on the worked ha, as before, and is split by
///   the year's planned sowing: k = sum of y_c x area x share_c over sum of
///   y_c x planned_c, and position c is priced on k x planned_c hectares.
///   A crop the plan does not sow owes nothing that year. With nothing
///   planned at all the split falls back to the fixed shares.
/// - (b) the total is multiplied by the band factor of the district rating
///   (chairman.raikom_reputation). Not milk (its own position), not the first
///   year (the start stock's rule, AnnouncePlan). A forecast reads TODAY's
///   band; the rating may move by the turn - a named limit.
/// Both off by default (campaign.csv); off, every figure is today's.

#ifndef CORE_PRODUCTION_PLAN_DUE_H_
#define CORE_PRODUCTION_PLAN_DUE_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

class ITableSet;

/// @brief The district rating's band factor on the grain total, by
///        world.chairman.raikom_reputation (bands 0-20, 21-40, 41-60, 61-80,
///        81-100; ProductionConfig::plan_band_factor).
float PlanBandFactor(const ProductionConfig& config, const WorldState& world);

/// @brief THE HECTARES position `index` is priced on: `area_ha` x its share
///        with the split off; with it on, k x the position's planned hectares
///        of chain year `year` (0 this, 1 next, 2 the one after; plan_alarms.h,
///        PlannedChainHectares), as of `as_of`. Times the band factor either way.
/// @return 0 for an index past the positions or a crop the tables lack.
float PlanPricedHectares(const ProductionConfig& config,
                         const WorldState& world,
                         std::size_t index,
                         float area_ha,
                         std::uint32_t year,
                         SimDay as_of);

/// @brief Grams position `index` owes, priced on PlanPricedHectares: its
///        crop's yield x those hectares x plan_grain_share. With the split
///        and the band off it is exactly PlanPositionGrams (district_plan.h).
Grams PlanDueGrams(const ProductionConfig& config,
                   const WorldState& world,
                   std::size_t index,
                   float area_ha,
                   std::uint32_t year,
                   SimDay as_of);

/// @brief Reads campaign.csv `plan_split_by_sown_mix` (0/1) and
///        `plan_band_factor_0_20` ... `plan_band_factor_81_100` (0..5); absent
///        rows keep the defaults (off, 1.0).
/// @return false with `error` naming the key for a value out of range.
bool ParsePlanDue(const ITableSet& tables, ProductionConfig& config, std::string& error);

}  // namespace core

#endif  // CORE_PRODUCTION_PLAN_DUE_H_
