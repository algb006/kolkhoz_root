/// @file
/// @brief COMMUTE2261 (econ's seam census for boss's map, not for release): the commute's share
///        of a worker's day - 2 × his assignment's one-way travel_hours ÷ the day's daylight -
///        by where he works (the unit's type for unit work, a site and herd care; the work kind
///        for the field, felling, hauling, extraction, roads, planting), median and p90, Dec-Feb
///        against Jun-Aug, over the whole run. One sample a resident-day with work and a
///        priced travel (travel_hours ≥ 0).
///        WHAT IT IS NOT: the hours of a ride (a horse) are the assignment's own; a share over 1
///        is printed as is (a walk longer than the light).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_COMMUTE_CENSUS_H_
#define TESTS_RUN_COMMON_COMMUTE_CENSUS_H_

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class CommuteCensus {
 public:
  explicit CommuteCensus(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t key = types->FindColumn("key");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        names_.emplace_back(key != core::kNoTableColumn ? types->CellText(row, key) : "?");
      }
    }
  }

  /// @brief Once a day after the step (the day's assignments and daylight stand).
  void CountDay(const core::WorldState& world) {
    const auto month = world.calendar.date.month;
    const bool winter = month == core::Month::kDecember || month == core::Month::kJanuary ||
                        month == core::Month::kFebruary;
    const bool summer =
        month == core::Month::kJune || month == core::Month::kJuly || month == core::Month::kAugust;
    const float light = world.weather.daylight_hours;
    if ((!winter && !summer) || !(light > 0.0F)) {
      return;
    }
    for (const core::ResidentRow& resident : world.residents.rows) {
      const core::WorkAssignment& work = resident.work;
      if (work.kind == core::WorkKind::kNone || work.kind >= core::WorkKind::kWorkKindCount ||
          !(work.travel_hours >= 0.0F)) {
        continue;
      }
      (winter ? winter_ : summer_)[KeyOf(world, work)].push_back(2.0F * work.travel_hours / light);
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    std::map<std::string, bool> keys;
    for (const auto& [key, v] : winter_) {
      keys[key] = true;
    }
    for (const auto& [key, v] : summer_) {
      keys[key] = true;
    }
    for (const auto& [key, unused] : keys) {
      out << "population_curve: COMMUTE2261 seed " << seed << ' ' << key << " winter "
          << Quantiles(winter_, key) << " summer " << Quantiles(summer_, key) << '\n';
    }
  }

 private:
  std::string KeyOf(const core::WorldState& world, const core::WorkAssignment& work) const {
    switch (work.kind) {
      case core::WorkKind::kUnitWork:
      case core::WorkKind::kConstruction: {
        const std::uint32_t row = core::FindRow(world.units, work.unit);
        const std::uint32_t type =
            row == core::kNoRow ? 0xFFFFFFFFU : world.units.rows[row].type.value;
        return (work.kind == core::WorkKind::kConstruction ? "site:" : "unit:") +
               (type < names_.size() ? names_[type] : std::string("?"));
      }
      case core::WorkKind::kHerdCare:
        return "herd_care";
      case core::WorkKind::kPlowing:
      case core::WorkKind::kHarrowing:
      case core::WorkKind::kSowing:
      case core::WorkKind::kHarvest:
        return "field";
      case core::WorkKind::kFelling:
        return "felling";
      case core::WorkKind::kHauling:
        return "hauling";
      case core::WorkKind::kExtraction:
        return "extraction";
      case core::WorkKind::kPlanting:
        return "planting";
      case core::WorkKind::kRoadWork:
        return "road_work";
      default:
        return "other";
    }
  }

  static std::string Quantiles(const std::map<std::string, std::vector<float>>& map,
                               const std::string& key) {
    const auto found = map.find(key);
    if (found == map.end() || found->second.empty()) {
      return "none";
    }
    std::vector<float> v = found->second;
    std::sort(v.begin(), v.end());
    std::ostringstream s;
    s << std::fixed << std::setprecision(2) << "median " << v[v.size() / 2] << " p90 "
      << v[std::min(v.size() - 1, (v.size() * 9) / 10)] << " n " << v.size();
    return s.str();
  }

  std::vector<std::string> names_;
  std::map<std::string, std::vector<float>> winter_;
  std::map<std::string, std::vector<float>> summer_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_COMMUTE_CENSUS_H_
