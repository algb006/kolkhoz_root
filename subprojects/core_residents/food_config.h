// Internal to core_residents: the stage-6 food and household-plot
// configuration, parsed from tables/food.csv (scalar knobs + one row per
// food resource). The defaults here equal the canonical table contents so
// that a table set without the table (unit tests, early runs) behaves like
// the shipped one; a present-but-malformed table refuses the factory
// (the labor_config.cpp pattern: missing key = default, unreadable or
// out-of-range cell = error).
//
// Model: manual/66-food-model.md. Design sources: metrics design §8
// (satiety, variety), labor-payment design §3/§5/§7 (distribution, ration,
// auto-rules), household design §1 (plot time and its factors), decision
// 105 (life expectancy) and 106 (birth conditions) — the latter two extend
// LifeConfig, not this file.
//
// Units: the tables and this struct speak kilograms and hours, like the
// design documents; systems convert to Grams at the pantry boundary.

/// @threading PARALLEL_READONLY
/// Filled ONCE by the factory, before the system object exists, and never
/// written again. TWO parallel phases hold a pointer to it —
/// FamilyNeedsPhase and FamilyMetricsPhase — so every worker of slots 2 and
/// 5 reads it at the same time, and that is safe for exactly one reason:
/// nothing writes it.
///
/// It said slot 6 until 2026-09-07, and 6 was right until the seventh phase
/// was removed on 5 September: logistics stood at 4 and metrics behind it.
/// The number outlived the phase, and it did not merely go stale — slot 6 is
/// now kEvents, which is SEQUENTIAL, so the line named a place with no
/// workers in it at all while claiming to say who reads this concurrently.
///
/// SO: NO PER-STEP FIELD BELONGS HERE. A cache, a counter, anything the
/// step writes turns this from a constant into shared mutable state read by
/// every worker at once, and the compiler will not say a word.

#ifndef CORE_RESIDENTS_FOOD_CONFIG_H_
#define CORE_RESIDENTS_FOOD_CONFIG_H_

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/family_state.h"
#include "core_common/fund_ladder.h"
#include "core_common/ids.h"

namespace core {

class ITableSet;  // Defined in core_tables.

/// @brief Food category for the family variety mask (metrics design §8):
/// the mask bit index of FamilyRow::food_variety_mask. The roster is the
/// design's own nine-category list; a resource outside it is not food.
enum class FoodCategory : std::uint8_t {
  kBread = 0,        ///< Grain and groats.
  kPotato,           ///< The staple of the household plot.
  kVegetables,       ///< Cabbage, roots, onion.
  kDairy,            ///< Milk and what is made of it.
  kMeatAndEgg,       ///< One resource for meat (no sorts) plus eggs.
  kFish,             ///< Fishing and the artel.
  kFruitAndBerries,  ///< Orchard, forest, garden.
  kSweet,            ///< Honey; the shop later.
  kFats,             ///< Butter and oils; the shop later.
  kCount,
  kNotFood,  ///< The resource is not eaten (flax, firewood, manure).
};

static_assert(static_cast<std::size_t>(FoodCategory::kCount) == kFoodVarietyCategories,
              "FoodCategory's roster moved: FamilyRow::season_category_kcal is sized by it "
              "(family_state.h, kFoodVarietyCategories) and the save format with it");

/// @brief Per-resource food facts, dense by ResourceId (tables/food.csv,
/// one row per edible resource; resources absent from the table are
/// kNotFood with zeroes).
struct FoodResourceDef {
  /// Caloric density, kcal per gram (sim_v4 anchors: grain 3.3,
  /// potato 0.77, vegetables 0.25, milk 0.64, fish 0.9). Consumption norms
  /// are stated in grain equivalent; the ratio to grain's density converts.
  float kcal_per_gram = 0.0F;

  FoodCategory category = FoodCategory::kNotFood;

  /// The default distribution norm: kg issued per whole trudoden
  /// (labor-payment §3: the chairman's per-position norms; phase 1 has no
  /// player, the defaults rule). 0 = not issued automatically.
  float issue_kg_per_trudoden = 0.0F;

  /// The minimum-ration composition: kg per eater per day when the ration
  /// triggers (labor-payment §5: bread, potato, a little milk). 0 = not in
  /// the ration.
  float ration_kg_per_day = 0.0F;

  /// How much of the free stock the ISSUE BUNDLE may draw on, 0..1 (boss
  /// answer to question Q4, 2026-08-31: "half the milk goes into the
  /// bundle"). 1 = the farm hands the position out whole. The rest stays the
  /// kolkhoz's — for the plan, the calves and what the district asks next.
  float issue_share_of_stock = 1.0F;
};

/// @brief How much a person eats (metrics design §8): nothing until 1.5
/// biological years, a linear ramp to the adult norm at 16, a bit less in
/// old age, a bonus on heavy work days.
struct ConsumptionConfig {
  /// Adult norm in GRAIN-EQUIVALENT kilograms per game year. Anchor: the
  /// v4 balance feeds the settlement ~240 kg grain-eq per average eater a
  /// year; with the age pyramid the adult norm lands near 300.
  /// ASSUMPTION until the stage criterion run.
  float adult_kg_grain_eq_per_year = 300.0F;

  /// What one gram of the grain the norm is stated in is worth, kcal. The
  /// norm above is an EQUIVALENT, and this is the thing it is equivalent to:
  /// a resource converts by kcal_per_gram / this. Named as a knob rather
  /// than taken from some row of the roster, so that the reference cannot
  /// drift when the roster is reordered (sim_v4's KKAL['зерно']).
  float grain_reference_kcal_per_gram = 3.3F;

  float eat_from_bio_years = 1.5F;

  float adult_from_bio_years = 16.0F;

  float elderly_from_bio_years = 60.0F;

  float elderly_factor = 0.9F;  ///< ASSUMPTION: "a little less" in old age.

  /// Multiplier on a day the person worked a heavy kind (the kinds whose
  /// rest drain is the heavy 4/norm-day: plowing, harvest).
  float heavy_work_factor = 1.2F;  ///< ASSUMPTION.

  /// THE FOOD LIGHT'S MARGIN, game days: how much slack "enough with room to
  /// spare" means before the light goes green (office design §5, the stock
  /// traffic light). Its own number and not a shared one — reaching the
  /// harvest with a fortnight in hand is a different kind of comfort from
  /// reaching the sowing with the seed norm intact.
  ///
  /// ASSUMPTION until the balance pass. A light that is yellow always is
  /// noise and stops being seen inside a week; if that happens this is what
  /// is wrong, not the player.
  float food_light_margin_days = 8.0F;

  /// A LITTLE OF EVERYTHING THAT KEEPS (family_meal.cpp,
  /// EatALittleOfEachCategory; econ's rule, STUB): a category whose food
  /// keeps LONGER than this many game days gives its small share of the
  /// day's need before the strict order of the shelf life takes the rest.
  /// food.csv `little_of_each_keeps_over_days`. Six: the grain (600), the
  /// potato and the vegetables (120) are over it; milk, meat and fish (2),
  /// the egg and baked bread (6) are not, and the order takes them first
  /// anyway.
  float little_of_each_keeps_over_days = 6.0F;

  /// Which WorkKind values count as heavy, as a bitmask by kind index.
  /// Default: plowing (1) and harvest (4) — the decision-107 heavy pair.
  /// A mask, not a labor-config read: the food side must not depend on
  /// another module's parsed configuration.
  std::uint32_t heavy_kinds_mask = (1U << 1U) | (1U << 4U);
};

/// @brief How satiety moves and what it does (metrics design §8, health
/// design §2). Satiety drifts toward 100 x (eaten / needed) each day; the
/// family component is the member mean under the variety ceiling.
struct SatietyConfig {
  /// Daily drift rate toward the fed-ratio target, metric points per day.
  /// Slow by design: February's failure cannot be hidden by September.
  float drift_per_day = 4.0F;  ///< ASSUMPTION.

  /// Variety ceiling: the component cannot exceed
  /// 100 - penalty x max(0, epoch norm - categories eaten this season).
  /// Anchors: bread alone in Epoch I (norm 3) caps at 50.
  float missing_category_penalty = 25.0F;

  /// WHEN A CATEGORY IS ON THE TABLE (metrics design §8; econ §4.2, the
  /// human's word of 2026-10-01; food.csv `category_counted_share_of_need`,
  /// STUB): a category is counted for the ceiling when the kilocalories
  /// eaten of it this season reach this share of the family's need over the
  /// season (FamilyRow::season_category_kcal, season_need_kcal). Nought
  /// counts any gram, as every table did until 0.37.77. «Sweet» is outside
  /// the count whatever is eaten of it: it is the sugar's own complaint,
  /// not a place at the table. THE CONTRACT OF 0.37.76 — READ BY THE NEXT
  /// DELIVERY.
  float category_counted_share_of_need = 0.02F;

  std::array<float, 3> categories_norm_by_epoch = {3.0F, 5.0F, 7.0F};

  /// Health link (health design §2: chronic malnutrition, weekly cadence,
  /// recovery slower than the fall). ASSUMPTION knobs.
  float health_loss_satiety_threshold = 40.0F;

  float health_loss_per_week = 1.0F;

  float health_recovery_satiety_threshold = 70.0F;

  float health_recovery_per_week = 0.25F;
};

/// @brief The family-kolkhoz exchange (labor-payment §3, §5, §7): the
/// monthly automatic distribution against trudodni and the minimum ration.
struct DistributionConfig {
  /// Distribution cadence, days. A month is 4 game days; "once a month" is
  /// the design's own default auto-rule.
  std::uint32_t period_days = kDaysPerMonth;

  /// Family satiety at or below which the ration triggers.
  /// The health line (labor-payment §5; boss, boss-core-epoch1-4 seq 13):
  /// 25 until 0.34.42, when two thirds of the hungry stood between 25 and 40
  /// with no right to the ration.
  float ration_satiety_threshold = 40.0F;

  /// 0/1: the distribution never dips into next sowing's seed. The guard
  /// reserves, per grain resource, the seed the planned sowing needs
  /// (resources design §2: funds are off-limits to auto-issue).
  std::uint8_t reserve_seed_fund = 1;
};

/// @brief Household-plot time and the garden (household design §1). The
/// family's plot hours = the working members' mean day remainder (24 -
/// sleep - hours away, the stage-5 arithmetic) plus additive factors; the
/// garden's yield scales by min(1, hours / full_yield_hours) — the curve
/// the design's own yard examples draw (7.5h -> 100%, 2.7 -> 68%,
/// 1.2 -> 30%, 0.5 -> 12%).
struct PlotConfig {
  float full_yield_hours = 4.0F;

  /// Base for a family with no kolkhoz worker today (elders-only yards):
  /// the day remainder of someone who stayed home. ASSUMPTION.
  float no_worker_base_hours = 5.0F;

  /// Game hours of sleep, subtracted from the day before anything else.
  /// Read from tables/labor.csv, where that fact lives for every consumer —
  /// duplicating it into food.csv would be two homes for one number.
  float sleep_hours = 8.0F;

  /// Elders in the yard: +bonus when present, -bonus when absent (the
  /// design table carries both signs around the same base).
  float elders_hours = 1.3F;

  float elder_from_bio_years = 60.0F;

  /// Schoolchildren: each adds hours up to the cap; the summer value applies
  /// in the school-holiday months. The band is school age itself — from the
  /// first school year (education design §2) to adulthood.
  float schoolchild_from_bio_years = 7.0F;

  float schoolchild_to_bio_years = 16.0F;

  float schoolchild_hours = 0.4F;

  float schoolchild_summer_hours = 0.6F;

  float schoolchild_hours_cap = 1.6F;

  std::uint8_t summer_from_month = 5;  ///< 0-based: June.

  std::uint8_t summer_to_month = 7;  ///< Inclusive: August.

  /// A drinking adult in the family costs the plot directly (household
  /// design §1). Alcoholism is laid out but has no source in phase 1, so
  /// the factor is wired and silent.
  float drinker_hours = 1.0F;

  float drinker_alcoholism_threshold = 50.0F;  ///< ASSUMPTION.

  /// Sickness in the family (any member below the health threshold).
  float sickness_hours = 0.8F;

  float sickness_health_threshold = 40.0F;  ///< Health ladder: "ill" starts at 40.

  /// What one yard's garden yields a year at full time (household design
  /// §1: ~0.9 t potatoes, ~1.2 t vegetables from 10+10 sotkas).
  float potato_kg_per_yard_year = 900.0F;

  float vegetables_kg_per_yard_year = 1200.0F;

  /// The growing season whose daily yield ratios average into the harvest
  /// scale, and the month the garden lands in the pantry. 0-based months.
  std::uint8_t growing_from_month = 3;  ///< April.

  std::uint8_t growing_to_month = 8;  ///< Inclusive: September.

  std::uint8_t garden_harvest_month = 8;  ///< September.

  /// Hay the yard mows for ITSELF, kilograms a head of its hay-eating stock
  /// (adults and young; FoodConfig::hay_eating_kinds), carried in at the end
  /// of `hay_harvest_month` by a yard with anyone of adult age (food.csv
  /// `yard_hay_kg_per_head`, 1080, STUB — two goats winter on 2163 kg, the
  /// count the table's comment keeps). This is the one fodder a household
  /// does not get from the kolkhoz (household design §2, question 163):
  /// mowing is not kolkhoz work, so a yard with nobody on the farm's books
  /// still keeps its goats. NOT SCALED BY THE GARDEN'S ATTENTION since
  /// 0.37.54: the season's attention stood at 0.35-0.43 in Epoch I, the
  /// kolkhoz holding the hours, and the goats starved in spring year 2 on
  /// 27 seeds of 27 — the design's mowing is «несколько дней с косой,
  /// посильных и старику», not the garden's daily hours (boss, 30.09.2026).
  float yard_hay_kg_per_head = 1080.0F;

  std::uint8_t hay_harvest_month = 7;  ///< August, 0-based: the mowing is done.

  /// Net fishing, per epoch (household design §2, boss answer of
  /// 2026-08-30): a plain epoch constant into the pantry — no unit, no work
  /// order, no mechanic. It is help ON TOP of the designed coverage, never
  /// inside it. The numbers themselves are polish question P22m.
  std::array<float, 3> fish_kg_per_yard_year = {400.0F, 250.0F, 100.0F};

  /// THE FOREST'S GIFTS, GATHERED BY THE FAMILIES (the human's word of 30
  /// September and of 3 October 2026, «Сбор ягод и яйцо делайте»; boss,
  /// boss-all-epoch1-queue-after-counterweight-2026-10-03 [7], [9];
  /// 0.37.152): built as the nets are — no unit, no order, the families' own
  /// rest — and measured BY THE EATER, not by the yard: a share of an adult's
  /// need (the meal's own ramp, family_meal.h), so that a basket is the same
  /// part of every family's table. food.csv `forage_kg_per_eater_year`, 55 kg
  /// of fresh mushrooms and berries an eater over the months
  /// `forage_from_month`..`forage_to_month` (July to September), spread
  /// evenly over their days. STUB, econ's.
  ///
  /// forest_forage.csv — the yields by biome — is NOT read: the basket is the
  /// same wherever the yard stands. STUB, named.
  float forage_kg_per_eater_year = 0.0F;
  std::uint8_t forage_from_month = 6;  ///< July, 0-based.
  std::uint8_t forage_to_month = 8;    ///< September, 0-based.

  /// The share of the basket that is dried the day it is gathered (food.csv
  /// `forage_dried_share`, 0.5 — STUB, boss [7]): the dried is the winter's
  /// and the spring's place at the table, the fresh is the summer's. ITS
  /// MASS HAS NO KEY: the fresh's kilocalories over the dried's kilocalories a
  /// gram, both food.csv's — drying keeps what is eaten and loses the water.
  float forage_dried_share = 0.0F;

  /// The months the dried is eaten in, in equal shares (food.csv
  /// `dried_eaten_from_month`..`dried_eaten_to_month`, December to May —
  /// boss [7]: «едят ровной долей с декабря по май»): each day the store
  /// over the days left to the window's end. Outside the window the dried is
  /// not touched. The window may wrap the year's end.
  std::uint8_t dried_eaten_from_month = 11;  ///< December, 0-based.
  std::uint8_t dried_eaten_to_month = 4;     ///< May, 0-based.
};

/// @brief What the next sowing of one crop needs, per crop row of
/// tables/crops.csv. Read here rather than borrowed from core_production:
/// a module parses the cells it needs itself and never depends on another
/// module's parsed configuration (the labor precedent, labor_config.cpp).
/// The TYPE is core_common's, because the ladder of funds that reads it is.
using SeedNormDef = SeedNorm;

/// @brief The parsed stage-6 food configuration of core_residents.
struct FoodConfig {
  /// Dense by ResourceId, sized to the resource roster.
  std::vector<FoodResourceDef> resources;

  /// THE FODDER FUND TODAY, handed in by the assembly and not parsed
  /// (IProductionSystem::FodderFund): the working stock's ration of each work
  /// feed until its next reaping. The issue stays below rung 3, the larger
  /// of last year's feed and this (core_common/fund_ladder.h). Empty: no
  /// fund, rung 3 is last year's feed alone.
  ///
  /// A CALL INTO core_production, NOT DATA, and this config is read from the
  /// parallel slots 2 and 5 (PARALLEL_READONLY): call it only from the
  /// sequential decisions slot (phase 3) and between steps — the callers it
  /// has, IssueReserve's distribution, the ration alarm and the night theft
  /// (SealedFunds, 0.34.37). A parallel
  /// phase that called it would run production's read on a worker thread.
  std::function<ResourceAmounts(const WorldState&)> fodder_fund;

  /// What nobody eats today because next year needs it, by ResourceId
  /// (IProductionSystem::NextYearHold; 0.37.2, boss-core-epoch1-queue [60],
  /// (г)) — held above the issue's rungs, rot margin included. A CALL INTO
  /// core_production on the same terms as `fodder_fund` above: the decisions
  /// slot and between steps only. Empty: nothing held.
  std::function<ResourceAmounts(const WorldState&)> next_year_hold;

  /// Whole game days to the next harvest of a resource the fields will give
  /// (a crop standing in a field, or the one after its next sowing), -1 when
  /// none (IProductionSystem::DaysToHarvestOf; 0.37.28, the fields since
  /// 0.37.34; labor-payment §7): the default issue norm's "until the next
  /// harvest of this position".
  /// A CALL INTO core_production on the same terms as `fodder_fund` above.
  /// Empty: no position has a harvest — every one at the table's grams.
  std::function<std::int32_t(const WorldState&, ResourceId)> days_to_harvest_of;

  /// What next year's plan reserve will seal at the coming turn, by
  /// ResourceId (IProductionSystem::TurnPlanSeal; 0.37.36; labor-payment §7,
  /// «Что делится»): held in place of `next_year_hold` where larger, for a
  /// position whose next harvest comes after the turn. A CALL INTO
  /// core_production on the same terms as `fodder_fund` above. Empty:
  /// nothing sealed ahead.
  std::function<ResourceAmounts(const WorldState&)> turn_plan_seal;

  /// What this year's harvest will still bring into the stores, by
  /// ResourceId (IProductionSystem::HarvestToComeThisYear; 0.37.38; labor-
  /// payment §7): before the harvest the plan rung holds of the carry-over
  /// only what this will not pay (fund_ladder.h, PlanRungGrams). A CALL INTO
  /// core_production on the same terms as `fodder_fund` above. Empty: the
  /// rung holds the whole owed.
  std::function<ResourceAmounts(const WorldState&)> harvest_to_come;

  /// THE POSITION THE DISTRICT'S CART TAKES DAILY (district §9; register
  /// 231; boss seq 113): milk, by its resources.csv key. The plan does NOT
  /// seal it from the issue — its share of the day has left at the milking
  /// (core_production/milk_cart.h), and the issue takes from the rest. Invalid
  /// without a milk row. Meat and eggs join it when their cart is built.
  ResourceId carted_daily;

  /// Game days a resource keeps, dense by ResourceId — resources.csv
  /// `spoil_days`, zero for what does not go bad (task A4; transport design
  /// §10). Read here for the FAMILIES' LARDERS; core_production reads the
  /// same column for the units' stores, and the rule itself is shared
  /// (core_common/spoilage.h). Milk rots in a cellar exactly as it rots in a
  /// granary — and a larder that did not rot would turn the kolkhoz's issue
  /// into a way of hiding food from time itself.
  std::vector<float> spoil_days;

  /// STUB at 1.0: cellars, ice houses and frost are not in the slice.
  float keeping_factor = 1.0F;

  /// Dense by CropId, sized to the crop roster: what the seed fund holds
  /// back before the distribution runs.
  std::vector<SeedNormDef> seed_norms;

  ConsumptionConfig consumption;

  SatietyConfig satiety;

  DistributionConfig distribution;

  PlotConfig plot;

  // -- resources the household side names by hand (invalid = absent) -------
  ResourceId potato_resource;  ///< resources.csv "potato": the garden's half.

  ResourceId vegetables_resource;  ///< resources.csv "vegetables".

  ResourceId fish_resource;  ///< resources.csv "fish": the nets.

  /// resources.csv "forest_forage" and "dried_forest_gifts": the families'
  /// gathering, fresh and dried (PlotConfig::forage_kg_per_eater_year).
  ResourceId forage_resource;
  ResourceId dried_forage_resource;

  ResourceId hay_resource;  ///< resources.csv "hay": what the yard mows itself.

  /// 0/1 by livestock.csv row: the kind has a hay row in feed_links.csv, so
  /// the yard mows for its heads (PlotConfig::yard_hay_kg_per_head). Empty in
  /// a set without the two tables: no yard mows.
  std::vector<std::uint8_t> hay_eating_kinds;
};

/// @brief Parses tables/food.csv against the resource roster.
/// @param error Receives a human-readable message on failure; may be null.
/// @return The configuration, or defaults when the table is absent; refuses
/// (returns defaults and sets *error) on a malformed present table.
/// Missing scalar keys keep defaults; unreadable or out-of-range cells are
/// errors — the labor_config.cpp contract. Implemented at stage-6 task O1.
FoodConfig ParseFoodConfig(const ITableSet& tables, std::string* error);

}  // namespace core

#endif  // CORE_RESIDENTS_FOOD_CONFIG_H_
