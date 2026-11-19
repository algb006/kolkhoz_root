/// @file
/// @brief #1907 (econ + core): why the reference house points along the streets build the
///        village slower than the bot's own lattice cells. Every house unit marked in years
///        8-12 is printed (HOUSE1907, one line a house): its place, the days it was marked,
///        started (kDelivering or kBuilding first seen) and stood (kNone at level >= 1), the
///        distance to the nearest log pile and the nearest sawmill standing on its start day
///        (the haul of the logs and the boards), and the distance to the centroid of the houses
///        standing then (the builders' walk - they live in the houses). A year line counts the
///        houses under construction each month and the couples who moved in: their count and
///        the median and worst wait, from since_day to the day the couple left the queue.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_HOUSE_TIMELINE_TALLY_H_
#define TESTS_RUN_COMMON_HOUSE_TIMELINE_TALLY_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/unit_state.h"
#include "core_common/wedding_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class HouseTimelineTally {
 public:
  static constexpr std::uint32_t kFirstYear = 8;
  static constexpr std::uint32_t kLastYear = 12;

  explicit HouseTimelineTally(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t klass = types->FindColumn("class");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        const std::string_view key = types->CellText(row, 0);
        is_house_.push_back(klass != core::kNoTableColumn &&
                            types->CellText(row, klass) == std::string_view("housing"));
        if (key == "log_pile") {
          log_pile_type_ = row;
        } else if (key == "sawmill") {
          sawmill_type_ = row;
        }
      }
    }
  }

  /// @brief One day: houses' phases, the month's sites, the couples who left the queue.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && day != closed_day_) {
      closed_day_ = day;
      CloseYear(year - 1U);
    }
    std::uint32_t building = 0;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (!IsHouse(unit.type.value)) {
        continue;
      }
      const std::uint32_t id = world.units.row_ids[row].value;
      const core::ConstructionPhase phase = unit.construction.phase;
      const bool started = phase == core::ConstructionPhase::kDelivering ||
                           phase == core::ConstructionPhase::kBuilding;
      building += started ? 1U : 0U;
      auto found = houses_.find(id);
      if (found == houses_.end()) {
        if (phase == core::ConstructionPhase::kNone) {
          continue;  // a start house, or one first seen standing
        }
        House house;
        house.position = unit.position;
        house.marked = day;
        found = houses_.emplace(id, house).first;
      }
      House& house = found->second;
      if (started && house.started == kNever) {
        house.started = day;
        house.to_logs_m = NearestOfType(world, log_pile_type_, unit.position);
        house.to_sawmill_m = NearestOfType(world, sawmill_type_, unit.position);
        house.to_homes_m = ToHousesCentroid(world, id, unit.position);
      }
      if (phase == core::ConstructionPhase::kNone && unit.level >= 1 && house.stood == kNever) {
        house.stood = day;
      }
    }
    const std::uint32_t month = (day % core::kDaysPerYear) / core::kDaysPerMonth;
    month_max_[month] = std::max(month_max_[month], building);
    // The couples: a wait row gone from the table moved in (or the couple broke up - the
    // core's queue holds no reason; counted alike, said in the line's name).
    std::map<std::uint64_t, std::uint32_t> now;
    for (const core::WeddingWaitRow& wait : world.wedding_waits.rows) {
      const std::uint64_t key =
          (static_cast<std::uint64_t>(wait.bride.value) << 32U) | wait.groom.value;
      now[key] = wait.since_day;
    }
    for (const auto& [key, since] : waits_) {
      if (!now.contains(key)) {
        left_waits_.push_back(day >= since ? day - since : 0U);
      }
    }
    waits_ = std::move(now);
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [id, house] : houses_) {
      const std::uint32_t year = (house.marked / core::kDaysPerYear) + 1U;
      if (year < kFirstYear || year > kLastYear) {
        continue;
      }
      out << "population_curve: HOUSE1907 seed " << seed << " house " << id << " x "
          << std::lround(house.position.x) << " y " << std::lround(house.position.y) << " marked "
          << house.marked << " started " << Day(house.started) << " stood " << Day(house.stood)
          << " to_log_pile_m " << Metres(house.to_logs_m) << " to_sawmill_m "
          << Metres(house.to_sawmill_m) << " to_homes_centroid_m " << Metres(house.to_homes_m)
          << '\n';
    }
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: HOUSE1907 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  static constexpr core::SimDay kNever = std::numeric_limits<core::SimDay>::max();
  static constexpr std::uint32_t kNoType = std::numeric_limits<std::uint32_t>::max();

  struct House {
    core::Vec2 position;
    core::SimDay marked = 0;
    core::SimDay started = kNever;
    core::SimDay stood = kNever;
    float to_logs_m = -1.0F;
    float to_sawmill_m = -1.0F;
    float to_homes_m = -1.0F;
  };

  [[nodiscard]] bool IsHouse(std::uint32_t type) const {
    return type < is_house_.size() && is_house_[type];
  }

  static std::string Day(core::SimDay day) {
    return day == kNever ? std::string("-") : std::to_string(day);
  }

  static std::string Metres(float metres) {
    return metres < 0.0F ? std::string("none") : std::to_string(std::lround(metres));
  }

  static float Distance(core::Vec2 a, core::Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

  static float NearestOfType(const core::WorldState& world, std::uint32_t type, core::Vec2 at) {
    float best = -1.0F;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value != type || unit.level == 0 ||
          unit.construction.phase != core::ConstructionPhase::kNone) {
        continue;
      }
      const float distance = Distance(unit.position, at);
      best = best < 0.0F ? distance : std::min(best, distance);
    }
    return best;
  }

  [[nodiscard]] float ToHousesCentroid(const core::WorldState& world,
                                       std::uint32_t self,
                                       core::Vec2 at) const {
    double sum_x = 0.0;
    double sum_y = 0.0;
    std::uint32_t count = 0;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (!IsHouse(unit.type.value) || world.units.row_ids[row].value == self || unit.level == 0 ||
          unit.construction.phase != core::ConstructionPhase::kNone) {
        continue;
      }
      sum_x += unit.position.x;
      sum_y += unit.position.y;
      ++count;
    }
    if (count == 0) {
      return -1.0F;
    }
    const core::Vec2 centroid{.x = static_cast<float>(sum_x / count),
                              .y = static_cast<float>(sum_y / count)};
    return Distance(centroid, at);
  }

  void CloseYear(std::uint32_t year) {
    if (year >= kFirstYear && year <= kLastYear) {
      lines_ << "year " << year << " houses_building_month_max";
      for (const std::uint32_t count : month_max_) {
        lines_ << ' ' << count;
      }
      std::vector<std::uint32_t> waits = left_waits_;
      std::ranges::sort(waits);
      lines_ << " couples_left_queue " << waits.size() << " wait_days_median "
             << (waits.empty() ? std::string("-") : std::to_string(waits[waits.size() / 2]))
             << " worst " << (waits.empty() ? std::string("-") : std::to_string(waits.back()))
             << '\n';
    }
    month_max_.fill(0);
    left_waits_.clear();
  }

  std::vector<bool> is_house_;
  std::uint32_t log_pile_type_ = kNoType;
  std::uint32_t sawmill_type_ = kNoType;
  std::map<std::uint32_t, House> houses_;
  std::array<std::uint32_t, core::kMonthsPerYear> month_max_{};
  std::map<std::uint64_t, std::uint32_t> waits_;
  std::vector<std::uint32_t> left_waits_;
  core::SimDay closed_day_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HOUSE_TIMELINE_TALLY_H_
