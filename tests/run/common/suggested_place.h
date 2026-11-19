/// @file
/// @brief The elder's suggested places (tables/suggestions.csv) as the run's
/// chairman takes them: for a unit type, the first of its points whose plot
/// is free, in the table's row order — and a count of the marks that went on
/// a point and the marks that went past every point.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY (the human's word of 2 October 2026: «Мы не будем тестировать игру в
/// случаях когда игрок делает грубые ошибки размещая юниты и дороги. Вы
/// должны ботам задать оптимальные места для строек юнитов, дорог и
/// тропинок»; boss, boss-all-bot-reference-layout-2026-10-02 [6]): until
/// 0.37.159 one policy of thirteen read a suggestion — the first cattle yard;
/// every other unit went to the nearest free place round the OLD village, and
/// the food yard, the utility yard and the office stood 0.5-0.9 km from the
/// points the map gave them (seed 1931, year 10, core 0.37.99).
///
/// A TYPE WITH NO POINT, OR WITH EVERY POINT TAKEN, IS SAID ALOUD: the policy
/// falls back to its old place and the count of marks past the suggestion is
/// printed at the run's end — a balance measured on a layout nobody chose is
/// not a measurement (CLAUDE.md §5, «расстановка бота не проверена»).

#ifndef TESTS_RUN_COMMON_SUGGESTED_PLACE_H_
#define TESTS_RUN_COMMON_SUGGESTED_PLACE_H_

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "build_bans.h"
#include "core_common/geometry.h"
#include "core_common/plot.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

/// @brief One unit type's suggested points and what the policy did with them.
class SuggestedPlaces {
 public:
  /// @brief Reads the rows of `unit_type` from suggestions.csv, in order. A
  /// table set without the table, or without the type, has no points.
  SuggestedPlaces(const core::ITableSet& tables, std::string_view unit_type)
      : unit_type_(unit_type) {
    const core::ITable* table = tables.FindTable("suggestions");
    if (table == nullptr) {
      return;
    }
    const std::uint32_t type_column = table->FindColumn("unit_type");
    const std::uint32_t x_column = table->FindColumn("x_m");
    const std::uint32_t y_column = table->FindColumn("y_m");
    if (type_column == core::kNoTableColumn || x_column == core::kNoTableColumn ||
        y_column == core::kNoTableColumn) {
      return;
    }
    for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
      if (table->CellText(row, type_column) != unit_type) {
        continue;
      }
      const std::optional<float> x = table->CellReal(row, x_column);
      const std::optional<float> y = table->CellReal(row, y_column);
      if (x && y) {
        points_.push_back(core::Vec2{.x = *x, .y = *y});
      }
    }
    refused_.assign(points_.size(), 0);
    banned_.assign(points_.size(), 0);
    banned_day_.assign(points_.size(), 0);
    banned_road_.assign(points_.size(), std::string());
  }

  /// @brief The laid road nearest `point` on `world`, in one word: id, kind
  /// (road/path), origin (map/player), its distance and its axis's two ends
  /// (the end at a unit names whom it was laid for).
  static std::string NearestRoadOf(const core::WorldState& world, core::Vec2 point) {
    float best = 1e30F;
    std::string text = "none";
    for (std::size_t row = 0; row < world.roads.rows.size(); ++row) {
      const core::RoadRow& road = world.roads.rows[row];
      for (std::size_t index = 1; index < road.axis.size(); ++index) {
        const core::Vec2 a = road.axis[index - 1].position;
        const core::Vec2 b = road.axis[index].position;
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = (dx * dx) + (dy * dy);
        float along =
            length > 0.0F ? (((point.x - a.x) * dx) + ((point.y - a.y) * dy)) / length : 0.0F;
        along = along < 0.0F ? 0.0F : (along > 1.0F ? 1.0F : along);
        const float distance =
            std::hypot(point.x - (a.x + (dx * along)), point.y - (a.y + (dy * along)));
        if (distance < best) {
          best = distance;
          const core::Vec2 first = road.axis.front().position;
          const core::Vec2 last = road.axis.back().position;
          text = "id" + std::to_string(world.roads.row_ids[row].value) +
                 (road.kind == core::RoadKind::kPath ? ":path" : ":road") +
                 (road.origin == core::RoadOrigin::kMap ? ":map" : ":player") + ":" +
                 std::to_string(static_cast<int>(distance)) +
                 "m:" + std::to_string(static_cast<int>(first.x)) + "," +
                 std::to_string(static_cast<int>(first.y)) + "-" +
                 std::to_string(static_cast<int>(last.x)) + "," +
                 std::to_string(static_cast<int>(last.y));
        }
      }
    }
    return text;
  }

  /// @brief #1788: the suggested points passed over for a placement ban, with
  /// their places - one line, «none» when none.
  void BannedReport(std::string_view run_name) const {
    std::cout << run_name << ": BANNED1788 suggested " << unit_type_
              << " on a ban: " << banned_count_ << " of " << points_.size();
    for (std::size_t index = 0; index < points_.size(); ++index) {
      if (banned_[index] != 0) {
        // bits: 1 water, 2 forest, 4 reserve, 8 road, 16 plot, 32 edge (placement_bans.h)
        std::cout << " @" << points_[index].x << "," << points_[index].y << " bits "
                  << static_cast<unsigned>(banned_[index]) << " day " << banned_day_[index]
                  << " road " << banned_road_[index];
      }
    }
    std::cout << '\n';
  }

  /// @brief The first point whose plot of `radius` is free by the core's own
  /// rule (plot.h, FreePlot answers the point itself), counted as a mark on
  /// the suggestion; nothing — counted as a mark past it — when the type has
  /// no point or every point is taken. The caller places the unit there, or
  /// by its old rule when nothing comes back.
  /// A POINT HANDED OUT AND FREE AGAIN AT THE NEXT CALL WAS REFUSED by the
  /// core (the mark never stood): it is passed over from then on, or a policy
  /// that marks again the next day would ask for it for ever.
  std::optional<core::Vec2> Take(const core::WorldState& world,
                                 const core::PlotRules& rules,
                                 float radius,
                                 core::UnitTypeId type = core::UnitTypeId{}) {
    for (std::size_t index = 0; index < points_.size(); ++index) {
      if (refused_[index] != 0) {
        continue;
      }
      const core::Vec2& point = points_[index];
      // #1788: a suggested point that sits on a placement ban is passed over,
      // and named at the run's end (BannedReport) - the reference layout's
      // owner moves or keeps it.
      if (type.value != core::kInvalidDefIdValue && !BuildAllowed(type, point)) {
        if (banned_[index] == 0) {
          // The bits, and the day, the point was first found banned: a road
          // the bot laid later bans it on that day, a map area from day 0.
          const core::ISimulation* const simulation = BuildBansSimulation();
          banned_[index] = simulation != nullptr ? simulation->BuildBansToday(type, point) : 0xFF;
          banned_day_[index] = world.calendar.day;
          banned_road_[index] = NearestRoadOf(world, point);
          ++banned_count_;
        }
        continue;
      }
      const core::Vec2 free = core::FreePlot(world.units, rules, point, radius);
      if (std::hypot(free.x - point.x, free.y - point.y) > kSamePlaceMetres) {
        continue;
      }
      if (last_given_ == index) {
        refused_[index] = 1;
        ++refused_count_;
        continue;
      }
      last_given_ = index;
      ++on_;
      return point;
    }
    last_given_ = kNone;
    ++past_;
    return std::nullopt;
  }

  /// @brief One line for the run's end: the type, its points, the marks on
  /// them and past them — PAST in capitals when there were any.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": suggested places of " << unit_type_ << ": " << points_.size()
              << " point(s), " << on_ << " mark(s) on them";
    if (refused_count_ > 0) {
      std::cout << ", " << refused_count_ << " POINT(S) REFUSED BY THE CORE";
    }
    if (past_ > 0) {
      std::cout << ", " << past_ << " MARK(S) PAST THE SUGGESTION"
                << (points_.empty() ? " (the table has no point for it)" : " (every point taken)");
    }
    std::cout << "\n";
    if (banned_count_ > 0) {
      BannedReport(run_name);  // #1788: the layout's points on a ban, named
    }
  }

  std::size_t PointCount() const { return points_.size(); }

 private:
  /// FreePlot answers a free point itself; anything further is a neighbour's.
  static constexpr float kSamePlaceMetres = 1.0F;

  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  std::string unit_type_;
  std::vector<core::Vec2> points_;
  std::vector<std::uint8_t> refused_;
  std::size_t last_given_ = kNone;
  std::uint32_t on_ = 0;
  std::uint32_t past_ = 0;
  std::uint32_t refused_count_ = 0;
  std::vector<std::uint8_t> banned_;  ///< #1788: 1 - passed over for a placement ban.
  std::uint32_t banned_count_ = 0;
  std::vector<std::uint32_t> banned_day_;  ///< #1788: the day each was first found banned.
  std::vector<std::string> banned_road_;   ///< #1788: the nearest laid road then.
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SUGGESTED_PLACE_H_
