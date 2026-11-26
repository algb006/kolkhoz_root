/// @file
/// @brief LAMP2190 (#2190's gates; econ's theft-grain-store-consolidated §7 and his (4)): the
///        «план под угрозой: хищения и порча» lamp (kPlanPositionThreatened) by year - the
///        village-months it is lit (budget < 10 %), how many of them belong to a position that
///        then missed, the positions missed and how many of them it lit a game month or more
///        ahead (recall ≥ 90 %), and whether the year closed under pressure (#2164).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// GROUND, printed: lit = the simulation's CollectAlarms after each day; a miss = a position
/// whose closed book delivered below campaign.csv's plan_met_share of its due; «early» = lit
/// on a day with DaysToPlanTurn ≥ kDaysPerMonth; under pressure = ChairmanState::
/// year_under_pressure on the year's last day.

#ifndef TESTS_RUN_COMMON_PLAN_THREAT_TALLY_H_
#define TESTS_RUN_COMMON_PLAN_THREAT_TALLY_H_

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class PlanThreatTally {
 public:
  explicit PlanThreatTally(const core::ITableSet& tables) {
    if (const core::ITable* const campaign = tables.FindTable("campaign")) {
      const std::uint32_t row = campaign->FindRowByKey("plan_met_share");
      const std::uint32_t column = campaign->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        if (const std::optional<float> share = campaign->CellReal(row, column)) {
          met_share_ = *share;
        }
      }
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day = world.calendar.day;
    if (day > 0 && day % core::kDaysPerYear == 0) {
      CloseYear(world, (day / core::kDaysPerYear) - 1U);
    }
    std::vector<core::Alarm> alarms;
    simulation.CollectAlarms(alarms);
    const std::uint32_t month = (day % core::kDaysPerYear) / core::kDaysPerMonth;
    const bool early = core::DaysToPlanTurn(world) >= core::kDaysPerMonth;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind != core::AlarmKind::kPlanPositionThreatened) {
        continue;
      }
      const std::size_t at = alarm.resource.value;
      if (lit_.size() <= at) {
        lit_.resize(at + 1U, Lit{});
      }
      lit_[at].months[month] = 1;
      lit_[at].early = lit_[at].early || early;
    }
    if (day % core::kDaysPerYear == core::kDaysPerYear - 1U) {
      under_pressure_ = world.chairman.year_under_pressure;
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: LAMP2190 seed " << seed << ' ' << line << '\n';
    }
    if (lines_.empty()) {
      out << "population_curve: LAMP2190 seed " << seed << " NOT MEASURED: no year closed\n";
    }
  }

 private:
  struct Lit {
    std::array<std::uint8_t, core::kMonthsPerYear> months{};
    bool early = false;
  };

  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    const core::YearLedger& book = world.ledger.closed;
    std::array<std::uint8_t, core::kMonthsPerYear> any{};
    std::array<std::uint8_t, core::kMonthsPerYear> on_missed{};
    std::uint32_t missed = 0;
    std::uint32_t missed_early = 0;
    for (std::size_t at = 0; at < book.plan_due.size(); ++at) {
      const core::Grams due = book.plan_due[at];
      const core::Grams delivered = at < book.plan_delivered.size() ? book.plan_delivered[at] : 0;
      const bool miss = due > 0 && static_cast<double>(delivered) <
                                       static_cast<double>(due) * static_cast<double>(met_share_);
      const Lit lit = at < lit_.size() ? lit_[at] : Lit{};
      for (std::uint32_t month = 0; month < core::kMonthsPerYear; ++month) {
        any[month] = any[month] != 0 || lit.months[month] != 0 ? 1 : 0;
        on_missed[month] = on_missed[month] != 0 || (miss && lit.months[month] != 0) ? 1 : 0;
      }
      missed += miss ? 1U : 0U;
      missed_early += miss && lit.early ? 1U : 0U;
    }
    std::uint32_t lit_months = 0;
    std::uint32_t lit_on_missed = 0;
    for (std::uint32_t month = 0; month < core::kMonthsPerYear; ++month) {
      lit_months += any[month];
      lit_on_missed += on_missed[month];
    }
    std::ostringstream line;
    line << "year " << (year + 1U)
         << " ground=CollectAlarms_daily;miss=book_below_plan_met_share;early=DaysToPlanTurn>=4"
         << " lit_months " << lit_months << " of " << core::kMonthsPerYear
         << " lit_months_on_a_missed_position " << lit_on_missed << " positions_missed " << missed
         << " missed_lit_early " << missed_early << " under_pressure "
         << static_cast<int>(under_pressure_);
    lines_.push_back(line.str());
    lit_.clear();
    under_pressure_ = 0;
  }

  float met_share_ = 0.99F;
  std::vector<Lit> lit_;
  std::uint8_t under_pressure_ = 0;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_PLAN_THREAT_TALLY_H_
