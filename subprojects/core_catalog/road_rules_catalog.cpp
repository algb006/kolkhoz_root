// The roads' numbers out of world_params.csv (road_rules_catalog.h).

#include "core_catalog/road_rules_catalog.h"

#include <array>
#include <span>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_common/road_rules.h"
#include "core_tables/tables.h"

namespace core {
namespace {

// The ranges are the design base's min_ok..max_ok of each row (boss [38]),
// so the core refuses exactly what the base's own check would.
constexpr Range kDays{.low = 0.0F, .high = 12.0F};
constexpr Range kFactor{.low = 0.05F, .high = 1.0F};
constexpr Range kWeight{.low = 0.0F, .high = 10.0F};
constexpr Range kPerDay{.low = 0.0F, .high = 1000.0F};
constexpr Range kPctPerDay{.low = 0.0F, .high = 5.0F};

constexpr std::array<std::string_view, 29> kRoadWorldParamKeys = {"road_dry_days_dirt",
                                                                  "road_dry_days_gravel",
                                                                  "road_dry_days_asphalt",
                                                                  "road_dry_cold_below_c",
                                                                  "road_dry_cold_extra_days",
                                                                  "road_wet_factor_dirt",
                                                                  "road_wet_factor_gravel",
                                                                  "road_wet_factor_asphalt",
                                                                  "road_mud_factor_gravel",
                                                                  "road_frozen_factor",
                                                                  "road_snow_wheel_factor",
                                                                  "road_traffic_weight_walk",
                                                                  "road_traffic_weight_horse",
                                                                  "road_traffic_weight_cart",
                                                                  "road_traffic_weight_cart_loaded",
                                                                  "road_traffic_memory_days",
                                                                  "road_traffic_regular_from",
                                                                  "road_traffic_rare_from",
                                                                  "road_traffic_almost_none_from",
                                                                  "road_traffic_start_regular",
                                                                  "road_traffic_start_rare",
                                                                  "road_traffic_start_almost_none",
                                                                  "road_wear_pct_per_trip",
                                                                  "road_wear_wet_factor",
                                                                  "road_wear_speed_from_51",
                                                                  "road_wear_speed_from_76",
                                                                  "road_overgrow_pct_per_day",
                                                                  "road_overgrow_floor_pct",
                                                                  "road_strip_decay_pct_per_day"};

}  // namespace

std::span<const std::string_view> RoadWorldParamKeys() {
  return kRoadWorldParamKeys;
}

bool ParseRoadRules(const ITableSet& tables, RoadRules& rules, std::string& error) {
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  const auto& keys = kRoadWorldParamKeys;
  const std::array<ScalarKnob, kRoadWorldParamKeys.size()> knobs = {{
      {.key = keys[0], .value = &rules.dry_days[0], .range = kDays},
      {.key = keys[1], .value = &rules.dry_days[1], .range = kDays},
      {.key = keys[2], .value = &rules.dry_days[2], .range = kDays},
      {.key = keys[3],
       .value = &rules.dry_cold_below_celsius,
       .range = Range{.low = -20.0F, .high = 20.0F}},
      {.key = keys[4], .value = &rules.dry_cold_extra_days, .range = kDays},
      {.key = keys[5], .value = &rules.wet_factor[0], .range = kFactor},
      {.key = keys[6], .value = &rules.wet_factor[1], .range = kFactor},
      {.key = keys[7], .value = &rules.wet_factor[2], .range = kFactor},
      {.key = keys[8], .value = &rules.mud_factor_gravel, .range = kFactor},
      {.key = keys[9], .value = &rules.frozen_factor, .range = Range{.low = 1.0F, .high = 2.0F}},
      {.key = keys[10], .value = &rules.snow_wheel_factor, .range = kFactor},
      {.key = keys[11], .value = &rules.traffic_weight_walk, .range = kWeight},
      {.key = keys[12], .value = &rules.traffic_weight_horse, .range = kWeight},
      {.key = keys[13], .value = &rules.traffic_weight_cart, .range = kWeight},
      {.key = keys[14], .value = &rules.traffic_weight_cart_loaded, .range = kWeight},
      {.key = keys[15],
       .value = &rules.traffic_memory_days,
       .range = Range{.low = 1.0F, .high = 48.0F}},
      {.key = keys[16], .value = &rules.traffic_regular_from, .range = kPerDay},
      {.key = keys[17], .value = &rules.traffic_rare_from, .range = kPerDay},
      {.key = keys[18], .value = &rules.traffic_almost_none_from, .range = kPerDay},
      {.key = keys[19], .value = &rules.traffic_start_regular, .range = kPerDay},
      {.key = keys[20], .value = &rules.traffic_start_rare, .range = kPerDay},
      {.key = keys[21], .value = &rules.traffic_start_almost_none, .range = kPerDay},
      {.key = keys[22], .value = &rules.wear_pct_per_trip, .range = kPctPerDay},
      {.key = keys[23], .value = &rules.wear_wet_factor, .range = Range{.low = 1.0F, .high = 5.0F}},
      {.key = keys[24], .value = &rules.wear_speed_from_51, .range = kFactor},
      {.key = keys[25], .value = &rules.wear_speed_from_76, .range = kFactor},
      {.key = keys[26], .value = &rules.overgrow_pct_per_day, .range = kPctPerDay},
      {.key = keys[27],
       .value = &rules.overgrow_floor_pct,
       .range = Range{.low = 0.0F, .high = 100.0F}},
      {.key = keys[28], .value = &rules.strip_decay_pct_per_day, .range = kPctPerDay},
  }};
  if (!ReadKnobs(*world, "world_params", knobs, error)) {
    return false;
  }
  // The four words are read off one counter by falling thresholds; out of
  // order, a stretch would read as driven regularly and rarely at once.
  if (!(rules.traffic_regular_from > rules.traffic_rare_from &&
        rules.traffic_rare_from > rules.traffic_almost_none_from)) {
    error =
        "world_params: road_traffic_regular_from > road_traffic_rare_from > "
        "road_traffic_almost_none_from must hold — one counter, four words";
    return false;
  }
  return true;
}

}  // namespace core
