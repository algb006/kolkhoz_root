/// @file
/// @brief LEVEL1907 (econ's ask on ha1907d, not for release): at each Epoch I year's last day,
///        the kolkhoz units standing BELOW the level the Epoch II door requires
///        (era_readiness.h RequiredUnitLevel, the door's own rule and catalogue) - type, level,
///        required, position. Printed only for a year with at least one; «none» years are
///        counted.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_LEVEL_BLOCK_PROBE_H_
#define TESTS_RUN_COMMON_LEVEL_BLOCK_PROBE_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"

namespace run {

class LevelBlockProbe {
 public:
  explicit LevelBlockProbe(const core::ITableSet& tables)
      : catalog_(core::ReadReadinessCatalog(tables, core::Epoch::kOne)) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t key = types->FindColumn("key");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        names_.emplace_back(key != core::kNoTableColumn ? types->CellText(row, key) : "?");
      }
    }
  }

  /// @brief Once a day after the step: on an Epoch I year's last day, the units below level.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day = world.calendar.day;
    if ((day + 1U) % core::kDaysPerYear != 0 || world.epoch != core::Epoch::kOne) {
      return;
    }
    std::ostringstream line;
    std::uint32_t below = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level == 0 || unit.dead != 0) {
        continue;
      }
      const bool kolkhoz = std::ranges::any_of(
          catalog_.kolkhoz_types,
          [&unit](core::UnitTypeId type) { return type.value == unit.type.value; });
      const std::uint8_t required =
          core::RequiredUnitLevel(catalog_, unit.type, world.epoch, world);
      if (!kolkhoz || unit.level >= required) {
        continue;
      }
      ++below;
      line << ' ' << (unit.type.value < names_.size() ? names_[unit.type.value] : "?") << ':'
           << static_cast<int>(unit.level) << '/' << static_cast<int>(required)
           << (unit.construction.phase != core::ConstructionPhase::kNone ? "(site)" : "") << '@'
           << std::lround(unit.position.x) << ',' << std::lround(unit.position.y);
    }
    if (below == 0) {
      ++clear_years_;
      return;
    }
    std::ostringstream out;
    out << "year " << (day / core::kDaysPerYear) + 1U << " below " << below << ':' << line.str();
    lines_.push_back(out.str());
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: LEVEL1907 seed " << seed << ' ' << line << '\n';
    }
    out << "population_curve: LEVEL1907 seed " << seed << " clear_years " << clear_years_
        << " blocked_years " << lines_.size() << '\n';
  }

 private:
  core::ReadinessCatalog catalog_;
  std::vector<std::string> names_;
  std::vector<std::string> lines_;
  std::uint32_t clear_years_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_LEVEL_BLOCK_PROBE_H_
