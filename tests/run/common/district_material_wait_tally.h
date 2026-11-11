/// @file
/// @brief #1441 T0 (boss, 11 Nov 2026; econ's dealer-grain-lot-path-2026-11-07):
/// the building-site days spent waiting for a DISTRICT material - steel, tools,
/// glass, roofing - a village-year at a time, split by material, with the count
/// of waiting events beside the days and the year's worst single wait.
/// @threading SINGLE_THREADED
/// Test-side code, read once a day from the thread that owns the simulation.
///
/// A SITE-DAY WAITS ON A MATERIAL when the site is marked or delivering (not
/// built, not building, not paused) and ISimulation::MaterialsShortFor names
/// that material short - the same door BrakesTally reads. A site short of two
/// district materials waits on both that day: the split by material can sum
/// above the site-days. A WAITING EVENT is a run of such days of one site on
/// one material; a run that crosses a year's turn counts in both years, its
/// days in the year they fell. THE WORST WAIT is the year's longest run, by its
/// days inside the year («open» when it was still running at the year's close).
/// A village-year with no wait prints 0 waits and its site count, not a blank.

#ifndef TESTS_RUN_COMMON_DISTRICT_MATERIAL_WAIT_TALLY_H_
#define TESTS_RUN_COMMON_DISTRICT_MATERIAL_WAIT_TALLY_H_

#include <array>
#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/material_shortfall.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class DistrictMaterialWaitTally {
 public:
  static constexpr std::uint32_t kYears = 16;
  static constexpr std::size_t kMaterials = 4;

  DistrictMaterialWaitTally(const core::ITableSet& tables, std::uint64_t seed) : seed_(seed) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      for (std::size_t index = 0; index < kMaterials; ++index) {
        material_rows_[index] = resources->FindRowByKey(kMaterialKeys[index]);
      }
    }
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t key = types->FindColumn("key");
      for (std::uint32_t row = 0; key != core::kNoTableColumn && row < types->RowCount(); ++row) {
        type_keys_.emplace_back(types->CellText(row, key));
      }
    }
  }

  /// @brief One closed day: every site's district-material waits; on the
  ///        year's last day, the year's line.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (year > kYears) {
      return;
    }
    std::set<std::pair<std::uint32_t, std::size_t>> waiting_today;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const core::ConstructionPhase phase = unit.construction.phase;
      if (phase == core::ConstructionPhase::kNone ||
          phase == core::ConstructionPhase::kDemolishing) {
        continue;
      }
      const core::UnitId id = world.units.row_ids[row];
      sites_.insert(id.value);
      if (unit.paused != 0 || phase == core::ConstructionPhase::kBuilding) {
        continue;
      }
      for (const core::MaterialShortfall& line : simulation.MaterialsShortFor(id)) {
        for (std::size_t material = 0; material < kMaterials; ++material) {
          if (line.resource.value == material_rows_[material]) {
            waiting_today.emplace(id.value, material);
            if (type_of_.find(id.value) == type_of_.end()) {
              type_of_[id.value] = unit.type.value;
            }
          }
        }
      }
    }
    // The runs: a wait today extends yesterday's run of the same site and
    // material, or opens one; a run not waiting today closes.
    for (auto run = open_.begin(); run != open_.end();) {
      if (waiting_today.find(run->first) == waiting_today.end()) {
        Close(run->first, run->second, false);
        run = open_.erase(run);
      } else {
        ++run;
      }
    }
    for (const auto& key : waiting_today) {
      // A run with no day yet THIS year (new today, or carried over the turn)
      // is an event of this year from its first day in it.
      const auto run = open_.try_emplace(key, 0U).first;
      if (run->second == 0) {
        ++events_[key.second];
      }
      ++run->second;
      ++days_[key.second];
    }
    if (world.calendar.day % core::kDaysPerYear == core::kDaysPerYear - 1U) {
      CloseYear(year);
    }
  }

  /// @brief The village's year lines.
  void Print(std::ostream& out) const { out << lines_.str(); }

 private:
  static constexpr std::array<const char*, kMaterials> kMaterialKeys = {
      "steel", "tool", "glass", "roofing"};

  void Close(const std::pair<std::uint32_t, std::size_t>& key, std::uint32_t days, bool open) {
    if (days > worst_days_) {
      worst_days_ = days;
      worst_site_ = key.first;
      worst_material_ = key.second;
      worst_open_ = open;
    }
  }

  void CloseYear(std::uint32_t year) {
    // Runs still going at the close: their days inside this year, «open»; they
    // count again as an event of next year, from its first day.
    for (const auto& [key, days] : open_) {
      Close(key, days, true);
    }
    std::uint32_t events = 0;
    std::uint32_t days = 0;
    for (std::size_t material = 0; material < kMaterials; ++material) {
      events += events_[material];
      days += days_[material];
    }
    lines_ << "population_curve: DMWAIT1441 seed " << seed_ << " year " << year << " sites "
           << sites_.size() << " waits " << events << " site_material_days " << days;
    for (std::size_t material = 0; material < kMaterials; ++material) {
      lines_ << ' ' << kMaterialKeys[material] << ":days=" << days_[material]
             << ",waits=" << events_[material];
    }
    if (worst_days_ == 0) {
      lines_ << " worst none";
    } else {
      const auto type = type_of_.find(worst_site_);
      const std::string type_key = type != type_of_.end() && type->second < type_keys_.size()
                                       ? type_keys_[type->second]
                                       : std::string("unit");
      lines_ << " worst " << type_key << '#' << worst_site_ << ':' << kMaterialKeys[worst_material_]
             << ':' << worst_days_ << (worst_open_ ? ":open" : "");
    }
    for (std::size_t material = 0; material < kMaterials; ++material) {
      if (material_rows_[material] == core::kNoTableRow) {
        lines_ << " " << kMaterialKeys[material] << "_NOT_IN_THE_TABLES";
      }
    }
    lines_ << '\n';
    events_ = {};
    days_ = {};
    sites_.clear();
    worst_days_ = 0;
    worst_open_ = false;
    // Runs open over the turn start the new year with no day in it: an event
    // of the new year only if they wait on its first day too.
    for (auto& entry : open_) {
      entry.second = 0;
    }
  }

  std::uint64_t seed_ = 0;
  std::array<std::uint32_t, kMaterials> material_rows_ = {
      core::kNoTableRow, core::kNoTableRow, core::kNoTableRow, core::kNoTableRow};
  std::vector<std::string> type_keys_;
  std::map<std::uint32_t, std::uint32_t> type_of_;                       ///< site id -> unit type
  std::map<std::pair<std::uint32_t, std::size_t>, std::uint32_t> open_;  ///< run -> days
  std::array<std::uint32_t, kMaterials> events_ = {};
  std::array<std::uint32_t, kMaterials> days_ = {};
  std::set<std::uint32_t> sites_;  ///< every site under works this year
  std::uint32_t worst_days_ = 0;
  std::uint32_t worst_site_ = 0;
  std::size_t worst_material_ = 0;
  bool worst_open_ = false;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DISTRICT_MATERIAL_WAIT_TALLY_H_
