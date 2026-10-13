/// @file
/// @brief The tables' numbers of the floor under a works' supply (core_common/
///        site_supply_floor.h; #734): one reading of logistics.csv for
///        labour's placement and logistics' lamp alike.
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.

#ifndef CORE_CATALOG_SUPPLY_FLOOR_CATALOG_H_
#define CORE_CATALOG_SUPPLY_FLOOR_CATALOG_H_

#include <string>

#include "core_common/site_supply_floor.h"
#include "core_tables/tables.h"

namespace core {

/// @brief Reads logistics.csv's `site_supply_floor_after_days` (0..48 whole
///        days; 0 switches the floor off) and `site_supply_floor_share`
///        (0..1). An absent table or key leaves the rule's default (a stub
///        table set, legal).
/// @return false with `error` for a cell out of its range.
bool ReadSupplyFloorRules(const ITableSet& tables, SupplyFloorRules& rules, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_SUPPLY_FLOOR_CATALOG_H_
