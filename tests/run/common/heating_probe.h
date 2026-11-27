/// @file
/// @brief HEAT2261 (#2261 stage 1, econ's base rates before the gates): the heated units' burn
///        by month - unit-days that wanted fuel, of them the days the buffer did not cover,
///        the same for dwellings, m³ burnt and m³ short. Read off HeatingState's cumulative
///        probe tallies at each month's last day; printed per year and by month of the year.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
#ifndef TESTS_RUN_COMMON_HEATING_PROBE_H_
#define TESTS_RUN_COMMON_HEATING_PROBE_H_

#include <array>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"

namespace run {

class HeatingProbe {
 public:
  /// @brief Once a day after the step: on a month's last day, the month's deltas.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t day = world.calendar.day;
    if ((day + 1U) % core::kDaysPerMonth != 0) {
      return;
    }
    const core::HeatingState& heating = world.heating;
    Month month;
    month.index = day / core::kDaysPerMonth;
    month.want = heating.probe_want_unit_days - last_.probe_want_unit_days;
    month.empty = heating.probe_empty_unit_days - last_.probe_empty_unit_days;
    month.want_house = heating.probe_want_house_days - last_.probe_want_house_days;
    month.empty_house = heating.probe_empty_house_days - last_.probe_empty_house_days;
    month.burnt = heating.probe_burnt_m3 - last_.probe_burnt_m3;
    month.short_m3 = heating.probe_short_m3 - last_.probe_short_m3;
    months_.push_back(month);
    last_ = heating;
    // The buffers standing at the month's end, m³, and how many heated units hold any.
    double standing = 0.0;
    std::uint32_t holding = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.dead == 0 && unit.heat_buffer_m3 > 0.0F) {
        standing += static_cast<double>(unit.heat_buffer_m3);
        ++holding;
      }
    }
    months_.back().standing = standing;
    months_.back().holding = holding;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    if (months_.empty()) {
      out << "HEAT2261 seed " << seed << " NOT MEASURED (no month closed)\n";
      return;
    }
    constexpr std::uint32_t kMonths = core::kDaysPerYear / core::kDaysPerMonth;
    std::array<Month, kMonths> by_month{};
    out << std::fixed << std::setprecision(1);
    for (std::size_t first = 0; first < months_.size(); first += kMonths) {
      Month year;
      for (std::size_t i = first; i < first + kMonths && i < months_.size(); ++i) {
        Add(year, months_[i]);
        Add(by_month[months_[i].index % kMonths], months_[i]);
      }
      out << "HEAT2261 seed " << seed << " year " << (first / kMonths) + 1 << ' ';
      Line(out, year);
      const Month& last = months_[std::min(first + kMonths, months_.size()) - 1];
      out << " standing_m3 " << last.standing << " holding " << last.holding << '\n';
    }
    for (std::uint32_t m = 0; m < kMonths; ++m) {
      out << "HEAT2261 seed " << seed << " month " << m + 1 << ' ';
      Line(out, by_month[m]);
      out << '\n';
    }
  }

 private:
  struct Month {
    std::uint32_t index = 0;
    std::uint64_t want = 0;
    std::uint64_t empty = 0;
    std::uint64_t want_house = 0;
    std::uint64_t empty_house = 0;
    double burnt = 0.0;
    double short_m3 = 0.0;
    double standing = 0.0;
    std::uint32_t holding = 0;
  };

  static void Add(Month& into, const Month& month) {
    into.want += month.want;
    into.empty += month.empty;
    into.want_house += month.want_house;
    into.empty_house += month.empty_house;
    into.burnt += month.burnt;
    into.short_m3 += month.short_m3;
  }

  static double Pct(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
  }

  static void Line(std::ostream& out, const Month& m) {
    out << "want_unit_days " << m.want << " empty " << m.empty << " (" << Pct(m.empty, m.want)
        << "%) house_days " << m.want_house << " empty " << m.empty_house << " ("
        << Pct(m.empty_house, m.want_house) << "%) burnt_m3 " << m.burnt << " short_m3 "
        << m.short_m3;
  }

  core::HeatingState last_;
  std::vector<Month> months_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HEATING_PROBE_H_
