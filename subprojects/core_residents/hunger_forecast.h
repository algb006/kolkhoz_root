/// @file hunger_forecast.h
/// @brief THE HUNGER LAMP'S YELLOW STAGE (#1046): which families will run
///        short of food before the horizon, and how soon.
/// @threading SINGLE_THREADED - the residents' alarm collection, between
///        steps: it calls the issue norm (issue_norm.h) and FoodConfig's
///        production callbacks, which may not run on a worker.
///
/// STUB: THE YELLOW DOES NOT LIGHT (world_params `hunger_forecast_lit` 0;
/// boss, the #1046 thread [20], econ forecast-lamp §9.5). Its best arm (C+
/// at c 1.0, 0.37.294) kept its promise - no hunger in 107 episodes without a
/// yellow before it, lead median 19 days - but lit 32 days of the 48-day year
/// in one canon village in ten, against the 24 set before the run, and only
/// 41 % of its runs were followed by hunger. A deferral of the human's
/// approved yellow (30 September, «делай»), not a cancellation: the code
/// stands, ForecastHunger and its units run, and residents_system.cpp does
/// not collect it. RETURN CONDITION (boss [20]; econ §9.5): the family-split
/// term; the false runs printed by season (half of them in year-days 24-35)
/// and by family size; the red's village-share margin (boss's item); barley's
/// overshoot (its issue forecast P10 0 on the canon); and the working ages
/// outside the 16-60 band (families of no worker by the band drew the issue).
///
/// THE CONTRACT IS econ's (econ/manual/audit/forecast-lamp-2026-09-30.md §2;
/// boss, the #1046 thread [2]). A family is UNDER THREAT when, by the horizon
/// H, its pantry + the issue to come + its own food < the members' need × H ×
/// c (world_params `hunger_forecast_share`, STUB 0.9):
/// - the issue to come, by position: the norm in force (ResolveIssueNorms,
///   the distribution's own door) × the position's kcal × ForecastTrudodni to
///   min(H, the position's harvest), capped by the position's free stock ×
///   the family's share: its workers against the village's (CountHousehold,
///   the advance's door; econ §6 (a1), the #1046 thread [6]). A family with
///   no worker forecasts no issue;
/// - THE INFLOWS INSIDE H (#1046 (b); econ §7.2 and §8; boss, the #1046
///   thread [12], [17]), by the bits of world_params `hunger_forecast_terms`
///   (STUB 13): bit 1, a
///   position whose harvest falls inside H adds, from the harvest's day to H,
///   the world's own norm after it - the standing crop and its heaps
///   (IProductionSystem::StandingHarvest), at most this year's harvest net of
///   next year's seed, less the plan still owed, over the trudodni to the
///   next year's harvest (a NAMED DEVIATION: the funds of the harvest's day
///   cannot be read today, so the plan owed and the seed stand for them); bit
///   8, the crop first fills what the stores lie below today's funds (arm
///   C+); bit 2, the milk ahead (arm BM), left with 0.37.295; bit 4, the
///   family's own garden on its day (household_plot.h, ForecastGarden; arm
///   C). The family is under threat when it runs short by the end of ANY of
///   the spans to H, to the first such harvest (econ's U1: a crop of day 12
///   feeds nobody on day 5) and to its garden's day (U4), each on the
///   inflows that land inside it;
/// - its own food: FamilyRow::own_kcal_mean × H (an exponential mean, the
///   contract's named deviation from «the last 7 days' mean»);
/// - the need: DailyNeedKilograms of every member at home, in the grain
///   equivalent's kcal - the meal's own norm;
/// - H: the nearest harvest of a position giving the family more than a third
///   of its issue's kcal, at most kHungerForecastHorizonDays.
/// The yellow lights when the families under threat are at least the red's
/// share (kHungerLampShare) of the families with members.
///
/// NAMED DEVIATIONS (said to boss with the delivery):
/// - kLowerIssueNorm is named while a chairman's norm (IssueNormBasis::
///   kChairman) stands on any food position: the default it would be lowered
///   to is not computed, so a chairman's norm BELOW the default is advised
///   too;
/// - the yellow carries no clear margin of its own: it is recomputed daily
///   from the stocks, which do not jitter as the members' satiety does.

#ifndef CORE_RESIDENTS_HUNGER_FORECAST_H_
#define CORE_RESIDENTS_HUNGER_FORECAST_H_

#include <cstdint>
#include <vector>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "food_config.h"

namespace core {

/// The forecast's horizon cap, days (econ §2: «не дальше 24 суток - полгода:
/// весна видна с осени»).
inline constexpr std::int32_t kHungerForecastHorizonDays = 24;

/// @brief The village's forecast of the coming hunger.
struct HungerForecast {
  /// The families under threat, in table order.
  std::vector<FamilyId> under_threat;

  /// The families with anybody in them, the share's denominator.
  std::uint32_t families_with_members = 0;

  /// Whole days from today to the first day a family under threat runs short
  /// (its pantry spent at the day's shortfall), at most the horizon; 0 when
  /// none is under threat.
  std::uint16_t days_to_first_short = 0;
};

/// @brief Forecasts every family's food to the horizon (the file header's
///        rule).
/// @param share_c The `c` the need is scaled by (hunger_forecast_share; the
///        probe passes 0.8, 0.9 and 1.0 in one run).
/// @param life_speedup Biological years per game year (life.csv).
/// @pre Called between steps or in the sequential decisions slot.
HungerForecast ForecastHunger(const FoodConfig& config,
                              const WorldState& world,
                              float life_speedup,
                              float share_c);

}  // namespace core

#endif  // CORE_RESIDENTS_HUNGER_FORECAST_H_
