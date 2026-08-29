// Internal to core_production: configuration parsed from the balance tables.
//
// CropDef mirrors tables/crops.csv (indexed by CropId = row), LivestockDef
// mirrors tables/livestock.csv, UnitTypeDef mirrors tables/unit_types.csv,
// FarmingConfig mirrors tables/farming.csv. A world without these tables
// (unit tests, early runs) gets empty rosters and the subsystem idles;
// present-but-malformed tables refuse the factory.

#ifndef CORE_PRODUCTION_PRODUCTION_CONFIG_H_
#define CORE_PRODUCTION_PRODUCTION_CONFIG_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"

namespace core {

struct CropDef {
  ResourceId resource;  ///< What the harvest and the seed are.

  bool is_winter = false;  ///< Sown in autumn, harvested next July.

  bool is_perennial = false;  ///< Sown once, harvested for years.

  std::uint8_t sow_from_month = 0;  ///< 0-based month indices, inclusive.

  std::uint8_t sow_to_month = 0;

  std::uint8_t harvest_from_month = 0;

  std::uint8_t harvest_to_month = 0;

  float sow_min_temp_c = 0.0F;

  float harvest_min_temp_c = 0.0F;  ///< "Убрать до": kept for stage-5 timing.

  float yield_kg_per_ha = 0.0F;  ///< At neutral fertility.

  float sowing_norm_kg_per_ha = 0.0F;  ///< 0 = sowing consumes no material.

  /// Labor norms of the two crop-specific working phases, in GAME man-days
  /// per hectare (the table keeps REAL man-days; parsing divides by
  /// kRealDaysPerGameDay once). They size FieldRow::work_days_remaining when
  /// production opens the phase — the seam labor then drains
  /// (manual/65-labor-model.md §2). Grain anchor, real man-days per hectare:
  /// plow 10 + harrow 3 + sow 3 + harvest 8 = 24 (49-simulations §2).
  float sow_days_per_ha = 3.0F / kRealDaysPerGameDay;

  float harvest_days_per_ha = 8.0F / kRealDaysPerGameDay;

  float fertility_delta = 0.0F;  ///< Applied at harvest.

  float drought_sensitivity = 0.0F;  ///< 0..1 scale on the stress rate.

  float wet_sensitivity = 0.0F;
};

struct LivestockDef {
  float manure_kg_per_year = 0.0F;

  float hay_kg_per_day_winter = 0.0F;

  float grain_kg_per_day = 0.0F;

  // -- stage 6: breeding, aging, produce (manual/66-food-model.md §6) ------
  // Ages run on the BIOLOGICAL clock (boss rules 2026-08-29 §2.2: biology
  // / 4), so rung durations are biological; produce and birth rates are
  // per GAME year — that is what the settlement's yearly balance eats.
  // Poultry is sexless and two-runged (newborn_bio_days = 0 skips the rung).

  /// 0/1: the kind has sexes; breeding then needs an adult male present.
  std::uint8_t sexed = 1;

  float newborn_bio_days = 0.0F;

  float juvenile_bio_days = 0.0F;

  /// Adult lifespan from adulthood, biological years: the age-death draw
  /// ramps up as the cohort mean passes it (threshold with randomness).
  float adult_life_bio_years = 0.0F;

  float births_per_female_year = 0.0F;  ///< Litters per adult female per game year.

  float litter_heads = 1.0F;  ///< Newborns per litter.

  /// Adult males the herd keeps; males maturing beyond it are slaughtered
  /// (meat) — one bull serves the barn, extra mouths do not overwinter.
  std::uint8_t males_kept_per_herd = 1;

  /// Produce per adult head per game year, into the housing unit's stock
  /// (kolkhoz herds) or the family pantry (household herds). Milk counts
  /// females only; eggs and wool count every adult.
  float milk_kg_per_year = 0.0F;

  float egg_kg_per_year = 0.0F;

  float wool_kg_per_year = 0.0F;

  /// Slaughter yield per head, by rung share of adult weight for the young.
  float meat_kg_per_head = 0.0F;
};

struct UnitTypeDef {
  float storage_capacity_kg = 0.0F;  ///< 0 = stores nothing.

  float livestock_capacity_head = 0.0F;
};

struct FarmingConfig {
  float fertility_neutral = 50.0F;

  /// Plowing and harrowing norms in GAME man-days per hectare: one norm for
  /// any land and any crop (farming design §5). Same conversion as the crop
  /// norms above.
  float plow_days_per_ha = 10.0F / kRealDaysPerGameDay;

  float harrow_days_per_ha = 3.0F / kRealDaysPerGameDay;

  float manure_norm_kg_per_ha = 20000.0F;

  float manure_fertility_bonus = 6.0F;

  float fallow_recovery = 6.0F;

  float repeat_penalty_per_year = 3.0F;

  float drought_temp_c = 25.0F;

  float stress_per_day = 0.02F;

  float stress_cap = 0.3F;

  // -- stage 6: herd-wide knobs (tables/farming.csv scalar rows) -----------
  /// Age death: expected deaths/day = adults x max(0, mean age - lifespan)
  /// / (spread x days per year); the spread is the "randomness" width
  /// around the threshold. ASSUMPTION.
  float herd_age_death_spread_years = 2.0F;

  /// Underfeeding: produce multiplier while unfed, and when deaths start.
  /// ASSUMPTION until the feeding runs.
  float unfed_produce_factor = 0.5F;

  float unfed_death_after_days = 8.0F;

  float unfed_death_percent_per_day = 5.0F;
};

struct ProductionConfig {
  std::vector<CropDef> crops;  ///< Indexed by CropId row.

  std::vector<LivestockDef> livestock;  ///< Indexed by LivestockKindId row.

  std::vector<UnitTypeDef> unit_types;  ///< Indexed by UnitTypeId row.

  FarmingConfig farming;

  ResourceId manure_resource;  ///< resources.csv "manure" row.

  ResourceId hay_resource;  ///< resources.csv "hay" row.

  // -- stage 6: where herd produce lands (invalid = kind yields none) ------
  ResourceId milk_resource;  ///< resources.csv "milk" row.

  ResourceId egg_resource;  ///< resources.csv "egg" row.

  ResourceId wool_resource;  ///< resources.csv "wool" row.

  ResourceId meat_resource;  ///< resources.csv "meat" row.

  UnitTypeId compost_heap_type;  ///< unit_types.csv "compost_heap".

  /// unit_types.csv "stable": the closed housing horse breeding requires
  /// (boss rules 2026-08-29 §2.2); other kinds breed under any roof.
  UnitTypeId stable_type;
};

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_CONFIG_H_
