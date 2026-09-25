// The beds' condition and speed (road_rules.h).

#include "core_common/road_rules.h"

#include <algorithm>
#include <cstddef>

namespace core {

RoadBeds RoadBedsAfter(const RoadRules& rules,
                       const RoadBeds& yesterday,
                       bool rain_today,
                       float mean_celsius,
                       bool mud_season,
                       bool snow_lies) {
  RoadBeds today;
  const bool cold = mean_celsius < rules.dry_cold_below_celsius;
  for (std::size_t bed = 0; bed < kRoadBedCountValue; ++bed) {
    // The rainy day itself is wet on every bed; the count is of the days
    // AFTER it, so a bed whose dry_days is 0 is dry again tomorrow.
    bool wet = rain_today;
    if (rain_today) {
      today.wet_days_left[bed] = rules.dry_days[bed] + (cold ? rules.dry_cold_extra_days : 0.0F);
    } else if (yesterday.wet_days_left[bed] > 0.0F) {
      wet = true;
      today.wet_days_left[bed] = std::max(0.0F, yesterday.wet_days_left[bed] - 1.0F);
    }
    RoadCondition condition = wet ? RoadCondition::kWet : RoadCondition::kDry;
    if (snow_lies) {
      condition = RoadCondition::kSnow;
    } else if (mud_season && static_cast<RoadBed>(bed) != RoadBed::kAsphalt) {
      condition = RoadCondition::kMud;
    } else if (mud_season) {
      condition = RoadCondition::kWet;
    } else if (mean_celsius <= 0.0F) {
      condition = RoadCondition::kFrozen;
    }
    today.condition[bed] = condition;
  }
  return today;
}

float RoadBedFactor(const RoadRules& rules,
                    float mud_speed_factor,
                    RoadCondition condition,
                    RoadBed bed,
                    bool on_runners) {
  const auto index = static_cast<std::size_t>(bed);
  switch (condition) {
    case RoadCondition::kWet:
      return rules.wet_factor[index];
    case RoadCondition::kMud:
      return bed == RoadBed::kDirt ? mud_speed_factor : rules.mud_factor_gravel;
    case RoadCondition::kFrozen:
      return rules.frozen_factor;
    case RoadCondition::kSnow:
      return on_runners ? 1.0F : rules.snow_wheel_factor;
    default:
      return 1.0F;
  }
}

}  // namespace core
