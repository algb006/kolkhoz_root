/// @file
/// @brief The elder's suggested roads and paths, laid by the run's chairman
/// with the first new building beside each.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUILDS THIS (#423; the human's word of 2 October 2026: «Вы
/// должны ботам задать оптимальные места для строек юнитов, дорог и
/// тропинок»; econ's plan 423-suggested-roads-plan-2026-10-11). The map's
/// suggested roads (tables/suggested_roads.csv, exported from map.db) were
/// read by no run: the bot built the new village with no street.
///
/// THE RULE IS THE RUN'S: a suggested road or path is laid the day a unit
/// that was not in the start stands or is marked within kNearM of its drawn
/// line — the street with the first building on it, the path with the first
/// house beside it (econ's order, by geometry, not by name). Laid with
/// kLayRoad at the dirt surface (a path: no surface) from the road's
/// waypoints (node 1), a draft of at most kRoadDraftMaxPoints points at a
/// time chained end to end. A dirt road and a path cost nothing and are laid
/// at once (roads design §9, «Грунтовка ничего не стоит»; order_state.h).

#ifndef TESTS_RUN_COMMON_SUGGESTED_ROADS_POLICY_H_
#define TESTS_RUN_COMMON_SUGGESTED_ROADS_POLICY_H_

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/order_state.h"
#include "core_common/road_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Lays each suggested road once a new building stands beside it.
class SuggestedRoadsPolicy {
 public:
  explicit SuggestedRoadsPolicy(const core::ITableSet& tables) {
    const core::ITable* table = tables.FindTable("suggested_roads");
    if (table == nullptr) {
      return;
    }
    const std::uint32_t road_col = table->FindColumn("road");
    const std::uint32_t kind_col = table->FindColumn("kind");
    const std::uint32_t node_col = table->FindColumn("node");
    const std::uint32_t x_col = table->FindColumn("x_m");
    const std::uint32_t y_col = table->FindColumn("y_m");
    if (road_col == core::kNoTableColumn || x_col == core::kNoTableColumn ||
        y_col == core::kNoTableColumn) {
      return;
    }
    std::map<std::string, std::size_t> index;
    for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
      const std::string key(table->CellText(row, road_col));
      if (index.count(key) == 0) {
        index[key] = roads_.size();
        Road road;
        road.key = key;
        road.path = kind_col != core::kNoTableColumn &&
                    table->CellText(row, kind_col).find("path") != std::string_view::npos;
        roads_.push_back(road);
      }
      Road& road = roads_[index[key]];
      const core::Vec2 point{.x = Number(*table, row, x_col), .y = Number(*table, row, y_col)};
      road.line.push_back(point);
      if (node_col != core::kNoTableColumn && Number(*table, row, node_col) != 0.0F) {
        road.waypoints.push_back(point);
      }
    }
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (!start_read_) {
      start_read_ = true;
      for (const core::UnitId unit : world.units.row_ids) {
        start_units_.insert(unit.value);
      }
    }
    std::vector<core::OrderRow> orders;
    for (Road& road : roads_) {
      if (road.laid_day >= 0 || road.waypoints.size() < 2 ||
          (!NewUnitBeside(world, road) && !GroveLinkIsDue(world, road))) {
        continue;
      }
      road.laid_day = static_cast<std::int64_t>(world.calendar.day);
      // A chain of drafts of at most kRoadDraftMaxPoints points, each starting
      // where the last ended.
      for (std::size_t first = 0; first + 1 < road.waypoints.size();
           first += core::kRoadDraftMaxPoints - 1U) {
        core::OrderRow order;
        order.kind = core::OrderKind::kLayRoad;
        order.road_kind = road.path ? core::RoadKind::kPath : core::RoadKind::kRoad;
        order.road_surface = road.path ? core::RoadSurface::kNone : core::RoadSurface::kDirt;
        std::uint8_t count = 0;
        for (std::size_t point = first;
             point < road.waypoints.size() && count < core::kRoadDraftMaxPoints;
             ++point) {
          order.road_points[count++] = road.waypoints[point];
        }
        order.road_point_count = count;
        orders.push_back(order);
        ++road.drafts;
      }
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout
        << run_name
        << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman lays the elder's "
           "SUGGESTED ROADS AND PATHS (suggested_roads.csv), each the day a building that was "
           "not in the start stands or is marked within "
        << kNearM
        << " m of it, at the dirt surface, free (#423; roads design §9; econ's plan "
           "423-suggested-roads-plan-2026-10-11); a road into a grove on the first day of year "
        << kGroveLinkYear << " (#1205 (r1'))\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": suggested roads — " << roads_.size() << " in the table:";
    for (const Road& road : roads_) {
      std::cout << ' ' << road.key << (road.laid_day >= 0 ? " laid day " : " NOT LAID")
                << (road.laid_day >= 0 ? std::to_string(road.laid_day) : std::string()) << " ("
                << road.drafts << " drafts);";
    }
    std::cout << '\n';
  }

 private:
  /// A new unit within this many metres of the road's drawn line lays it.
  static constexpr float kNearM = 60.0F;

  /// THE FOREST'S TRACK IS LAID BY THE CALENDAR, NOT BY A BUILDING (#1205
  /// (r1′); boss, boss-core-1205-birth-prints [47]; econ, econ-boss-1205-c33-
  /// births [11]): no building stands beside a road into a grove, so the rule
  /// above never laid it. A suggested road whose key names a grove is laid on
  /// the first day of this game year - econ's «by y9-y10», the year the forest
  /// inside the log cart's reach is below a few years of the felling's want on
  /// the canon (rch1205: 1 288 m3 at y10, 390 at y14). Her other trigger, «the
  /// stock inside the reach below three years of `wanted`», is NOT built: the
  /// year stands for it, named.
  static constexpr std::uint32_t kGroveLinkYear = 9;

  struct Road {
    std::string key;
    bool path = false;
    std::vector<core::Vec2> line;
    std::vector<core::Vec2> waypoints;
    std::int64_t laid_day = -1;
    std::uint32_t drafts = 0;
  };

  static float Number(const core::ITable& table, std::uint32_t row, std::uint32_t column) {
    return std::strtof(std::string(table.CellText(row, column)).c_str(), nullptr);
  }

  bool NewUnitBeside(const core::WorldState& world, const Road& road) const {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (start_units_.count(world.units.row_ids[row].value) != 0 ||
          world.units.rows[row].dead != 0) {
        continue;
      }
      const core::Vec2 at = world.units.rows[row].position;
      for (const core::Vec2& point : road.line) {
        const float dx = at.x - point.x;
        const float dy = at.y - point.y;
        if (dx * dx + dy * dy <= kNearM * kNearM) {
          return true;
        }
      }
    }
    return false;
  }

  static bool GroveLinkIsDue(const core::WorldState& world, const Road& road) {
    return road.key.find("_grove_") != std::string::npos &&
           world.calendar.day >= (kGroveLinkYear - 1U) * core::kDaysPerYear;
  }

  std::vector<Road> roads_;
  std::set<std::uint32_t> start_units_;
  bool start_read_ = false;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SUGGESTED_ROADS_POLICY_H_
