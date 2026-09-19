// The chairman's trip to the district in numbers
// (core_catalog/district_trip_catalog.h).

#include "core_catalog/district_trip_catalog.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The world_params.csv keys, in the order of the knob list in the parse.
constexpr std::array<std::string_view, 8> kDistrictTripWorldParamKeys = {
    "trip_depart_hour",
    "trip_return_hour",
    "summon_letter_delay_days",
    "summon_after_letter_days",
    "plan_trade_percent",
    "plan_trade_percent_rep_cost",
    "plan_trade_swap_rep_cost",
    "plan_trade_min_reputation"};

/// An hour and a day count are whole: 8.5 is a typo, not half past eight.
bool Whole(float value) {
  return std::floor(value) == value;
}

}  // namespace

std::span<const std::string_view> DistrictTripWorldParamKeys() {
  return kDistrictTripWorldParamKeys;
}

bool ParseDistrictTripCatalog(const ITableSet& tables,
                              DistrictTripCatalog& catalog,
                              std::string& error) {
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  float depart = catalog.depart_hour;
  float back = catalog.return_hour;
  auto letter = static_cast<float>(catalog.summon_letter_delay_days);
  auto after = static_cast<float>(catalog.summon_after_letter_days);
  const auto last_hour = static_cast<float>(kTicksPerDay - 1);
  const auto month = static_cast<float>(kDaysPerMonth);
  const Range reputation{.low = 0.0F, .high = 100.0F};
  const std::array<ScalarKnob, kDistrictTripWorldParamKeys.size()> knobs = {{
      {.key = kDistrictTripWorldParamKeys[0],
       .value = &depart,
       .range = {.low = 0.0F, .high = last_hour}},
      {.key = kDistrictTripWorldParamKeys[1],
       .value = &back,
       .range = {.low = 0.0F, .high = last_hour}},
      {.key = kDistrictTripWorldParamKeys[2],
       .value = &letter,
       .range = {.low = 0.0F, .high = month}},
      {.key = kDistrictTripWorldParamKeys[3],
       .value = &after,
       .range = {.low = 1.0F, .high = month}},
      {.key = kDistrictTripWorldParamKeys[4],
       .value = &catalog.plan_trade_percent,
       .range = {.low = 0.0F, .high = 100.0F}},
      {.key = kDistrictTripWorldParamKeys[5],
       .value = &catalog.plan_trade_percent_rep_cost,
       .range = reputation},
      {.key = kDistrictTripWorldParamKeys[6],
       .value = &catalog.plan_trade_swap_rep_cost,
       .range = reputation},
      {.key = kDistrictTripWorldParamKeys[7],
       .value = &catalog.plan_trade_min_reputation,
       .range = reputation},
  }};
  if (!ReadKnobs(*world, "world_params", knobs, error)) {
    return false;
  }
  const std::array<float, 4> whole = {depart, back, letter, after};
  for (std::size_t index = 0; index < whole.size(); ++index) {
    if (!Whole(whole[index])) {
      error = "world_params: " + std::string(kDistrictTripWorldParamKeys[index]) +
              " is not a whole number";
      return false;
    }
  }
  // Back the same day after he left: an evening before the morning would be
  // a trip that returns before it starts.
  if (!(back > depart)) {
    error = "world_params: trip_return_hour is not after trip_depart_hour";
    return false;
  }
  catalog.depart_hour = static_cast<std::uint8_t>(depart);
  catalog.return_hour = static_cast<std::uint8_t>(back);
  catalog.summon_letter_delay_days = static_cast<std::uint32_t>(letter);
  catalog.summon_after_letter_days = static_cast<std::uint32_t>(after);
  return true;
}

}  // namespace core
