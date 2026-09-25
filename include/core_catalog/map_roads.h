/// @file
/// @brief The roads the map comes with, read from tables/roads.csv — the
///        seed of the road network (roads design §13).
/// @threading PARALLEL_READONLY
/// A pure function of the table set.
///
/// ONE READER FOR TWO DOORS: genesis builds the network from it, and the save
/// puts a map road's axis back from it on load (a map road's axis is never
/// written to a save; core_common/road_state.h). Two readers would be two
/// answers to "where does the trunk road run".
///
/// THE FILE IS MANY ROWS A ROAD (its first column is `road`, not `key`):
/// the drawn line, point by point, in `seq` order. The roads are numbered in
/// the order their keys first appear, which is what a save's dictionary of
/// map roads records and remaps by.

#ifndef CORE_CATALOG_MAP_ROADS_H_
#define CORE_CATALOG_MAP_ROADS_H_

#include <string>
#include <vector>

#include "core_common/road_state.h"

namespace core {

class ITableSet;

/// @brief One road of the map, as the table draws it.
struct MapRoadDef {
  std::string key;

  RoadKind kind = RoadKind::kRoad;

  /// roads.csv `removable`.
  std::uint8_t removable = 1;

  /// The drawn line in `seq` order, with its marks.
  std::vector<RoadPoint> axis;
};

/// @brief Reads tables/roads.csv.
/// @param roads Replaced on success: one entry a road, in the order its key
///        first appears in the file. Left untouched on failure.
/// @return true with an empty list when the set has no `roads` table (unit
///         tests and stub worlds); false on a malformed table — a missing
///         column, a cell that does not parse, a road of fewer than two
///         points, or a road's rows out of `seq` order — with the reason in
///         `error`.
bool ReadMapRoads(const ITableSet& tables, std::vector<MapRoadDef>& roads, std::string& error);

}  // namespace core

#endif  // CORE_CATALOG_MAP_ROADS_H_
