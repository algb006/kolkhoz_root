#include "start_roads.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/map_roads.h"
#include "core_common/road_graph.h"
#include "core_common/road_route.h"
#include "core_common/road_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// A stretch's reach the overlay may miss the axis's end by: the export
/// rounds metres to a tenth.
constexpr float kAxisEndSlackMetres = 0.5F;

/// LOCAL WEAR (roads design §4; tables/road_wear.csv, 0.37.55): a piece of a
/// road worn past the road's own start wear — the pit on the village street
/// by the church store (boss-core-start-quest-facts-2026-09-30 [11]). Every
/// stretch the piece touches, even by an edge, takes its wear: the core keeps
/// wear by kRoadStretchMetres and no finer. A set without the table has no
/// local wear.
bool ApplyLocalWear(const ITableSet& tables,
                    const std::vector<MapRoadDef>& map_roads,
                    WorldState& world,
                    std::string& error) {
  const ITable* const table = tables.FindTable("road_wear");
  if (table == nullptr) {
    return true;
  }
  const std::uint32_t road_column = table->FindColumn("road");
  const std::uint32_t from_column = table->FindColumn("from_m");
  const std::uint32_t to_column = table->FindColumn("to_m");
  const std::uint32_t wear_column = table->FindColumn("wear_pct");
  if (road_column == kNoTableColumn || from_column == kNoTableColumn ||
      to_column == kNoTableColumn || wear_column == kNoTableColumn) {
    error = "road_wear: a column of road, from_m, to_m, wear_pct is missing";
    return false;
  }
  for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
    const std::string_view key = table->CellText(row, road_column);
    const std::optional<float> from = table->CellReal(row, from_column);
    const std::optional<float> to = table->CellReal(row, to_column);
    const std::optional<float> wear = table->CellReal(row, wear_column);
    const std::string where =
        "road_wear: row " + std::to_string(row) + " (" + std::string(key) + ")";
    if (!from || !to || !wear || *from < 0.0F || *to <= *from || *wear < 0.0F || *wear > 100.0F) {
      error = where + ": from_m, to_m or wear_pct does not parse, or is out of range";
      return false;
    }
    std::size_t index = map_roads.size();
    for (std::size_t known = 0; known < map_roads.size(); ++known) {
      if (map_roads[known].key == key) {
        index = known;
        break;
      }
    }
    if (index == map_roads.size()) {
      error = where + ": the road is not on roads.csv";
      return false;
    }
    if (map_roads[index].kind == RoadKind::kPath) {
      error = where + ": a path has no wear (roads design §4)";
      return false;
    }
    RoadRow* road = nullptr;
    for (RoadRow& placed : world.roads.rows) {
      if (placed.origin == RoadOrigin::kMap &&
          placed.map_road.value == DefIdFromIndex<MapRoadIdTag>(index).value) {
        road = &placed;
        break;
      }
    }
    const float length = RoadAxisLength(map_roads[index].axis);
    if (road == nullptr || road->stretches.empty() || *to > length + kAxisEndSlackMetres) {
      error = where + ": the piece runs past the road's axis";
      return false;
    }
    const auto first = static_cast<std::size_t>(*from / kRoadStretchMetres);
    const std::size_t last =
        std::min(road->stretches.size() - 1,
                 static_cast<std::size_t>(std::ceil(*to / kRoadStretchMetres)) - 1);
    for (std::size_t stretch = first; stretch <= last; ++stretch) {
      road->stretches[stretch].wear_pct = *wear;
    }
  }
  return true;
}

}  // namespace

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
  if (!ApplyLocalWear(tables, map_roads, world, error)) {
    return false;
  }
  world.road_index = BuildRoadIndex(world.roads);
  return true;
}

}  // namespace core
