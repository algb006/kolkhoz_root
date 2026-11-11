/// @file
/// @brief #1606's gate A1 (econ, econ-boss-1606-heap-cure-gates [1] §3): the hay
/// forecast's error on the eve of the new cut. On two days - y1 September's
/// first, y2 January's first - the lamp's forecast (ISimulation::HayLampToday)
/// says what hay it expects held at the end of the day before next year's
/// first scythes; on that eve the world's hay is read: the herds' store
/// allowance plus the field heaps above the plan's debt, raw. One line a
/// village; the reader takes the mean absolute error over villages.
/// @threading SINGLE_THREADED
/// Test-side code, read once a day from the thread that owns the simulation.
///
/// THE EVE is the horizon's end less hay_new_cut_in_store_days (farming.csv)
/// less one: the lamp's horizon runs that many days into the new cut. «-1»
/// for a forecast with no such day (a horizon that does not reach it).

#ifndef TESTS_RUN_COMMON_HEAP_CURE_A1_TALLY_H_
#define TESTS_RUN_COMMON_HEAP_CURE_A1_TALLY_H_

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>

#include "core_common/calendar.h"
#include "core_common/hay_lamp_trace.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class HeapCureA1Tally {
 public:
  HeapCureA1Tally(const core::ITableSet& tables, std::uint64_t seed) : seed_(seed) {
    if (const core::ITable* const farming = tables.FindTable("farming")) {
      const std::uint32_t row = farming->FindRowByKey("hay_new_cut_in_store_days");
      const std::uint32_t value = farming->FindColumn("value");
      if (row != core::kNoTableRow && value != core::kNoTableColumn) {
        into_cut_ = static_cast<std::uint32_t>(farming->CellReal(row, value).value_or(3.0F));
      }
    }
  }

  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay day = world.calendar.day;
    for (Read& read : reads_) {
      if (day == read.on_day) {
        const core::HayLampTrace trace = simulation.HayLampToday();
        if (trace.traced && trace.horizon_days > into_cut_ + 1U) {
          const std::uint32_t offset = trace.horizon_days - into_cut_ - 1U;
          read.eve_day = day + offset;
          read.predicted_kg =
              offset < trace.hay_held_kg_by_day.size() ? trace.hay_held_kg_by_day[offset] : -1.0F;
        }
      }
      if (read.eve_day && day == *read.eve_day) {
        const core::HayLampTrace trace = simulation.HayLampToday();
        read.actual_kg = trace.store_hay_kg + RawHeapsKg(trace);
      }
    }
  }

  void Print(std::ostream& out) const {
    out << "population_curve: A1HEAP seed " << seed_;
    for (const Read& read : reads_) {
      out << ' ' << read.name << ":read_day=" << read.on_day
          << ",eve_day=" << (read.eve_day ? static_cast<std::int64_t>(*read.eve_day) : -1)
          << ",predicted_kg=" << read.predicted_kg << ",actual_kg=" << read.actual_kg;
    }
    out << '\n';
  }

 private:
  struct Read {
    const char* name;
    core::SimDay on_day;
    std::optional<core::SimDay> eve_day;
    float predicted_kg = -1.0F;
    float actual_kg = -1.0F;
  };

  /// The heaps as they lie (heap_hay_counted_kg is the raw count above the
  /// plan's debt, whatever the cure counts in the forecast).
  static float RawHeapsKg(const core::HayLampTrace& trace) { return trace.heap_hay_counted_kg; }

  std::uint64_t seed_ = 0;
  std::uint32_t into_cut_ = 3;
  // y1 September's first day, y2 January's first day (4 days a month).
  std::array<Read, 2> reads_ = {Read{.name = "y1m9",
                                     .on_day = 8U * core::kDaysPerMonth,
                                     .eve_day = std::nullopt,
                                     .predicted_kg = -1.0F,
                                     .actual_kg = -1.0F},
                                Read{.name = "y2m1",
                                     .on_day = core::kDaysPerYear,
                                     .eve_day = std::nullopt,
                                     .predicted_kg = -1.0F,
                                     .actual_kg = -1.0F}};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HEAP_CURE_A1_TALLY_H_
