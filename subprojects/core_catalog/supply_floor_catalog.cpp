/// @file
/// @brief Implementation of supply_floor_catalog.h.

#include "core_catalog/supply_floor_catalog.h"

#include <cstdint>
#include <string>

#include "core_catalog/table_value.h"
#include "core_common/site_supply_floor.h"
#include "core_tables/tables.h"

namespace core {

bool ReadSupplyFloorRules(const ITableSet& tables, SupplyFloorRules& rules, std::string& error) {
  const ITable* const logistics = tables.FindTable("logistics");
  if (logistics == nullptr) {
    return true;
  }
  // A game year at most, the ageing's own bound (logistics_config.cpp).
  auto after_days = static_cast<float>(rules.after_days);
  if (!OptionalValue(*logistics,
                     "site_supply_floor_after_days",
                     Range{.low = 0.0F, .high = 48.0F},
                     after_days,
                     error)) {
    PrefixError("logistics", "site_supply_floor_after_days", error);
    return false;
  }
  rules.after_days = static_cast<std::uint32_t>(after_days);
  if (!OptionalValue(*logistics, "site_supply_floor_share", Range::Unit(), rules.share, error)) {
    PrefixError("logistics", "site_supply_floor_share", error);
    return false;
  }
  return true;
}

}  // namespace core
