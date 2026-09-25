#include "start_roads.h"

#include <vector>

#include "core_catalog/map_roads.h"
#include "core_common/road_graph.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {

bool PlaceMapRoads(const ITableSet& tables,
                   const StartLayout& layout,
                   WorldState& world,
                   std::string& error) {
  std::vector<MapRoadDef> map_roads;
  if (!ReadMapRoads(tables, map_roads, error)) {
    return false;
  }
  // A layout road that names no road of the map is a broken pair of tables,
  // not a road with no wear: said, so the two exports are made to agree.
  for (const StartLayoutRow& entry : layout.rows) {
    if (entry.kind != LayoutKind::kRoad) {
      continue;
    }
    bool found = false;
    for (const MapRoadDef& road : map_roads) {
      found = found || road.key == entry.key;
    }
    if (!found) {
      error = "start roads: start_layout road '" + entry.key + "' is not on roads.csv";
      return false;
    }
  }
  for (std::size_t index = 0; index < map_roads.size(); ++index) {
    const MapRoadDef& map_road = map_roads[index];
    RoadRow road;
    road.kind = map_road.kind;
    road.surface = map_road.kind == RoadKind::kPath ? RoadSurface::kNone : RoadSurface::kDirt;
    road.origin = RoadOrigin::kMap;
    road.map_road = DefIdFromIndex<MapRoadIdTag>(index);
    road.removable = map_road.removable;
    road.axis = map_road.axis;
    float wear = 0.0F;
    for (const StartLayoutRow& entry : layout.rows) {
      if (entry.kind == LayoutKind::kRoad && entry.key == map_road.key) {
        wear = entry.start_wear_pct >= 0.0F ? entry.start_wear_pct : 0.0F;
        road.traffic_word = entry.traffic;
      }
    }
    // A PATH HAS NO WEAR (roads design §4: «Тропы износа не имеют»).
    if (road.kind == RoadKind::kPath) {
      wear = 0.0F;
    }
    road.stretches.assign(StretchCountForLength(RoadAxisLength(road.axis)),
                          RoadStretch{.wear_pct = wear});
    AppendRow(world.roads, std::move(road));
  }
  return true;
}

}  // namespace core
