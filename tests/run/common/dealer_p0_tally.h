/// @file
/// @brief #1441's Dealer theft-view candidate, econ's P0 (econ-boss-1441-t0-read
/// [1]-[2], boss's order of 12 Nov 2026): a read-only print a village.
/// @threading SINGLE_THREADED
/// Test-side code, read once a day from the thread that owns the simulation.
///
///   * P0a: the first day a GUARDABLE store stands (a unit type with a night
///     post in unit_staff, as food_theft.cpp's R2 reads it; level > 0, no works
///     in progress) - «start» when one stands from day 0;
///   * P0b: the plan's rye a year (y1, y2): due, the first day anything of it
///     was delivered, the day delivered reached due (or «never»), and the days
///     the delivered rye rose - set against the district's take, which collects
///     the standing request on a month's LAST day (PlanState::requested) and
///     ships the rest at the turn. The early-delivery order EXISTS: OrderKind::
///     kDeliverPlan (order_state.h) - printed as a fact of the core, once;
///   * P0c: what a chairman other than the Dealer sees of night theft is a fact
///     of the core, not of the village: it goes in the reply, not this stream.

#ifndef TESTS_RUN_COMMON_DEALER_P0_TALLY_H_
#define TESTS_RUN_COMMON_DEALER_P0_TALLY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class DealerP0Tally {
 public:
  DealerP0Tally(const core::ITableSet& tables, std::uint64_t seed) : seed_(seed) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      rye_ = resources->FindRowByKey("rye");
    }
    const core::ITable* const types = tables.FindTable("unit_types");
    const core::ITable* const staff = tables.FindTable("unit_staff");
    const core::ITable* const professions = tables.FindTable("professions");
    if (types == nullptr || staff == nullptr || professions == nullptr) {
      return;
    }
    guardable_.assign(types->RowCount(), 0);
    const std::uint32_t key = types->FindColumn("key");
    for (std::uint32_t row = 0; key != core::kNoTableColumn && row < types->RowCount(); ++row) {
      type_keys_.emplace_back(types->CellText(row, key));
    }
    const std::uint32_t unit_col = staff->FindColumn("unit");
    const std::uint32_t profession_col = staff->FindColumn("profession");
    const std::uint32_t shift_col = professions->FindColumn("shift");
    if (unit_col == core::kNoTableColumn || profession_col == core::kNoTableColumn ||
        shift_col == core::kNoTableColumn) {
      return;
    }
    for (std::uint32_t row = 0; row < staff->RowCount(); ++row) {
      const std::uint32_t profession =
          professions->FindRowByKey(staff->CellText(row, profession_col));
      const std::uint32_t type = types->FindRowByKey(staff->CellText(row, unit_col));
      if (profession != core::kNoTableRow && type != core::kNoTableRow &&
          professions->CellText(profession, shift_col) == "night") {
        guardable_[type] = 1;
      }
    }
  }

  void CountDayEnd(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (!guardable_day_) {
      for (const core::UnitRow& unit : world.units.rows) {
        if (unit.type.value < guardable_.size() && guardable_[unit.type.value] != 0 &&
            unit.level > 0 && unit.construction.phase == core::ConstructionPhase::kNone) {
          guardable_day_ = day;
          guardable_type_ = unit.type.value;
          break;
        }
      }
    }
    if (year > kPlanYears || rye_ == core::kNoTableRow) {
      return;
    }
    Rye& rye = years_[year - 1U];
    const core::Grams due = rye_ < world.plan.due.size() ? world.plan.due[rye_] : 0;
    const core::Grams sent = rye_ < world.plan.delivered.size() ? world.plan.delivered[rye_] : 0;
    rye.due = std::max(rye.due, due);
    if (sent > rye.last_sent) {
      if (!rye.first_day) {
        rye.first_day = day;
      }
      rye.rise_days +=
          (rye.rise_days.empty() ? "" : ",") + std::to_string(day % core::kDaysPerYear);
      if (due > 0 && sent >= due && !rye.met_day) {
        rye.met_day = day;
      }
    }
    rye.last_sent = sent;
  }

  void Print(std::ostream& out) const {
    out << "population_curve: DEALERP0 seed " << seed_ << " guardable_store_first_day "
        << (guardable_day_
                ? (*guardable_day_ == 0 ? std::string("start") : std::to_string(*guardable_day_))
                : std::string("never"))
        << " type "
        << (guardable_day_ && guardable_type_ < type_keys_.size() ? type_keys_[guardable_type_]
                                                                  : std::string("-"));
    for (std::uint32_t year = 0; year < kPlanYears; ++year) {
      const Rye& rye = years_[year];
      // Days of the year (0..47); the district's take is a month's last day
      // (3, 7, ..., 47) and the turn ships the rest.
      out << " y" << year + 1 << "_rye_due_kg " << rye.due / 1000 << " first_delivered_day "
          << (rye.first_day ? std::to_string(*rye.first_day % core::kDaysPerYear) : "never")
          << " met_day "
          << (rye.met_day ? std::to_string(*rye.met_day % core::kDaysPerYear) : "never")
          << " rise_days " << (rye.rise_days.empty() ? std::string("-") : rye.rise_days);
    }
    out << '\n';
  }

 private:
  static constexpr std::uint32_t kPlanYears = 2;

  struct Rye {
    core::Grams due = 0;
    core::Grams last_sent = 0;
    std::optional<core::SimDay> first_day;
    std::optional<core::SimDay> met_day;
    std::string rise_days;
  };

  std::uint64_t seed_ = 0;
  std::uint32_t rye_ = core::kNoTableRow;
  std::vector<std::uint8_t> guardable_;
  std::vector<std::string> type_keys_;
  std::optional<core::SimDay> guardable_day_;
  std::uint32_t guardable_type_ = 0;
  std::array<Rye, kPlanYears> years_{};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DEALER_P0_TALLY_H_
