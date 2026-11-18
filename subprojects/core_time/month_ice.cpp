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
#include "ice_winter.h"
#include "season_tables.h"
#include "weather_of_day.h"

namespace core {
namespace {

// The month door's two fulls, then #1859's six (boss, design.db world_param
// sort 380-385, all STUB): the live ice's growth, the currents, the inflow's
// disc and the band.
constexpr std::array<std::string_view, 8> kIceWorldParamKeys = {"ice_freeze_full_degree_days",
                                                                "ice_thaw_full_degree_days",
                                                                "ice_growth_alpha",
                                                                "ice_current_backwater",
                                                                "ice_current_river",
                                                                "ice_inflow_disc_m",
                                                                "ice_safe_cm",
                                                                "ice_deadly_cm"};

/// The ranges #1859's keys are read in: a share of the still ice, metres,
/// centimetres; alpha in cm/√(°C·day).
constexpr Range kIceAlpha{.low = 0.1F, .high = 10.0F};
constexpr Range kIceShare{.low = 0.0F, .high = 1.0F};
constexpr Range kIceDiscMetres{.low = 0.0F, .high = 500.0F};
constexpr Range kIceCentimetres{.low = 0.0F, .high = 100.0F};

/// The design base's min_ok..max_ok of both rows.
constexpr Range kFullDegreeDays{.low = 1.0F, .high = 500.0F};

// IceWinter and AdvanceIce moved to ice_winter.h (#1859): the live ice walks
// the same winter.

float ShareOf(float sum, float full) {
  return std::min(1.0F, sum / full);
}

}  // namespace

std::span<const std::string_view> IceWorldParamKeys() {
  return kIceWorldParamKeys;
}

bool ReadIceRules(const ITableSet& tables, IceRules& rules, std::string& error) {
  IceRules read = rules;
  if (const ITable* const world = tables.FindTable("world_params")) {
    const std::array<ScalarKnob, 7> knobs = {{
        {.key = kIceWorldParamKeys[1],
         .value = &read.thaw_full_degree_days,
         .range = kFullDegreeDays},
        {.key = kIceWorldParamKeys[2], .value = &read.growth_alpha, .range = kIceAlpha},
        {.key = kIceWorldParamKeys[3], .value = &read.current_backwater, .range = kIceShare},
        {.key = kIceWorldParamKeys[4], .value = &read.current_river, .range = kIceShare},
        {.key = kIceWorldParamKeys[5], .value = &read.inflow_disc_m, .range = kIceDiscMetres},
        {.key = kIceWorldParamKeys[6], .value = &read.safe_cm, .range = kIceCentimetres},
        {.key = kIceWorldParamKeys[7], .value = &read.deadly_cm, .range = kIceCentimetres},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
  }
  if (read.deadly_cm > read.safe_cm) {
    error = "world_params: ice_deadly_cm is above ice_safe_cm";
    return false;
  }
  rules = read;
  return true;
}

IceBand IceBandOf(float thickness_cm, const IceRules& rules) {
  if (thickness_cm <= 0.0F) {
    return IceBand::kNoIce;
  }
  if (thickness_cm < rules.deadly_cm) {
    return IceBand::kDeadly;
  }
  return thickness_cm < rules.safe_cm ? IceBand::kThin : IceBand::kSafe;
}

float IceShareOf(IceWater water, const IceRules& rules) {
  switch (water) {
    case IceWater::kStill:
      return 1.0F;
    case IceWater::kBackwater:
      return rules.current_backwater;
    case IceWater::kInflow:
    case IceWater::kRiver:
      return rules.current_river;
    case IceWater::kNone:
    case IceWater::kRapids:
    case IceWater::kIceWaterCount:
      return 0.0F;
  }
  return 0.0F;
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
