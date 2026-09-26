/// @file
/// @brief What laying a surface of road costs and when it opens, out of the
///        `road` rows of unit_levels.csv and unit_level_cost.csv (delivery
///        7b): the tracer's estimate reads it.
/// @threading PARALLEL_READONLY
/// A pure function of the table set.
///
/// THE LEVELS ARE THE SURFACES: level 1 dirt (free, not built), level 2
/// gravel, level 3 asphalt (roads design §2). Asphalt with walks has no level
/// of its own and costs as asphalt — STUB (boss [64]). THE LENGTH BASIS of the
/// rows is «на 100 м полотна 8 м» — STUB too, the design names no length
/// (boss [64]).

#ifndef CORE_CATALOG_ROAD_COST_CATALOG_H_
#define CORE_CATALOG_ROAD_COST_CATALOG_H_

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/road_state.h"

namespace core {

class ITableSet;

/// @brief One surface's price and its epoch.
struct RoadSurfaceLevel {
  /// The epoch it opens in (unit_levels.csv `era`); path and dirt: the first.
  Epoch opens = Epoch::kOne;

  /// Трудодни per 100 m (unit_levels.csv `labor_days`).
  float man_days_per_100m = 0.0F;

  /// Grams per 100 m, dense by ResourceId (unit_level_cost.csv `amount` times
  /// resources.csv `kg_per_unit`).
  ResourceAmounts materials_per_100m;
};

/// @brief By RoadSurface.
using RoadSurfaceLevels =
    std::array<RoadSurfaceLevel, static_cast<std::size_t>(RoadSurface::kRoadSurfaceCount)>;

/// @brief Reads the `road` rows.
/// @param levels Replaced on success; untouched on failure.
/// @return true with every surface free and the design's epochs (asphalt in
///         Epoch II, the rest in Epoch I) when the set has no unit_levels
///         (unit tests); false on a road row that does not
///         parse or names an unknown resource, or a resource without a mass,
///         with the reason in `error`.
bool ReadRoadSurfaceLevels(const ITableSet& tables, RoadSurfaceLevels& levels, std::string& error);

/// @brief The world_params.csv keys the road tools read — for the
///        assembly's declared-readers check.
std::span<const std::string_view> RoadToolWorldParamKeys();

/// @brief Reads `clearing_trudodni_per_ha` (boss [66]/[68]: 150, STUB with
///        its reasoning) into `per_ha`.
/// @param per_ha Overwritten when the row is present; kept when the set has
///        no world_params or no such row (the compiled default).
/// @return false when the row is present and not a number in 0..2000.
bool ReadClearingLabour(const ITableSet& tables, float& per_ha, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_ROAD_COST_CATALOG_H_
