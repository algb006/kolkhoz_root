/// @file
/// @brief What the building sites waited for, counted day by day: the logs and
/// boards short on sites that were delivering, and the adults with no work on
/// the days a site stood.
/// @threading SINGLE_THREADED
/// Test-side instrument, read from the thread that owns the simulation.
///
/// WHY. Boss asked the thirty-year measure of the house contract to name the
/// timber chain in numbers (parcel 281): how much the sites waited for, what
/// the felling, sawing and carting cost, and whether hands stood idle beside a
/// site that stood. The ledger has the work days by kind; this counts the
/// waiting, which no ledger column holds.

#ifndef TESTS_RUN_COMMON_TIMBER_CHAIN_TALLY_H_
#define TESTS_RUN_COMMON_TIMBER_CHAIN_TALLY_H_

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class TimberChainTally {
 public:
  explicit TimberChainTally(const core::ITableSet& tables) {
    const core::ITable* const resources = tables.FindTable("resources");
    const core::ITable* const types = tables.FindTable("unit_types");
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    if (resources == nullptr || types == nullptr || costs == nullptr) {
      return;
    }
    log_row_ = resources->FindRowByKey("log");
    board_row_ = resources->FindRowByKey("board");
    const std::uint32_t mass_col = resources->FindColumn("kg_per_unit");
    const std::optional<float> log_kg = resources->CellReal(log_row_, mass_col);
    const std::optional<float> board_kg = resources->CellReal(board_row_, mass_col);
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      const std::uint32_t resource = resources->FindRowByKey(costs->CellText(row, resource_col));
      if (resource != log_row_ && resource != board_row_) {
        continue;
      }
      const std::optional<float> amount = costs->CellReal(row, amount_col);
      const std::optional<float> level = costs->CellReal(row, level_col);
      const std::optional<float> kg = resource == log_row_ ? log_kg : board_kg;
      const std::uint32_t type = types->FindRowByKey(costs->CellText(row, unit_col));
      if (!amount.has_value() || !level.has_value() || !kg.has_value() ||
          type == core::kNoTableRow) {
        continue;
      }
      rows_.push_back(Cost{.type = type,
                           .level = static_cast<std::uint32_t>(*level),
                           .log = resource == log_row_,
                           .kilograms = static_cast<double>(*amount) * static_cast<double>(*kg)});
    }
  }

  /// @brief One day's count, from the state at the day's end.
  void CountDay(const core::WorldState& world) {
    bool a_site_waits = false;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.construction.phase != core::ConstructionPhase::kDelivering &&
          unit.construction.phase != core::ConstructionPhase::kMarked) {
        continue;
      }
      const std::uint32_t target = unit.construction.target_level;
      for (const Cost& cost : rows_) {
        if (cost.type != unit.type.value || cost.level != target) {
          continue;
        }
        const std::uint32_t resource = cost.log ? log_row_ : board_row_;
        const double there =
            resource < unit.stock.size() ? static_cast<double>(unit.stock[resource]) / 1000.0 : 0.0;
        const double short_kg = cost.kilograms - there;
        if (short_kg > 0.0) {
          (cost.log ? log_tonne_days_ : board_tonne_days_) += short_kg / 1000.0;
          a_site_waits = true;
        }
      }
    }
    if (a_site_waits) {
      ++days_a_site_waited_;
    }
  }

  void Report(std::string_view run_name, std::uint32_t years) const {
    std::cout << run_name << ": TIMBER CHAIN over " << years << " years — sites waited for "
              << std::lround(log_tonne_days_) << " t-days of logs and "
              << std::lround(board_tonne_days_) << " t-days of boards; a site waited for one or "
              << "the other on " << days_a_site_waited_ << " days of " << years * core::kDaysPerYear
              << "\n";
  }

 private:
  struct Cost {
    std::uint32_t type = 0;
    std::uint32_t level = 0;
    bool log = false;
    double kilograms = 0.0;
  };

  std::vector<Cost> rows_;

  std::uint32_t log_row_ = core::kNoTableRow;

  std::uint32_t board_row_ = core::kNoTableRow;

  double log_tonne_days_ = 0.0;

  double board_tonne_days_ = 0.0;

  std::uint32_t days_a_site_waited_ = 0;
};

/// @brief Who left the kolkhoz and why, and who was born, year by year — read
/// from each step's events (boss, parcel 287: to tell how much growth the
/// building chain eats and how much births and departures do).
class DepartureTally {
 public:
  void CountStep(const core::WorldState& world) {
    for (const core::SimEvent& event : world.step_events) {
      switch (event.kind) {
        case core::EventKind::kResidentBorn:
          ++year_.born;
          break;
        case core::EventKind::kResidentLeft:
          ++year_.left;
          break;
        case core::EventKind::kFamilyLeftForNoHouse:
          year_.left_for_no_house += static_cast<std::uint32_t>(event.amount);
          break;
        case core::EventKind::kWeddingAwaitsHouse:
          ++year_.couples_began_waiting;
          break;
        case core::EventKind::kResidentArrived:
          ++year_.arrived;  // a stranger comes only to a free house
          break;
        default:
          break;
      }
    }
  }

  /// @brief Closes the year's row. Call once at each year's end.
  void CloseYear() {
    years_.push_back(year_);
    year_ = Year{};
  }

  void Report(std::string_view run_name) const {
    std::uint32_t index = 0;
    for (const Year& year : years_) {
      ++index;
      std::cout << run_name << ": PEOPLE year " << index << " — born " << year.born << ", left "
                << year.left << " (for want of a house " << year.left_for_no_house << ", other "
                << (year.left - year.left_for_no_house) << "), couples began waiting "
                << year.couples_began_waiting << ", strangers arrived " << year.arrived << "\n";
    }
  }

 private:
  struct Year {
    std::uint32_t born = 0;
    std::uint32_t left = 0;
    std::uint32_t left_for_no_house = 0;
    std::uint32_t couples_began_waiting = 0;
    std::uint32_t arrived = 0;
  };

  Year year_;

  std::vector<Year> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TIMBER_CHAIN_TALLY_H_
