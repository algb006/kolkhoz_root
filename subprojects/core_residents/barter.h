/// @file
/// @brief The yards' exchange at the barter counter: who has a surplus and
/// who a lack, the dry count that raises «жителям есть что менять», the
/// evening's walk and the settlement between pantries (needs design §6,
/// «Эпоха I — место обмена»; the human's «По рынку делай», 2026-10-01;
/// boss-all-barter-counter-go-2026-10-01 [1]-[7]; econ's
/// barter-counter-2026-10-01, parts 1-2).
/// @threading SINGLE_THREADED
/// Runs in the residents' decisions sub-step (phase 3) on the sim thread: it
/// writes two families' pantries in one move, the evening's trips
/// (WorldState::barter_trips), the dry count (WorldState::barter), the year's
/// book (`bartered`) and the step's events, so it can only live in a
/// sequential slot.
///
/// THE CONTRACT OF 0.37.66: the declarations below have no bodies yet. The
/// dry count and the fact come with the next delivery, the walk and the
/// settlement with the one after it; nothing calls them until then.
///
/// WHAT IS EXCHANGED, AND BY WHAT MEASURE. Food of the yards' pantries only —
/// the rows of food.csv; the kolkhoz stores take no part; the crafts' goods
/// are not this work. The measure is the grain equivalent, one for one:
/// grams x kcal_per_gram over ConsumptionConfig::grain_reference_kcal_per_gram,
/// the distribution's own expression. A yard carries off as much equivalent
/// as it brought; the counter keeps no stock, and what found no pair goes
/// home.
///
/// WHO GIVES AND WHO TAKES (econ, part 1 §1.3; four STUB thresholds, all of
/// them thresholds and none a price). D is the yard's daily need in the
/// grain equivalent (DailyNeedKilograms over its eaters); a category's stock
/// is counted in days of D.
///   * A yard GIVES (a) a PERISHABLE — a food that keeps no longer than
///     `perishable_max_keep_days` — above `perishable_share_of_need` of D:
///     what the yard eats of one food TODAY. The milk is a flow, tomorrow
///     brings its own, so the shelf life does not multiply the share. The
///     family meal itself has no cap on a category's share (it eats the
///     perishable first up to the whole need), and until 0.37.68 the whole
///     of D x the shelf life stood here for that reason: about 64 kg of milk
///     a grown eater, a stock the first print found in no yard of nine
///     villages in two years (boss [11], econ [13]);
///     (b) of a category that has a HARVEST (crops.csv), what is above the
///     yard's need until the next one — D x the days to the month the
///     soonest crop of the category is reaped from. The bread a yard lives
///     on till summer is no surplus: «twelve days of the largest category»,
///     the measure until 0.37.68, called 29 yards of 32 rich in bread in
///     December. A category nothing is reaped into has no (b).
///   * A yard TAKES a category it holds less than `lack_share_of_need` of D
///     of, for `take_days_ahead` days at that share a day, and of a
///     perishable no longer than its shelf life.
///   * A HUNGRY yard — less than `hungry_days` of D in all — gives (a) only
///     and takes the richest in calories on offer: the last bread is not
///     exchanged for milk.
///
/// THE SETTLEMENT is one calculation for all the yards that came, by shares,
/// as the distribution against trudodni is: where a resource is asked for
/// beyond what is offered, every taker gets his share of it. Not a queue by
/// rows.
///
/// THE WALK. A yard goes only if it has something to give or to take. One
/// adult of it who is free in that hour, chosen deterministically; none free
/// — the yard does not go today. To the nearest standing `barter_place`
/// within `walk_limit_hours` there and back on foot (RoadKm(kWalk) x the
/// walking pace of labor.csv); a yard beyond it does not exchange at all.
/// Out after the working day, the settlement in `hour`, home before sleep;
/// `hour` is neither 22 nor 23, the hours the day's pantry flows are booked
/// as the plot's harvest and as eaten (core_world, FoldPantryFlows).
///
/// STUB, each named: the walk costs the yard no household hours and no rest
/// (a second shift of the balance in one delivery; boss [7], fork 4); no
/// path is trodden to the counter (the traffic is not counted yet).

#ifndef CORE_RESIDENTS_BARTER_H_
#define CORE_RESIDENTS_BARTER_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/barter_view.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

struct FoodConfig;

/// @brief The exchange's numbers (world_params.csv). Defaults are econ's and
/// boss's figures of 2026-10-01, every one a STUB the first print measures.
struct BarterConfig {
  /// unit_types.csv `barter_place`; invalid in a table set without it, and
  /// then nobody walks.
  UnitTypeId counter_type;

  /// A food that keeps no longer than this is a perishable, and rule (a) is
  /// its rule: milk and fish (2 days), the egg (6).
  /// `barter_perishable_max_keep_days`, game days (STUB: two months of four).
  float perishable_max_keep_days = 8.0F;

  /// The month each resource's harvest opens in, 1..12, dense by ResourceId;
  /// 0 for a resource no crop is reaped into (crops.csv `resource`,
  /// `harvest_from_month`, the soonest of its crops). Rule (b) reads the
  /// days to it.
  std::vector<std::uint8_t> harvest_month_by_resource;

  /// A category is lacking when the yard holds less than this share of its
  /// daily need of it. `barter_lack_share_of_need`, 0..1 (a third: the three
  /// categories of Epoch I's norm).
  float lack_share_of_need = 1.0F / 3.0F;

  /// A taker takes for this many days ahead, at `lack_share_of_need` a day.
  /// `barter_take_days_ahead`, days.
  float take_days_ahead = 4.0F;

  /// A yard with less than this many days of its need in all is hungry.
  /// `barter_hungry_days`, days of D.
  float hungry_days = 4.0F;

  /// The fact «жителям есть что менять»: the dry count meets the three
  /// thresholds below this many days running. `barter_fact_days_in_row`.
  std::uint32_t fact_days_in_row = 3;

  /// Yards that would give, and yards that would take, at least this many
  /// each. `barter_fact_yards_each_side`.
  std::uint32_t fact_yards_each_side = 3;

  /// The equivalent that would change hands, at least this share of the
  /// village's daily need. `barter_fact_share_of_village_need`, 0..1.
  float fact_share_of_village_need = 0.05F;

  /// The hour of the settlement at the counter, 0..21 — never 22 or 23 (the
  /// file's note). `barter_hour`.
  std::uint8_t hour = 19;

  /// The longest walk to the counter and back, hours on foot.
  /// `barter_walk_limit_hours`.
  float walk_limit_hours = 2.0F;

  /// What of its daily need a yard eats of ONE perishable food in a day:
  /// above this share of D the food is offered (rule (a)).
  /// `barter_perishable_share_of_need`, 0..1 (STUB: a third, as
  /// `lack_share_of_need`).
  float perishable_share_of_need = 1.0F / 3.0F;
};

/// @brief The world_params.csv keys this file reads, for the assembly's
/// declared-readers check.
std::span<const std::string_view> BarterWorldParamKeys();

/// @brief Reads the knobs and finds the counter's unit type.
/// @return false with `error` naming the key for a value out of range — a
///         share outside 0..1, a day count below nought, an hour past 21, a
///         walk's limit below nought.
bool ParseBarterConfig(const ITableSet& tables, BarterConfig& config, std::string& error);

/// @brief THE DRY COUNT, once a day, in `config.hour`: what the yards would
///        exchange today if a counter stood at every gate — no walk, no
///        counter, nothing moved. In the counter's hour and not at the day's
///        turn: after the day's milk and catch have reached the pantries and
///        BEFORE the dinner, which eats the perishable first — a count after
///        it finds the surplus eaten (econ [13]). Writes BarterWatch's three
///        counters and the days in a row they met their thresholds; on the
///        day that run reaches `fact_days_in_row` raises
///        kBarterWorthStarting, once a campaign. Does nothing in any other
///        hour.
/// @param life_speedup LifeConfig::life_speedup, for the eaters' ages.
/// @pre Called every tick of the decisions slot, before RunBarterEvening.
void RunBarterDryCount(const BarterConfig& config,
                       const FoodConfig& food,
                       float life_speedup,
                       WorldState& current);

/// @brief The dry count's own working, by yard and resource
///        (core_common/barter_view.h): what each yard would bring, ask, hand
///        over and carry home, counted now off `world` by the very rules
///        RunBarterDryCount counts by — the same yards, the same settlement,
///        with the flows kept. A pure read at any hour; BarterWatch and the
///        events are not touched.
/// @return Lines in family row order, then resource order; none for a yard
///         and a resource with nothing on any side.
std::vector<BarterYardLine> BarterDryLines(const BarterConfig& config,
                                           const FoodConfig& food,
                                           float life_speedup,
                                           const WorldState& world);

/// @brief THE EVENING AT THE COUNTER, called every tick: in the hour the
///        walkers leave, yesterday's trips are dropped and today's laid down
///        (one a yard with something to give or take and a free adult, to
///        the nearest counter within the limit); in `config.hour` the yards
///        at each counter settle — pantries, the trips' two amounts, the
///        book's `bartered`, one kBarterDay a counter where anything changed
///        hands. Does nothing in any other hour, and nothing in a village
///        with no standing counter.
/// @param walk_hours_per_km The walking pace of labor.csv
///        (ActivityRules::walk_hours_per_km), handed in by the assembly.
/// @post Each trip's given_equivalent equals its taken_equivalent within the
///       grams' rounding, and the village's food is what it was.
void RunBarterEvening(const BarterConfig& config,
                      const FoodConfig& food,
                      float life_speedup,
                      float walk_hours_per_km,
                      WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_BARTER_H_
