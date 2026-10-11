/// @file
/// @brief The tables' data the cold-housing rule reads (core_common/
///        cold_housing.h): one reading of livestock.csv and unit_levels.csv
///        for labour's allocation and logistics' plan alike.
/// @threading PARALLEL_READONLY
/// Parsed once at assembly on the sim thread; read-only afterwards.

#ifndef CORE_CATALOG_COLD_HOUSING_CATALOG_H_
#define CORE_CATALOG_COLD_HOUSING_CATALOG_H_

#include <string>

#include "core_common/cold_housing.h"
#include "core_tables/tables.h"

namespace core {

/// @brief Reads livestock.csv's `home_unit` (a key of unit_types.csv) and
///        `cold_night_cold_place_c` (non-empty: the kind freezes in a cold
///        place), and unit_levels.csv's `warm_place` by unit and level.
///        Absent tables or columns leave the data empty — the rule then never
///        holds (a stub table set, legal).
/// @return false with `error` for a home_unit naming no unit type.
bool ReadColdHousing(const ITableSet& tables, ColdHousing& cold, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_COLD_HOUSING_CATALOG_H_
