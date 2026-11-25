/// @file
/// @brief PRESS2164 (econ [10], boss: the base rates before the pair): per village-year, whether
///        the year came under pressure by each half of econ's re-cut gate -
///          lamp: the «план под угрозой» lamp's stand-in (#2190 not built): on some day the plan's
///                turn within 4 days with a position still short;
///          harvest: a plan position's harvest at least 15 % (and, apart, 25 %) below its
///                expected yield - the cheapest expectation: the table's yield (crops.csv
///                yield_kg_per_ha) × the hectares whose first rotation slot grows it at the year's
///                start.
///        Test-side: the world is unchanged.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_PRESSURE_TALLY_H_
#define TESTS_RUN_COMMON_PRESSURE_TALLY_H_

#include <cstdint>
#include <map>
#include <ostream>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class PressureTally {
 public:
  explicit PressureTally(const core::ITableSet& tables) {
    const core::ITable* const crops = tables.FindTable("crops");
    const core::ITable* const resources = tables.FindTable("resources");
    if (crops == nullptr || resources == nullptr) {
      return;
    }
    milk_ = resources->FindRowByKey("milk");
    const std::uint32_t resource_col = crops->FindColumn("resource");
    const std::uint32_t yield_col = crops->FindColumn("yield_kg_per_ha");
    if (resource_col == core::kNoTableColumn || yield_col == core::kNoTableColumn) {
      return;
    }
    crop_resource_.assign(crops->RowCount(), core::kNoTableRow);
    crop_yield_kg_.assign(crops->RowCount(), 0.0F);
    for (std::uint32_t row = 0; row < crops->RowCount(); ++row) {
      crop_resource_[row] = resources->FindRowByKey(crops->CellText(row, resource_col));
      crop_yield_kg_[row] = crops->CellReal(row, yield_col).value_or(0.0F);
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    Year& row = by_year_[year];
    // The year's expectation, once, on its first look.
    if (!row.expected_taken) {
      row.expected_taken = true;
      for (const core::FieldRow& field : world.fields.rows) {
        const std::uint32_t crop = field.rotation_year0.value;
        if (field.kind != core::LandKind::kArable || crop >= crop_resource_.size() ||
            crop_resource_[crop] == core::kNoTableRow) {
          continue;
        }
        row.expected_kg[crop_resource_[crop]] += field.area_ga * crop_yield_kg_[crop];
      }
    }
    // The lamp's stand-in.
    if (world.plan.announced != 0 && core::DaysToPlanTurn(world) <= kDeadlineDays) {
      for (std::size_t at = 0; at < world.plan.due.size(); ++at) {
        const core::Grams sent = at < world.plan.delivered.size() ? world.plan.delivered[at] : 0;
        const core::Grams owed = world.plan.due[at] > sent ? world.plan.due[at] - sent : 0;
        row.lamp = row.lamp || owed > 0;
        // THE REAL THREAT (pr8: «owed near the turn» is 100 % of years - the turn ships from the
        // store, so every position is owed until it): what is owed exceeds what lies stored.
        core::Grams lying = 0;
        for (const core::UnitRow& unit : world.units.rows) {
          lying += at < unit.stock.size() ? unit.stock[at] : 0;
        }
        // The milk is carted daily (the milk cart's share) and never lies stored for the turn: pr9
        // read owed > stock in 99.7 % of years with it in.
        const bool daily = static_cast<std::uint32_t>(at) == milk_;
        if (owed > lying && !daily) {
          row.lamp_uncovered = true;
          row.uncovered_positions[static_cast<std::uint32_t>(at)] = true;
        }
      }
    }
    // The plan's positions, read while they stand (the closed book has no due).
    for (std::size_t at = 0; at < world.plan.due.size(); ++at) {
      if (world.plan.due[at] > 0) {
        row.positions[static_cast<std::uint32_t>(at)] = true;
      }
    }
    if (day % core::kDaysPerYear == 0 && day > 0) {
      Year& closed = by_year_[year - 1U];
      for (const auto& [resource, position] : closed.positions) {
        const auto expected = closed.expected_kg.find(resource);
        if (expected == closed.expected_kg.end() || !(expected->second > 0.0F)) {
          continue;
        }
        const double harvest =
            resource < world.ledger.closed.harvest.size()
                ? static_cast<double>(world.ledger.closed.harvest[resource]) / 1000.0
                : 0.0;
        const double share = harvest / static_cast<double>(expected->second);
        closed.short15 = closed.short15 || share < 0.85;
        closed.short25 = closed.short25 || share < 0.75;
      }
      closed.closed = true;
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [year, row] : by_year_) {
      if (!row.closed) {
        continue;
      }
      out << "population_curve: PRESS2164 seed " << seed << " year " << year
          << " ground=lamp=turn_within_4_days_with_a_position_short(stand-in);harvest=closed_"
             "book_vs_table_yield_x_slot0_ha"
          << " lamp " << row.lamp << " lamp_uncovered " << row.lamp_uncovered << " short15 "
          << row.short15 << " short25 " << row.short25 << " uncovered_resources ";
      for (const auto& [resource, flag] : row.uncovered_positions) {
        out << resource << ';';
      }
      out << '\n';
    }
  }

 private:
  static constexpr std::uint32_t kDeadlineDays = 4;

  struct Year {
    bool expected_taken = false;
    std::map<std::uint32_t, float> expected_kg;
    std::map<std::uint32_t, bool> positions;
    bool lamp = false;
    bool lamp_uncovered = false;
    std::map<std::uint32_t, bool> uncovered_positions;
    bool short15 = false;
    bool short25 = false;
    bool closed = false;
  };

  std::uint32_t milk_ = core::kNoTableRow;
  std::vector<std::uint32_t> crop_resource_;
  std::vector<float> crop_yield_kg_;
  std::map<std::uint32_t, Year> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_PRESSURE_TALLY_H_
