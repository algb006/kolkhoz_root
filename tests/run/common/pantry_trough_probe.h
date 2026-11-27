/// @file
/// @brief PANTRY2295 (econ's #2295, not for release): on each year's lowest-free-stock day
///        (ISimulation::FreeStockToday, the issue's reading), the share of families whose pantry
///        holds ≥ ¼ of a month's need (one game day of the 4-day month), and the median months
///        held. The month's need is read at this month's pace (month_need_kcal so far × 4 /
///        days into the month, today included).
///        The pantry is the meal-countable W term (0.37.431): the winter store counts only
///        Dec-May at its pace - help_fund.cpp's WinterStoreMonthShare, mirrored here from the
///        same food.csv rows (a probe cannot call the file-local original).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_PANTRY_TROUGH_PROBE_H_
#define TESTS_RUN_COMMON_PANTRY_TROUGH_PROBE_H_

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/free_stock_view.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class PantryTroughProbe {
 public:
  explicit PantryTroughProbe(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      const std::uint32_t kcal = resources->FindColumn("kcal_per_gram");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        kcal_.push_back(kcal != core::kNoTableColumn ? resources->CellReal(row, kcal).value_or(0.0F)
                                                     : 0.0F);
      }
      dried_ = resources->FindRowByKey("dried_forest_gifts");
    }
    // W (econ, 0.37.431's help_fund.cpp WinterStoreMonthShare, mirrored): the dried store
    // counts only Dec-May, at the month's share of the days left in its window.
    if (const core::ITable* const food = tables.FindTable("food")) {
      const std::uint32_t value = food->FindColumn("value");
      const auto month_of = [&](std::string_view key, std::uint32_t fallback) {
        const std::uint32_t row = food->FindRowByKey(key);
        if (row == core::kNoTableRow || value == core::kNoTableColumn) {
          return fallback;
        }
        const float human = food->CellReal(row, value).value_or(static_cast<float>(fallback + 1U));
        return static_cast<std::uint32_t>(human) - 1U;
      };
      dried_from_ = month_of("dried_eaten_from_month", 11U);
      dried_to_ = month_of("dried_eaten_to_month", 4U);
    }
  }

  /// @brief Once a day after the step.
  void CountDay(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day = world.calendar.day;
    const core::FreeStockView free = simulation.FreeStockToday();
    if (free.measured && (low_days_ < 0.0F || free.days < low_days_)) {
      low_days_ = free.days;
      low_day_ = day;
      ReadPantries(world);
    }
    if ((day + 1U) % core::kDaysPerYear != 0) {
      return;
    }
    std::ostringstream line;
    line << "year " << (day / core::kDaysPerYear) + 1U
         << " ground=pantry_kcal_whole_over_month_need_at_this_months_pace";
    if (low_days_ < 0.0F || families_ == 0) {
      line << " NOT MEASURED";
    } else {
      std::vector<double> months = months_;
      std::sort(months.begin(), months.end());
      line << " trough_day " << (low_day_ % core::kDaysPerYear) << " free_days " << low_days_
           << " families " << families_ << " holding_quarter_month " << holding_ << " ("
           << 100.0 * holding_ / families_ << "%) median_months " << months[months.size() / 2];
    }
    lines_.push_back(line.str());
    low_days_ = -1.0F;
    families_ = holding_ = 0;
    months_.clear();
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: PANTRY2295 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  void ReadPantries(const core::WorldState& world) {
    families_ = holding_ = 0;
    months_.clear();
    const std::uint32_t into = (world.calendar.day % core::kDaysPerMonth) + 1U;
    for (const core::FamilyRow& family : world.families.rows) {
      if (!(family.month_need_kcal > 0.0F)) {
        continue;
      }
      const double month_need = static_cast<double>(family.month_need_kcal) *
                                static_cast<double>(core::kDaysPerMonth) /
                                static_cast<double>(into);
      double held = 0.0;
      const double dried_share = DriedMonthShare(world);
      for (std::size_t food = 0; food < family.pantry.size() && food < kcal_.size(); ++food) {
        held += static_cast<double>(family.pantry[food]) * static_cast<double>(kcal_[food]) *
                (food == dried_ ? dried_share : 1.0);
      }
      const double months = held / month_need;
      ++families_;
      holding_ += months >= 0.25 ? 1U : 0U;
      months_.push_back(months);
    }
  }

  /// The dried store's share the meal takes this month (WinterStoreMonthShare's rule).
  double DriedMonthShare(const core::WorldState& world) const {
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    const bool inside = dried_from_ <= dried_to_ ? (month >= dried_from_ && month <= dried_to_)
                                                 : (month >= dried_from_ || month <= dried_to_);
    if (!inside) {
      return 0.0;
    }
    const std::uint32_t after = (dried_to_ + 12U - month) % 12U;
    const std::uint32_t days_left = (after + 1U) * core::kDaysPerMonth;
    return std::min(1.0, static_cast<double>(core::kDaysPerMonth) / static_cast<double>(days_left));
  }

  std::vector<float> kcal_;
  std::uint32_t dried_ = core::kNoTableRow;
  std::uint32_t dried_from_ = 11U;
  std::uint32_t dried_to_ = 4U;
  float low_days_ = -1.0F;
  std::uint32_t low_day_ = 0;
  std::uint32_t families_ = 0;
  std::uint32_t holding_ = 0;
  std::vector<double> months_;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_PANTRY_TROUGH_PROBE_H_
