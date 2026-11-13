/// @file
/// @brief #1677 (econ #1658, houses-first-while-couples-wait-2026-11-13 §2 (a)): the run's
///        chairman raises every started house site in the queue while a couple waits for a
///        house, and lowers them when the queue is empty - the existing «Поднять в очереди»
///        (kRaiseSite, #595), so the house sites take the hands and the carts first and the
///        production sites take what is left. Not a core default; off unless asked
///        (--houses-first).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// THE PRINT (HOUSES1677, a village-year, base and arm alike): couple-days waiting for a house
/// (H1), residents (H2), the raise and lower orders given, and every production site's days from
/// start to standing by kind (H4: count, median, worst) - a production site is any non-housing
/// unit seen started (kDelivering or kBuilding), counted the day it stands.

#ifndef TESTS_RUN_COMMON_HOUSES_FIRST_POLICY_H_
#define TESTS_RUN_COMMON_HOUSES_FIRST_POLICY_H_

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class HousesFirstPolicy {
 public:
  static constexpr std::uint32_t kYears = 16;

  explicit HousesFirstPolicy(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t klass = types->FindColumn("class");
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        is_house_.push_back(klass != core::kNoTableColumn &&
                            types->CellText(row, klass) == std::string_view("housing"));
        type_keys_.emplace_back(types->CellText(row, 0));
      }
    }
  }

  void Enable(bool on) { on_ = on; }

  [[nodiscard]] bool On() const { return on_; }

  /// @brief The fixture difference, BEFORE the run measures.
  void Declare(std::string_view run_name) const {
    if (on_) {
      std::cout << run_name
                << ": ARM — --houses-first (#1677): while a couple waits for a house the run's "
                   "chairman raises every started house site in the queue (kRaiseSite), and "
                   "lowers them when the queue is empty\n";
    }
  }

  /// @brief One day: the year's line at the turn, the tallies, and the raise / lower orders.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && year - 1U <= kYears && day != closed_day_) {
      closed_day_ = day;
      CloseYear(world, year - 1U);
    }
    wait_days_ += static_cast<std::uint32_t>(world.wedding_waits.rows.size());
    CountSites(world);
    if (!on_) {
      return;
    }
    const bool waiting = !world.wedding_waits.rows.empty();
    std::vector<core::OrderRow> orders;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const bool started = unit.construction.phase == core::ConstructionPhase::kDelivering ||
                           unit.construction.phase == core::ConstructionPhase::kBuilding;
      if (!started || !IsHouse(unit.type.value)) {
        continue;
      }
      const bool raised = unit.construction.raised != 0;
      const std::uint32_t id = world.units.row_ids[row].value;
      const bool mine = std::ranges::find(mine_, id) != mine_.end();
      // Raise what waits; lower only what this rule raised - the canon's own
      // year-1 raise (house_policy.h, #595) stays the canon's.
      if (waiting && !raised) {
        mine_.push_back(id);
      } else if (!waiting && raised && mine) {
        std::erase(mine_, id);
      } else {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kRaiseSite;
      order.unit = world.units.row_ids[row];
      order.enable = waiting ? 1 : 0;
      orders.push_back(order);
      ++(waiting ? raised_ : lowered_);
    }
    if (!orders.empty()) {
      simulation.StageOrders(orders, {});
    }
  }

  /// @brief The village's lines.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: HOUSES1677 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  [[nodiscard]] bool IsHouse(std::uint32_t type) const {
    return type < is_house_.size() && is_house_[type];
  }

  /// Production sites: the day each is first seen started, and its days to standing.
  void CountSites(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (IsHouse(unit.type.value)) {
        continue;
      }
      const std::uint32_t id = world.units.row_ids[row].value;
      const bool started = unit.construction.phase == core::ConstructionPhase::kDelivering ||
                           unit.construction.phase == core::ConstructionPhase::kBuilding;
      const auto open = started_on_.find(id);
      if (started && open == started_on_.end()) {
        started_on_[id] = day;
      } else if (!started && open != started_on_.end()) {
        if (unit.construction.phase == core::ConstructionPhase::kNone && unit.level > 0) {
          days_by_type_[unit.type.value].push_back(day - open->second);
        }
        started_on_.erase(open);
      }
    }
  }

  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    lines_ << "year " << year << " arm " << (on_ ? 1 : 0) << " wedding_wait_days " << wait_days_
           << " residents " << world.residents.rows.size() << " raised " << raised_ << " lowered "
           << lowered_ << " sites";
    bool any = false;
    for (auto& [type, days] : days_by_type_) {
      if (days.empty()) {
        continue;
      }
      std::ranges::sort(days);
      lines_ << ' ' << (type < type_keys_.size() ? type_keys_[type] : std::to_string(type)) << ':'
             << days.size() << ':' << days[days.size() / 2] << ':' << days.back();
      any = true;
      days.clear();
    }
    lines_ << (any ? "" : " -") << '\n';
    wait_days_ = 0;
    raised_ = 0;
    lowered_ = 0;
  }

  bool on_ = false;
  std::vector<bool> is_house_;
  std::vector<std::string> type_keys_;
  std::vector<std::uint32_t> mine_;  ///< house sites this rule raised
  std::map<std::uint32_t, core::SimDay> started_on_;
  std::map<std::uint32_t, std::vector<core::SimDay>> days_by_type_;
  std::uint32_t wait_days_ = 0;
  std::uint32_t raised_ = 0;
  std::uint32_t lowered_ = 0;
  core::SimDay closed_day_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HOUSES_FIRST_POLICY_H_
