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

#ifndef CORE_RESIDENTS_FOOD_CONFIG_H_
#define CORE_RESIDENTS_FOOD_CONFIG_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core_common/calendar.h"
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

  float eat_from_bio_years = 1.5F;

  float adult_from_bio_years = 16.0F;

  float elderly_from_bio_years = 60.0F;

  float elderly_factor = 0.9F;  ///< ASSUMPTION: "a little less" in old age.

  /// Multiplier on a day the person worked a heavy kind (the kinds whose
  /// rest drain is the heavy 4/norm-day: plowing, harvest).
  float heavy_work_factor = 1.2F;  ///< ASSUMPTION.

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

  /// The ration auto-rule is armed for everyone (labor-payment §5 allows
  /// arming it in advance; phase 1 has no player to decide per family).
  /// 0/1. ASSUMPTION as a default, the mechanic itself is canon.
  std::uint8_t ration_auto = 1;

  /// Family satiety at or below which the ration triggers.
  float ration_satiety_threshold = 25.0F;  ///< ASSUMPTION.

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

  /// Elders in the yard: +bonus when present, -bonus when absent (the
  /// design table carries both signs around the same base).
  float elders_hours = 1.3F;

  float elder_from_bio_years = 60.0F;

  /// Schoolchildren (bio 7..16): each adds hours up to the cap; the summer
  /// value applies in the school-holiday months.
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

  /// Net fishing, per epoch (household design §2, boss answer of
  /// 2026-08-30): a plain epoch constant into the pantry — no unit, no work
  /// order, no mechanic. It is help ON TOP of the designed coverage, never
  /// inside it. The numbers themselves are polish question P22m.
  std::array<float, 3> fish_kg_per_yard_year = {400.0F, 250.0F, 100.0F};
};

/// @brief What the next sowing of one crop needs, per crop row of
/// tables/crops.csv. Read here rather than borrowed from core_production:
/// a module parses the cells it needs itself and never depends on another
/// module's parsed configuration (the labor precedent, labor_config.cpp).
struct SeedNormDef {
  ResourceId resource;  ///< What the seed of this crop is.

  float sowing_norm_kg_per_ha = 0.0F;  ///< 0 = the crop needs no seed stock.
};

/// @brief The parsed stage-6 food configuration of core_residents.
struct FoodConfig {
  /// Dense by ResourceId, sized to the resource roster.
  std::vector<FoodResourceDef> resources;

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
