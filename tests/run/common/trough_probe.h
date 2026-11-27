/// @file
/// @brief TROUGH2199 (econ's G4 for #2199b, not for release): per year, the lowest day of the
///        free food stock over the funds in days of the village's need (ISimulation::
///        FreeStockToday - the issue's own reading), and the days that free stock stood at
///        nought - the funds are then all that is left, the closest the run can see to «a fund
///        drawn below its set amount» (the free stock is clamped at the funds, so a breach
///        itself is NOT visible - the count says how often the funds were the floor).
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_TROUGH_PROBE_H_
#define TESTS_RUN_COMMON_TROUGH_PROBE_H_

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/free_stock_view.h"
#include "core_sim/step.h"

namespace run {

class TroughProbe {
 public:
  /// @brief Once a day after the step.
  void CountDay(const core::ISimulation& simulation) {
    const core::FreeStockView free = simulation.FreeStockToday();
    const std::uint32_t day = simulation.CompletedState().calendar.day;
    if (free.measured) {
      ++measured_days_;
      if (min_days_ < 0.0F || free.days < min_days_) {
        min_days_ = free.days;
      }
      if (free.free_grain_eq_grams <= 0) {
        ++floor_days_;
      }
    }
    if ((day + 1U) % core::kDaysPerYear == 0) {
      std::ostringstream line;
      line << "year " << (day / core::kDaysPerYear) + 1U << " ground=FreeStockToday_days_of_need";
      if (measured_days_ == 0) {
        line << " NOT MEASURED";
      } else {
        line << " min_free_days " << min_days_ << " days_free_at_nought " << floor_days_
             << " measured_days " << measured_days_;
      }
      lines_.push_back(line.str());
      min_days_ = -1.0F;
      floor_days_ = measured_days_ = 0;
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : lines_) {
      out << "population_curve: TROUGH2199 seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  float min_days_ = -1.0F;
  std::uint32_t floor_days_ = 0;
  std::uint32_t measured_days_ = 0;
  std::vector<std::string> lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TROUGH_PROBE_H_
