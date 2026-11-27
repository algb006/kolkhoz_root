/// @file
/// @brief HOUSE1907 (#1907's pre-pair check, not for release): which of suggestions.csv's
///        bot_house_point rows the core's own build check would refuse a wooden_house at, by
///        reason - the placement bans (ISimulation::BuildBansToday: water, forest, reserve, a
///        laid road, the edge band) and another unit's plot (the sum of the two plot radii, as
///        kTooClose reads it) - read on the first day and on year 8's first day (the points'
///        years begin). Slope is NOT a core build check (no rule reads it) and is not printed.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_HOUSE_POINT_PROBE_H_
#define TESTS_RUN_COMMON_HOUSE_POINT_PROBE_H_

#include <cmath>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/placement_bans.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class HousePointProbe {
 public:
  explicit HousePointProbe(const core::ITableSet& tables) {
    const core::ITable* const types = tables.FindTable("unit_types");
    if (types != nullptr) {
      house_ = types->FindRowByKey("wooden_house");
      const std::uint32_t radius = types->FindColumn("plot_radius_m");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        const std::optional<float> value =
            radius != core::kNoTableColumn ? types->CellReal(row, radius) : std::nullopt;
        radii_.push_back(value ? *value : 0.0F);
      }
    }
    if (const core::ITable* const points = tables.FindTable("suggestions")) {
      const std::uint32_t key = points->FindColumn("key");
      const std::uint32_t x = points->FindColumn("x_m");
      const std::uint32_t y = points->FindColumn("y_m");
      for (std::uint32_t row = 0; row < points->RowCount(); ++row) {
        const std::string name(points->CellText(row, key));
        if (name.rfind("bot_house_", 0) != 0) {
          continue;
        }
        points_.push_back(Point{.key = name,
                                .x = points->CellReal(row, x).value_or(0.0F),
                                .y = points->CellReal(row, y).value_or(0.0F)});
      }
    }
  }

  /// @brief Once a day after the step: reads the points on day 0 and on year 8's first day.
  void CountDay(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day = world.calendar.day;
    if ((day != 0 && day != 7U * core::kDaysPerYear) || house_ == core::kNoTableRow) {
      return;
    }
    const float own = house_ < radii_.size() ? radii_[house_] : 0.0F;
    std::ostringstream line;
    std::uint32_t refused = 0;
    for (const Point& point : points_) {
      const core::Vec2 at{.x = point.x, .y = point.y};
      const std::uint8_t bans =
          simulation.BuildBansToday(core::UnitTypeId{static_cast<std::uint16_t>(house_)}, at);
      bool plot = false;
      for (const core::UnitRow& unit : world.units.rows) {
        if (unit.dead != 0 || unit.type.value >= radii_.size()) {
          continue;
        }
        const float reach = own + radii_[unit.type.value];
        const float dx = unit.position.x - point.x;
        const float dy = unit.position.y - point.y;
        plot = plot || std::sqrt((dx * dx) + (dy * dy)) < reach;
      }
      if (bans == 0 && !plot) {
        continue;
      }
      ++refused;
      line << ' ' << point.key << '(';
      if ((bans & core::kBanWater) != 0)
        line << "water ";
      if ((bans & core::kBanForest) != 0)
        line << "forest ";
      if ((bans & core::kBanReserve) != 0)
        line << "reserve ";
      if ((bans & core::kBanRoad) != 0)
        line << "road ";
      if ((bans & core::kBanEdge) != 0)
        line << "edge ";
      if (plot)
        line << "plot ";
      line << ')';
    }
    std::ostringstream out;
    out << "day " << (day + 1U) << " points " << points_.size() << " refused " << refused << " keys"
        << (refused == 0 ? std::string(" none") : line.str());
    lines_.push_back(out.str());
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: HOUSE1907 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  struct Point {
    std::string key;
    float x = 0.0F;
    float y = 0.0F;
  };

  std::uint32_t house_ = core::kNoTableRow;
  std::vector<float> radii_;
  std::vector<Point> points_;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HOUSE_POINT_PROBE_H_
