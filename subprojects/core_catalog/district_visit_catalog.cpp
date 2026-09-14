// The district's regular visits in numbers (core_catalog/district_visit_catalog.h).

#include "core_catalog/district_visit_catalog.h"

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
constexpr std::array<std::string_view, 3> kDistrictVisitWorldParamKeys = {
    "district_visit_karasev_month",
    "district_visit_polushkina_month",
    "district_visit_notice_days"};

/// A month and a day count are whole: 6.5 is a typo, not half of June.
bool WholeValue(float value, std::int32_t& out) {
  if (std::floor(value) != value) {
    return false;
  }
  out = static_cast<std::int32_t>(value);
  return true;
}

}  // namespace

std::span<const std::string_view> DistrictVisitWorldParamKeys() {
  return kDistrictVisitWorldParamKeys;
}

bool ParseDistrictVisitCatalog(const ITableSet& tables,
                               DistrictVisitCatalog& catalog,
                               std::string& error) {
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  std::array<float, kDistrictVisitWorldParamKeys.size()> values = {
      static_cast<float>(catalog.karasev_month),
      static_cast<float>(catalog.polushkina_month),
      static_cast<float>(catalog.notice_days)};
  const auto months = static_cast<float>(kMonthsPerYear);
  const std::array<ScalarKnob, kDistrictVisitWorldParamKeys.size()> knobs = {{
      {.key = kDistrictVisitWorldParamKeys[0],
       .value = &values[0],
       .range = {.low = 1.0F, .high = months}},
      {.key = kDistrictVisitWorldParamKeys[1],
       .value = &values[1],
       .range = {.low = 1.0F, .high = months}},
      {.key = kDistrictVisitWorldParamKeys[2],
       .value = &values[2],
       .range = {.low = 0.0F, .high = static_cast<float>(kDaysPerMonth)}},
  }};
  if (!ReadKnobs(*world, "world_params", knobs, error)) {
    return false;
  }
  std::array<std::int32_t, kDistrictVisitWorldParamKeys.size()> whole{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (!WholeValue(values[index], whole[index])) {
      error = "world_params: " + std::string(kDistrictVisitWorldParamKeys[index]) +
              " is not a whole number";
      return false;
    }
  }
  catalog.karasev_month = static_cast<std::uint8_t>(whole[0]);
  catalog.polushkina_month = static_cast<std::uint8_t>(whole[1]);
  catalog.notice_days = static_cast<std::uint32_t>(whole[2]);
  return true;
}

}  // namespace core
