#include "core_catalog/world_junctions.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core_tables/tables.h"

namespace core {
namespace {

bool BorderOf(std::string_view word, BorderSide& side) {
  if (word == "north") {
    side = BorderSide::kNorth;
  } else if (word == "south") {
    side = BorderSide::kSouth;
  } else if (word == "east") {
    side = BorderSide::kEast;
  } else if (word == "west") {
    side = BorderSide::kWest;
  } else {
    return false;
  }
  return true;
}

bool LeadsToOf(std::string_view word, JunctionLeadsTo& leads_to) {
  if (word == "district") {
    leads_to = JunctionLeadsTo::kDistrict;
  } else if (word == "neighbor") {
    leads_to = JunctionLeadsTo::kNeighbor;
  } else {
    return false;
  }
  return true;
}

bool RoleOf(std::string_view word, NeighborRole& role) {
  if (word.empty()) {
    role = NeighborRole::kNone;
  } else if (word == "strong") {
    role = NeighborRole::kStrong;
  } else if (word == "poor") {
    role = NeighborRole::kPoor;
  } else if (word == "middling") {
    role = NeighborRole::kMiddling;
  } else {
    return false;
  }
  return true;
}

}  // namespace

bool ReadWorldJunctions(const ITableSet& tables,
                        std::vector<JunctionView>& junctions,
                        std::string& error) {
  const ITable* table = tables.FindTable("world_junctions");
  if (table == nullptr) {
    junctions.clear();
    return true;
  }
  const std::uint32_t key_column = table->FindColumn("key");
  const std::uint32_t road_column = table->FindColumn("road_key");
  const std::uint32_t border_column = table->FindColumn("border");
  const std::uint32_t leads_column = table->FindColumn("leads_to");
  const std::uint32_t role_column = table->FindColumn("neighbor_role");
  if (key_column == kNoTableColumn || road_column == kNoTableColumn ||
      border_column == kNoTableColumn || leads_column == kNoTableColumn ||
      role_column == kNoTableColumn) {
    error =
        "world_junctions: a column of key, road_key, border, leads_to, neighbor_role is missing";
    return false;
  }
  // The border vertices live in roads.csv; without it, or without one of its
  // four columns, no junction can be placed, and that is said as such.
  const ITable* roads = tables.FindTable("roads");
  if (roads == nullptr) {
    error = "world_junctions: the set has no roads table to place the junctions on";
    return false;
  }
  const std::uint32_t road_key_column = roads->FindColumn("road");
  const std::uint32_t x_column = roads->FindColumn("x_m");
  const std::uint32_t y_column = roads->FindColumn("y_m");
  const std::uint32_t mark_column = roads->FindColumn("mark");
  if (road_key_column == kNoTableColumn || x_column == kNoTableColumn ||
      y_column == kNoTableColumn || mark_column == kNoTableColumn) {
    error = "world_junctions: roads lacks a column of road, x_m, y_m, mark";
    return false;
  }
  std::vector<JunctionView> read;
  for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
    JunctionView junction;
    junction.key = std::string(table->CellText(row, key_column));
    junction.road_key = std::string(table->CellText(row, road_column));
    const bool listed_before = std::ranges::any_of(
        read, [&](const JunctionView& seen) { return seen.key == junction.key; });
    if (junction.key.empty() || listed_before) {
      error =
          "world_junctions: row " + std::to_string(row) + " has an empty key or one listed twice";
      return false;
    }
    const std::string_view border = table->CellText(row, border_column);
    if (!BorderOf(border, junction.border) ||
        !LeadsToOf(table->CellText(row, leads_column), junction.leads_to) ||
        !RoleOf(table->CellText(row, role_column), junction.neighbor_role)) {
      error = "world_junctions: row " + std::to_string(row) + " (" + junction.key +
              ") names a border, a destination or a role the core does not know";
      return false;
    }
    // The district has no neighbour's role; a neighbour has one.
    if ((junction.leads_to == JunctionLeadsTo::kDistrict) !=
        (junction.neighbor_role == NeighborRole::kNone)) {
      error = "world_junctions: row " + std::to_string(row) + " (" + junction.key +
              ") — the district takes no neighbour's role, and a neighbour needs one";
      return false;
    }
    // The border vertex: the road's point marked `border_<side>` in roads.csv.
    const std::string mark = "border_" + std::string(border);
    bool found = false;
    for (std::uint32_t point = 0; point < roads->RowCount(); ++point) {
      if (roads->CellText(point, road_key_column) != junction.road_key ||
          roads->CellText(point, mark_column) != mark) {
        continue;
      }
      const std::optional<float> x_m = roads->CellReal(point, x_column);
      const std::optional<float> y_m = roads->CellReal(point, y_column);
      if (!x_m.has_value() || !y_m.has_value()) {
        error = "world_junctions: row " + std::to_string(row) + " (" + junction.key +
                ") — the vertex of road " + junction.road_key + " marked " + mark +
                " has no readable x_m, y_m";
        return false;
      }
      junction.position = Vec2{.x = *x_m, .y = *y_m};
      found = true;
      break;
    }
    if (!found) {
      error = "world_junctions: row " + std::to_string(row) + " (" + junction.key +
              ") — no vertex of road " + junction.road_key + " is marked " + mark;
      return false;
    }
    read.push_back(std::move(junction));
  }
  junctions = std::move(read);
  return true;
}

}  // namespace core
