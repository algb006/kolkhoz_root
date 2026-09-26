#include "core_catalog/map_obstacle_tables.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core_tables/tables.h"

namespace core {
namespace {

constexpr std::array<std::string_view, static_cast<std::size_t>(MapAreaKind::kMapAreaKindCount)>
    kAreaKindWords = {"lake",
                      "pond",
                      "backwater",
                      "shallows",
                      "forest",
                      "floodplain",
                      "grove",
                      "old_orchard",
                      "reserve",
                      "ruins_site",
                      "village_zone"};

std::optional<MapAreaKind> AreaKindOf(std::string_view word) {
  for (std::size_t index = 0; index < kAreaKindWords.size(); ++index) {
    if (kAreaKindWords[index] == word) {
      return static_cast<MapAreaKind>(index);
    }
  }
  return std::nullopt;
}

std::optional<MapLineKind> LineKindOf(std::string_view word) {
  if (word == "river") {
    return MapLineKind::kRiver;
  }
  if (word == "brook") {
    return MapLineKind::kBrook;
  }
  return std::nullopt;
}

std::optional<MapLineMark> LineMarkOf(std::string_view word) {
  if (word.empty()) {
    return MapLineMark::kNone;
  }
  if (word == "ford") {
    return MapLineMark::kFord;
  }
  if (word == "dam") {
    return MapLineMark::kDam;
  }
  if (word == "arch") {
    return MapLineMark::kArch;
  }
  if (word == "mouth" || word == "pond" || word == "pool" || word == "spring" || word == "fall") {
    return MapLineMark::kOther;
  }
  return std::nullopt;
}

/// The columns both files share: the thing's key (first column), kind, seq
/// and the point.
struct PointColumns {
  std::uint32_t key = kNoTableColumn;
  std::uint32_t kind = kNoTableColumn;
  std::uint32_t seq = kNoTableColumn;
  std::uint32_t x = kNoTableColumn;
  std::uint32_t y = kNoTableColumn;
};

bool FindPointColumns(const ITable& table,
                      std::string_view key_name,
                      std::string_view file,
                      PointColumns& columns,
                      std::string& error) {
  columns.key = table.FindColumn(key_name);
  columns.kind = table.FindColumn("kind");
  columns.seq = table.FindColumn("seq");
  columns.x = table.FindColumn("x_m");
  columns.y = table.FindColumn("y_m");
  if (columns.key == kNoTableColumn || columns.kind == kNoTableColumn ||
      columns.seq == kNoTableColumn || columns.x == kNoTableColumn || columns.y == kNoTableColumn) {
    error = std::string(file) + ": a column of " + std::string(key_name) +
            ", kind, seq, x_m, y_m is missing";
    return false;
  }
  return true;
}

bool ReadAreas(const ITable& table, std::vector<MapAreaDef>& areas, std::string& error) {
  PointColumns columns;
  if (!FindPointColumns(table, "area", "map_areas", columns, error)) {
    return false;
  }
  std::unordered_map<std::string, std::size_t> index_of;
  std::vector<std::int64_t> last_seq;
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    const std::string_view key = table.CellText(row, columns.key);
    const std::string_view kind_word = table.CellText(row, columns.kind);
    const std::optional<std::int64_t> seq = table.CellInteger(row, columns.seq);
    const std::optional<float> x = table.CellReal(row, columns.x);
    const std::optional<float> y = table.CellReal(row, columns.y);
    const std::string where =
        "map_areas: row " + std::to_string(row) + " (" + std::string(key) + ")";
    const std::optional<MapAreaKind> kind = AreaKindOf(kind_word);
    if (key.empty() || !seq || !x || !y) {
      error = where + ": area, seq, x_m or y_m does not parse";
      return false;
    }
    if (!kind) {
      error = where + ": kind '" + std::string(kind_word) + "' is not an area kind the core knows";
      return false;
    }
    const auto [found, fresh] = index_of.try_emplace(std::string(key), areas.size());
    if (fresh) {
      areas.push_back(MapAreaDef{.key = std::string(key), .kind = *kind, .outline = {}});
      last_seq.push_back(-1);
    }
    const std::size_t index = found->second;
    if (areas[index].kind != *kind) {
      error = where + ": the area's kind changes to '" + std::string(kind_word) + "'";
      return false;
    }
    if (*seq <= last_seq[index]) {
      error = where + ": seq " + std::to_string(*seq) + " does not follow " +
              std::to_string(last_seq[index]);
      return false;
    }
    last_seq[index] = *seq;
    areas[index].outline.push_back(Vec2{.x = *x, .y = *y});
  }
  for (const MapAreaDef& area : areas) {
    if (area.outline.size() < 3) {
      error = "map_areas: '" + area.key + "' has fewer than three points";
      return false;
    }
  }
  return true;
}

bool ReadLines(const ITable& table, std::vector<MapLineDef>& lines, std::string& error) {
  PointColumns columns;
  if (!FindPointColumns(table, "line", "map_lines", columns, error)) {
    return false;
  }
  const std::uint32_t half_width_column = table.FindColumn("half_w_m");
  const std::uint32_t mark_column = table.FindColumn("mark");
  if (half_width_column == kNoTableColumn || mark_column == kNoTableColumn) {
    error = "map_lines: a column of half_w_m, mark is missing";
    return false;
  }
  std::unordered_map<std::string, std::size_t> index_of;
  std::vector<std::int64_t> last_seq;
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    const std::string_view key = table.CellText(row, columns.key);
    const std::string_view kind_word = table.CellText(row, columns.kind);
    const std::string_view mark_word = table.CellText(row, mark_column);
    const std::optional<std::int64_t> seq = table.CellInteger(row, columns.seq);
    const std::optional<float> x = table.CellReal(row, columns.x);
    const std::optional<float> y = table.CellReal(row, columns.y);
    const std::optional<float> half_width = table.CellReal(row, half_width_column);
    const std::string where =
        "map_lines: row " + std::to_string(row) + " (" + std::string(key) + ")";
    const std::optional<MapLineKind> kind = LineKindOf(kind_word);
    const std::optional<MapLineMark> mark = LineMarkOf(mark_word);
    if (key.empty() || !seq || !x || !y || !half_width || *half_width < 0.0F) {
      error = where + ": line, seq, x_m, y_m or half_w_m does not parse";
      return false;
    }
    if (!kind) {
      error = where + ": kind '" + std::string(kind_word) + "' is neither river nor brook";
      return false;
    }
    if (!mark) {
      error = where + ": mark '" + std::string(mark_word) + "' is not a mark the core knows";
      return false;
    }
    const auto [found, fresh] = index_of.try_emplace(std::string(key), lines.size());
    if (fresh) {
      lines.push_back(MapLineDef{.key = std::string(key), .kind = *kind, .points = {}});
      last_seq.push_back(-1);
    }
    const std::size_t index = found->second;
    if (lines[index].kind != *kind) {
      error = where + ": the line's kind changes to '" + std::string(kind_word) + "'";
      return false;
    }
    if (*seq <= last_seq[index]) {
      error = where + ": seq " + std::to_string(*seq) + " does not follow " +
              std::to_string(last_seq[index]);
      return false;
    }
    last_seq[index] = *seq;
    lines[index].points.push_back(MapLinePoint{
        .position = Vec2{.x = *x, .y = *y}, .half_width_m = *half_width, .mark = *mark});
  }
  for (const MapLineDef& line : lines) {
    if (line.points.size() < 2) {
      error = "map_lines: '" + line.key + "' has fewer than two points";
      return false;
    }
  }
  return true;
}

std::optional<MapPlaceKind> PlaceKindOf(std::string_view word) {
  if (word == "village_zone") {
    return MapPlaceKind::kVillageZone;
  }
  if (word == "dacha_zone") {
    return MapPlaceKind::kDachaZone;
  }
  if (word == "industry_zone") {
    return MapPlaceKind::kIndustryZone;
  }
  if (word == "meadow") {
    return MapPlaceKind::kMeadow;
  }
  return std::nullopt;
}

/// map_places.csv: ONE ROW A PLACE (unlike the other two), `place` the key.
bool ReadPlaces(const ITable& table, std::vector<MapPlaceDef>& places, std::string& error) {
  const std::uint32_t key_column = table.FindColumn("place");
  const std::uint32_t kind_column = table.FindColumn("kind");
  const std::uint32_t x_column = table.FindColumn("x_m");
  const std::uint32_t y_column = table.FindColumn("y_m");
  if (key_column == kNoTableColumn || kind_column == kNoTableColumn || x_column == kNoTableColumn ||
      y_column == kNoTableColumn) {
    error = "map_places: a column of place, kind, x_m, y_m is missing";
    return false;
  }
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    const std::string_view key = table.CellText(row, key_column);
    const std::string_view kind_word = table.CellText(row, kind_column);
    const std::optional<float> x = table.CellReal(row, x_column);
    const std::optional<float> y = table.CellReal(row, y_column);
    const std::optional<MapPlaceKind> kind = PlaceKindOf(kind_word);
    const std::string where =
        "map_places: row " + std::to_string(row) + " (" + std::string(key) + ")";
    if (key.empty() || !x || !y) {
      error = where + ": place, x_m or y_m does not parse";
      return false;
    }
    if (!kind) {
      error = where + ": kind '" + std::string(kind_word) + "' is not a place kind the core knows";
      return false;
    }
    if (std::any_of(places.begin(), places.end(), [key](const MapPlaceDef& place) {
          return place.key == key;
        })) {
      error = where + ": the place is listed twice";
      return false;
    }
    places.push_back(
        MapPlaceDef{.key = std::string(key), .kind = *kind, .point = Vec2{.x = *x, .y = *y}});
  }
  return true;
}

}  // namespace

bool ReadMapObstacles(const ITableSet& tables, MapObstacles& obstacles, std::string& error) {
  MapObstacles read;
  if (const ITable* areas = tables.FindTable("map_areas")) {
    if (!ReadAreas(*areas, read.areas, error)) {
      return false;
    }
  }
  if (const ITable* lines = tables.FindTable("map_lines")) {
    if (!ReadLines(*lines, read.lines, error)) {
      return false;
    }
  }
  if (const ITable* places = tables.FindTable("map_places")) {
    if (!ReadPlaces(*places, read.places, error)) {
      return false;
    }
  }
  obstacles = std::move(read);
  return true;
}

}  // namespace core
