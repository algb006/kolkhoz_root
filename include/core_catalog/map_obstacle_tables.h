/// @file
/// @brief Reads tables/map_areas.csv and tables/map_lines.csv into
///        MapObstacles (core_common/map_obstacles.h; delivery 7b).
/// @threading PARALLEL_READONLY
/// A pure function of the table set.
///
/// BOTH FILES ARE MANY ROWS A THING, like roads.csv: the first column names
/// the area or line, `seq` orders its points. An area or a line is listed in
/// the order its key first appears. Every `kind` and `mark` word is known or
/// the read fails — an obstacle the core did not understand is not an
/// obstacle the tracer may skip.

#ifndef CORE_CATALOG_MAP_OBSTACLE_TABLES_H_
#define CORE_CATALOG_MAP_OBSTACLE_TABLES_H_

#include <string>

#include "core_common/map_obstacles.h"

namespace core {

class ITableSet;

/// @brief Reads the three tables (map_places.csv one row a place, boss [72]:
///        a key, a kind among village_zone, dacha_zone, industry_zone and
///        meadow, the contour's centroid; a key listed twice refuses).
/// @param obstacles Replaced on success; untouched on failure.
/// @return true with no areas (no lines) when the set has no `map_areas`
///         (`map_lines`) table — unit tests and stub worlds, where the tracer
///         then says it saw nothing; false on a malformed table — a missing
///         column, a cell that does not parse, an unknown kind or mark, a key
///         whose kind changes, points out of `seq` order, an area of fewer
///         than three points or a line of fewer than two — with the reason
///         in `error`.
bool ReadMapObstacles(const ITableSet& tables, MapObstacles& obstacles, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_MAP_OBSTACLE_TABLES_H_
