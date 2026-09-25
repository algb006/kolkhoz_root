#include "core_catalog/map_roads.h"

#include <cstdint>
#include <string_view>

#include "core_tables/tables.h"

namespace core {
namespace {

/// The core's reading of a vertex's `mark`. The map names its junctions and
/// borders by direction (junction_north, border_east) and its bridge ends by
/// side (bridge_south); the core needs what they ARE, not where they face.
RoadMark MarkOf(std::string_view mark) {
  if (mark.empty()) {
    return RoadMark::kNone;
  }
  if (mark.starts_with("junction")) {
    return RoadMark::kJunction;
  }
  if (mark.starts_with("border")) {
    return RoadMark::kBorder;
  }
  // The stone arch is a bridge in the road's embankment (map.db `stone_arch`:
  // «Род моста»), and the core treats both the same: a passable piece.
  if (mark.starts_with("bridge") || mark == "arch") {
    return RoadMark::kBridge;
  }
  if (mark == "ford") {
    return RoadMark::kFord;
  }
  return RoadMark::kOther;  // village, forest, forest_edge, grove_exit
}

}  // namespace

bool ReadMapRoads(const ITableSet& tables, std::vector<MapRoadDef>& roads, std::string& error) {
  const ITable* table = tables.FindTable("roads");
  if (table == nullptr) {
    roads.clear();
    return true;
  }
  const std::uint32_t road_column = table->FindColumn("road");
  const std::uint32_t kind_column = table->FindColumn("kind");
  const std::uint32_t removable_column = table->FindColumn("removable");
  const std::uint32_t seq_column = table->FindColumn("seq");
  const std::uint32_t x_column = table->FindColumn("x_m");
  const std::uint32_t y_column = table->FindColumn("y_m");
  const std::uint32_t mark_column = table->FindColumn("mark");
  if (road_column == kNoTableColumn || kind_column == kNoTableColumn ||
      removable_column == kNoTableColumn || seq_column == kNoTableColumn ||
      x_column == kNoTableColumn || y_column == kNoTableColumn || mark_column == kNoTableColumn) {
    error = "roads: a column of road, kind, removable, seq, x_m, y_m, mark is missing";
    return false;
  }
  std::vector<MapRoadDef> read;
  std::vector<std::int64_t> last_seq;
  for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
    const std::string_view key = table->CellText(row, road_column);
    const std::optional<std::int64_t> seq = table->CellInteger(row, seq_column);
    const std::optional<float> x = table->CellReal(row, x_column);
    const std::optional<float> y = table->CellReal(row, y_column);
    const std::optional<std::int64_t> removable = table->CellInteger(row, removable_column);
    const std::string_view kind = table->CellText(row, kind_column);
    const std::string where = "roads: row " + std::to_string(row) + " (" + std::string(key) + ")";
    if (key.empty() || !seq || !x || !y || !removable || (*removable != 0 && *removable != 1)) {
      error = where + ": road, seq, x_m, y_m or removable does not parse";
      return false;
    }
    if (kind != "road" && kind != "path") {
      error = where + ": kind '" + std::string(kind) + "' is neither road nor path";
      return false;
    }
    std::size_t index = read.size();
    for (std::size_t known = 0; known < read.size(); ++known) {
      if (read[known].key == key) {
        index = known;
        break;
      }
    }
    if (index == read.size()) {
      MapRoadDef road;
      road.key = std::string(key);
      road.kind = kind == "path" ? RoadKind::kPath : RoadKind::kRoad;
      road.removable = static_cast<std::uint8_t>(*removable);
      read.push_back(std::move(road));
      last_seq.push_back(-1);
    } else if (*seq <= last_seq[index]) {
      error = where + ": seq " + std::to_string(*seq) + " does not follow " +
              std::to_string(last_seq[index]);
      return false;
    }
    last_seq[index] = *seq;
    read[index].axis.push_back(RoadPoint{.position = Vec2{.x = *x, .y = *y},
                                         .mark = MarkOf(table->CellText(row, mark_column))});
  }
  for (const MapRoadDef& road : read) {
    if (road.axis.size() < 2) {
      error = "roads: '" + road.key + "' has fewer than two points";
      return false;
    }
  }
  roads = std::move(read);
  return true;
}

}  // namespace core
