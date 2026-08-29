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

  float fertility_delta = 0.0F;  ///< Applied at harvest.

  float drought_sensitivity = 0.0F;  ///< 0..1 scale on the stress rate.

  float wet_sensitivity = 0.0F;
};

struct LivestockDef {
  float manure_kg_per_year = 0.0F;

  float hay_kg_per_day_winter = 0.0F;

  float grain_kg_per_day = 0.0F;
};

struct UnitTypeDef {
  float storage_capacity_kg = 0.0F;  ///< 0 = stores nothing.

  float livestock_capacity_head = 0.0F;
};

struct FarmingConfig {
  float fertility_neutral = 50.0F;

  float manure_norm_kg_per_ha = 20000.0F;

  float manure_fertility_bonus = 6.0F;

  float fallow_recovery = 6.0F;

  float repeat_penalty_per_year = 3.0F;

  float drought_temp_c = 25.0F;

  float stress_per_day = 0.02F;

  float stress_cap = 0.3F;
};

struct ProductionConfig {
  std::vector<CropDef> crops;  ///< Indexed by CropId row.

  std::vector<LivestockDef> livestock;  ///< Indexed by LivestockKindId row.

  std::vector<UnitTypeDef> unit_types;  ///< Indexed by UnitTypeId row.

  FarmingConfig farming;

  ResourceId manure_resource;  ///< resources.csv "manure" row.

  ResourceId hay_resource;  ///< resources.csv "hay" row.

  UnitTypeId compost_heap_type;  ///< unit_types.csv "compost_heap".
};

}  // namespace core

#endif  // CORE_PRODUCTION_PRODUCTION_CONFIG_H_
