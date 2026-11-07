#ifndef TESTS_RUN_COMMON_HAY_TRACE_TALLY_H_
#define TESTS_RUN_COMMON_HAY_TRACE_TALLY_H_

/// @file hay_trace_tally.h
/// @brief #1586's print (econ, econ-boss-1585-novice-dieoff [1] §4): the hay lamp's day laid open
///        (ISimulation::HayLampToday), one line a day from y1 month 6 to y2 month 6, for the seeds
///        KOLKHOZ_HAY_TRACE_SEEDS names (comma-separated; none: nothing printed) -
///        «HAYTRACE seed S day D y Y m M short 0/1 ahead A horizon H short_days N death 0/1
///        store_kg heaps_counted_kg cut_to_come_kg need_u covered_u short_hay_kg horses/kept
///        cows/kept points P advice K heads stock/horses below stock/horses rungs
///        reason:value,...». Reasons are HayRungReason's numbers (hay_lamp_trace.h). Printed after
///        the day's chairman, at the evening's state.
/// @threading SINGLE_THREADED - the run's own loop.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/hay_lamp_trace.h"
#include "core_sim/step.h"
#include "env_value.h"

namespace run {

class HayTraceTally {
 public:
  explicit HayTraceTally(std::uint64_t seed) : seed_(seed) {
    if (const std::optional<std::string> seeds = EnvValue("KOLKHOZ_HAY_TRACE_SEEDS")) {
      std::stringstream list(*seeds);
      std::string item;
      while (std::getline(list, item, ',')) {
        if (!item.empty() && std::strtoull(item.c_str(), nullptr, 10) == seed) {
          on_ = true;
        }
      }
    }
  }

  void CountDayEnd(const core::ISimulation& simulation, std::ostream& out) const {
    if (!on_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = world.calendar.date.year;
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month) + 1U;  // human 1..12
    if (!((year == 1 && month >= 6) || (year == 2 && month <= 6))) {
      return;
    }
    const core::HayLampTrace t = simulation.HayLampToday();
    out << "population_curve: HAYTRACE seed " << seed_ << " day " << world.calendar.day << " y "
        << year << " m " << month;
    if (!t.traced) {
      out << " NOT TRACED\n";
      return;
    }
    out << " short " << (t.short_ahead ? 1 : 0) << " ahead " << t.days_ahead << " horizon "
        << t.horizon_days << " short_days " << t.short_days << " death " << (t.death_ahead ? 1 : 0)
        << " store_kg " << t.store_hay_kg << " heaps_counted_kg " << t.heap_hay_counted_kg
        << " cut_to_come_kg " << t.cut_to_come_kg << " need_u " << t.need_units << " covered_u "
        << t.covered_units << " short_hay_kg " << t.short_hay_kg << " horses " << t.horses << '/'
        << t.horses_kept << " cows " << t.cows << '/' << t.cows_kept << " points " << t.points
        << " advice " << static_cast<int>(t.advice) << " heads " << t.hand_over_stock << '/'
        << t.hand_over_horses << " below " << t.below_floor_stock << '/' << t.below_floor_horses
        << " rungs";
    for (const core::HayRungStep& step : t.rungs) {
      out << ' ' << static_cast<int>(step.reason) << ':' << step.value;
    }
    out << '\n';
  }

 private:
  std::uint64_t seed_ = 0;
  bool on_ = false;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_TRACE_TALLY_H_
