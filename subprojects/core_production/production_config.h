// Internal to core_production: configuration parsed from the balance tables.
//
// CropDef mirrors tables/crops.csv (indexed by CropId = row), LivestockDef
// mirrors tables/livestock.csv, UnitTypeDef mirrors tables/unit_types.csv,
// FarmingConfig mirrors tables/farming.csv, FeedLinkDef mirrors
// tables/feed_links.csv (stage 6). A world without these tables
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

  /// Stage-4 interim feeding, replaced by the feed-unit model below at
  /// stage-6 task O3; the fields and their livestock.csv columns leave
  /// together with the winter-hay block in production_system.cpp.
  float hay_kg_per_day_winter = 0.0F;

  float grain_kg_per_day = 0.0F;

  /// Daily need of an adult head, kilogram feed units per GAME day
  /// (oat = 1.0). The table column feed_need_units_per_real_day keeps the
  /// REAL reference number (design db, question 133: horse 10, cow 9,
  /// pig 3); parsing multiplies by 365 / kDaysPerYear once so the yearly
  /// feed mass holds. Juveniles eat juvenile_feed_factor of it, newborns
  /// at the dam eat nothing (canon, feed rules §11).
  float feed_need_units_per_day = 0.0F;

  /// Share of the daily need summer pasture and free-ranging cover in the
  /// pasture months (the norm is about need, not mandatory store spending).
  /// ASSUMPTION defaults mirror the stage-4 seasonal shape: grazers eat
  /// stores only in winter, pigs and poultry eat stores all year.
  float pasture_coverage_summer = 0.0F;

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

/// @brief One feeding-order row (tables/feed_links.csv, question 133):
/// which kind eats which resource. Row order IS the priority — regular
/// ration before reserve, fodder grain before bread grain; the exported
/// file keeps that order, so no sorting happens in code.
struct FeedLinkDef {
  LivestockKindId kind;

  ResourceId resource;

  std::uint8_t reserve = 0;  ///< 1 = reserve ration with a lowered effect.
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

  // -- stage 6: feeding by feed units (question 133) -----------------------
  /// Juveniles eat this share of the adult norm (canon: one half).
  float juvenile_feed_factor = 0.5F;

  /// Feed units from reserve rows (FeedLinkDef::reserve) count into the
  /// covered need with this multiplier — the design says "reserve with a
  /// lowered effect" but names no number. ASSUMPTION until the balance pass.
  float reserve_feed_factor = 0.8F;
};

struct ProductionConfig {
  std::vector<CropDef> crops;  ///< Indexed by CropId row.

  std::vector<LivestockDef> livestock;  ///< Indexed by LivestockKindId row.

  std::vector<UnitTypeDef> unit_types;  ///< Indexed by UnitTypeId row.

  FarmingConfig farming;

  /// Dense by ResourceId: feed value in kilogram feed units per kilogram
  /// (oat = 1.0); 0 = the resource is not feed. Source: the resources.csv
  /// feed_value column (design db resource.feed_value, question 133).
  std::vector<float> feed_values;

  /// Feeding-order rows in file (= priority) order; see FeedLinkDef.
  std::vector<FeedLinkDef> feed_links;

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
