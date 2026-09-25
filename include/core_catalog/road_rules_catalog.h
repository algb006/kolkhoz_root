/// @file
/// @brief The roads' numbers out of world_params.csv (core_common/road_rules.h).
/// @threading SINGLE_THREADED
/// Parsed once at assembly on the sim thread; read-only afterwards.
///
/// OPTIONAL, LIKE EVERY KNOB OF THAT TABLE: a set without a row keeps the
/// default, which equals the export. A present row out of its range refuses.

#ifndef CORE_CATALOG_ROAD_RULES_CATALOG_H_
#define CORE_CATALOG_ROAD_RULES_CATALOG_H_

#include <span>
#include <string>
#include <string_view>

#include "core_common/road_rules.h"
#include "core_tables/tables.h"

namespace core {

/// @brief The world_params.csv keys the roads read — for the assembly's
///        declared-readers check.
std::span<const std::string_view> RoadWorldParamKeys();

/// @brief Reads the `road_` rows of world_params.csv into `rules`.
/// @param tables The table set; a set without world_params keeps `rules`.
/// @param rules Overwritten row by row where a row is present.
/// @param error Set to the table, key and fault on refusal.
/// @return false when a present row is not a number or out of its range,
///         or when the traffic thresholds are not in falling order.
bool ParseRoadRules(const ITableSet& tables, RoadRules& rules, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_ROAD_RULES_CATALOG_H_
