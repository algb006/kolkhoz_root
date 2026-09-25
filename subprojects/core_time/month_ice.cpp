// The ice of a month (core_time/month_ice.h): the water's setting and
// opening, walked by the time phase's own weather and snow.

#include "core_time/month_ice.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/daylight.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_time/month_climate.h"
#include "season_tables.h"
#include "weather_of_day.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 2> kIceWorldParamKeys = {"ice_freeze_full_degree_days",
                                                                "ice_thaw_full_degree_days"};

/// The design base's min_ok..max_ok of both rows.
constexpr Range kFullDegreeDays{.low = 1.0F, .high = 500.0F};

/// The two sums of one winter and what they make of the ice.
struct IceWinter {
  float frost_degree_days = 0.0F;
  float thaw_degree_days = 0.0F;

  /// A snow cover has lain this winter: from then on a warm bare day opens
  /// the ice (the spring), before it one melts back what has just set.
  bool snow_has_lain = false;
};

/// One day of the rule (month_ice.h): frost below nought; above it, before
/// the winter's first snow the young ice melts back, and after the snow has
/// lain and gone the thaw opens it; a fresh winter the day the thaw is whole.
///
/// THE AUTUMN'S WARM DAY IS NOT THE SPRING (found by this door's own test,
/// 2026-09-25): counted as thaw, the snowless warm days of November had the
/// rivers «opened» by a seventh in January.
void AdvanceIce(IceWinter& winter, const WeatherState& today, float thaw_full) {
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

float ShareOf(float sum, float full) {
  return std::min(1.0F, sum / full);
}

}  // namespace

std::span<const std::string_view> IceWorldParamKeys() {
  return kIceWorldParamKeys;
}

bool MonthIceOfTables(const ITableSet& tables, Month month, MonthIce& ice, std::string& error) {
  if (tables.FindTable("weather") == nullptr) {
    error = "month ice: the table set has no weather table";
    return false;
  }
  SeasonTable seasons;
  if (!ReadSeasonTable(tables, seasons, error)) {
    return false;
  }
  float freeze_full = 30.0F;
  float thaw_full = 40.0F;
  if (const ITable* const world = tables.FindTable("world_params")) {
    const std::array<ScalarKnob, 2> knobs = {{
        {.key = kIceWorldParamKeys[0], .value = &freeze_full, .range = kFullDegreeDays},
        {.key = kIceWorldParamKeys[1], .value = &thaw_full, .range = kFullDegreeDays},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
  }
  // THE MIDDLE OF THE MONTH: its third day of four, one after the day the
  // climate door reads (SecondDayOfMonth).
  const std::uint32_t day_of_year = (SecondDayOfMonth(month) + 1U) % kDaysPerYear;
  std::vector<float> freezes;
  std::vector<float> thaws;
  freezes.reserve(kMonthClimateYears);
  thaws.reserve(kMonthClimateYears);
  WeatherState yesterday;
  IceWinter winter;
  const auto last_day = static_cast<SimDay>((kMonthClimateYears + 1U) * kDaysPerYear);
  for (SimDay day = 0; day < last_day; ++day) {
    WeatherState today = WeatherOfDay(seasons, kMonthClimateSeed, day);
    today.snow_cover_days = SnowCoverAfter(seasons, yesterday, today, day);
    AdvanceIce(winter, today, thaw_full);
    if (day >= kDaysPerYear && day % kDaysPerYear == day_of_year) {
      freezes.push_back(ShareOf(winter.frost_degree_days, freeze_full));
      thaws.push_back(ShareOf(winter.thaw_degree_days, thaw_full));
    }
    yesterday = today;
  }
  // THE MEDIAN of each, the typical middle of the month — each share on its
  // own, so the pair is two typical answers and not one typical year's.
  const auto median = [](std::vector<float>& values) {
    std::ranges::sort(values);
    return values.empty() ? 0.0F : values[(values.size() - 1U) / 2U];
  };
  ice.freeze = median(freezes);
  ice.thaw = median(thaws);
  return true;
}

}  // namespace core
