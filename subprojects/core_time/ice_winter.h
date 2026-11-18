/// @file
/// @brief One winter's ice, day by day: the frost and the thaw sums the month's
///        ice door (month_ice.h) and the live ice (ITimeSystem::StillIceCmOn;
///        #1859) both walk. Internal to core_time.
/// @threading PARALLEL_READONLY
/// Plain data and pure functions.

#ifndef CORE_TIME_ICE_WINTER_H_
#define CORE_TIME_ICE_WINTER_H_

#include <algorithm>
#include <cmath>

#include "core_common/world_state.h"

namespace core {

/// @brief The two sums of one winter and what they make of the ice.
struct IceWinter {
  /// °C × game days below nought this winter, less what an autumn warm day
  /// melted back before the first snow.
  float frost_degree_days = 0.0F;

  /// °C × game days above nought on bare ground after a cover has lain.
  float thaw_degree_days = 0.0F;

  /// A snow cover has lain this winter: from then on a warm bare day opens
  /// the ice (the spring), before it one melts back what has just set.
  bool snow_has_lain = false;
};

/// @brief One day of the rule (month_ice.h): frost below nought; above it,
///        before the winter's first snow the young ice melts back, and after
///        the snow has lain and gone the thaw opens it; a fresh winter the day
///        the thaw is whole.
///
/// THE AUTUMN'S WARM DAY IS NOT THE SPRING (found by the month door's own
/// test, 2026-09-25): counted as thaw, the snowless warm days of November had
/// the rivers «opened» by a seventh in January.
/// @param today Its air_temperature_celsius and snow_cover_days are read.
/// @param thaw_full The thaw sum at which the ice is gone (world_params
///        `ice_thaw_full_degree_days`).
inline void AdvanceIce(IceWinter& winter, const WeatherState& today, float thaw_full) {
  const float mean = today.air_temperature_celsius;
  winter.snow_has_lain = winter.snow_has_lain || today.snow_cover_days > 0;
  if (mean < 0.0F) {
    winter.frost_degree_days -= mean;
  } else if (mean > 0.0F && !winter.snow_has_lain) {
    winter.frost_degree_days = std::max(0.0F, winter.frost_degree_days - mean);
  } else if (mean > 0.0F && today.snow_cover_days == 0 && winter.frost_degree_days > 0.0F) {
    winter.thaw_degree_days += mean;
  }
  if (winter.thaw_degree_days >= thaw_full) {
    winter = IceWinter{};
  }
}

/// @brief The still water's ice of a winter, centimetres (#1859).
///
/// STEFAN'S LAW on the frost: h = alpha × √(frost degree-days in REAL days), a
/// game day counting as `real_days_per_game_day` real ones (the man-days ÷ 7
/// rule; boss, #1859). THE SPRING THINS IT by the thaw's share: the ice the
/// frost made, times what is left of it before the thaw is whole - so it goes
/// to nought the day the month door says the water has opened. STUB: a snow
/// cover's insulation is not modelled (the ice grows under snow as in the
/// open), nor is a thaw's ice that refreezes.
/// @return ≥ 0.
inline float StillIceCm(const IceWinter& winter,
                        float alpha,
                        float thaw_full,
                        float real_days_per_game_day) {
  if (winter.frost_degree_days <= 0.0F) {
    return 0.0F;
  }
  const float grown = alpha * std::sqrt(winter.frost_degree_days * real_days_per_game_day);
  const float left =
      thaw_full > 0.0F ? 1.0F - std::min(1.0F, winter.thaw_degree_days / thaw_full) : 1.0F;
  return std::max(0.0F, grown * left);
}

}  // namespace core

#endif  // CORE_TIME_ICE_WINTER_H_
