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
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/hay_lamp_trace.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"
#include "env_value.h"

namespace run {

class HayTraceTally {
 public:
  HayTraceTally(const core::ITableSet& tables, std::uint64_t seed) : seed_(seed) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      hay_ = resources->FindRowByKey("hay");
    }
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      for (std::uint32_t row = 0; row < kinds->RowCount(); ++row) {
        kind_keys_.emplace_back(kinds->CellText(row, 0));
      }
    }
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

  /// @brief Every step: the day's hunger deaths of the kolkhoz herds by kind
  ///        (kHerdDied, not routine: the hunger's own severity), boss's added
  ///        column of #1586 (i)'s pair.
  void CountStep(const core::WorldState& world) {
    if (!on_) {
      return;
    }
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind != core::EventKind::kHerdDied ||
          event.severity == core::EventSeverity::kRoutine) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.herds, event.herd);
      if (row == core::kNoRow || world.herds.rows[row].household_owned != 0) {
        continue;
      }
      day_deaths_[world.herds.rows[row].kind.value] += event.amount;
    }
  }

  void CountDayEnd(const core::ISimulation& simulation, std::ostream& out) {
    if (!on_) {
      return;
    }
    // The day's heap hay hauled into the stores (the book's hauled_to_stores
    // of hay, today's rise), and the day's deaths; both start the next day at
    // nought. The book's turn resets its sum, so a fall reads as the new year's.
    const core::WorldState& now = simulation.CompletedState();
    const core::Grams hauled_sum = hay_ < now.ledger.current.hauled_to_stores.size()
                                       ? now.ledger.current.hauled_to_stores[hay_]
                                       : 0;
    day_hauled_ = hauled_sum >= last_hauled_ ? hauled_sum - last_hauled_ : hauled_sum;
    last_hauled_ = hauled_sum;
    PrintDay(simulation, out);
    day_deaths_.clear();
  }

 private:
  void PrintDay(const core::ISimulation& simulation, std::ostream& out) const {
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
        << " heap_hauled_today_kg " << day_hauled_ / core::kGramsPerKilogram << " died_today";
    if (day_deaths_.empty()) {
      out << " -";
    }
    for (const auto& [kind, heads] : day_deaths_) {
      out << ' ' << KindKey(kind) << ':' << heads;
    }
    out << " rungs";
    for (const core::HayRungStep& step : t.rungs) {
      out << ' ' << static_cast<int>(step.reason) << ':' << step.value;
    }
    out << '\n';
  }

  std::string KindKey(std::uint32_t kind) const {
    return kind < kind_keys_.size() ? kind_keys_[kind] : std::to_string(kind);
  }

  std::uint64_t seed_ = 0;
  bool on_ = false;
  std::uint32_t hay_ = core::kNoTableRow;
  std::vector<std::string> kind_keys_;
  std::map<std::uint32_t, std::int64_t> day_deaths_;
  core::Grams day_hauled_ = 0;
  core::Grams last_hauled_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_TRACE_TALLY_H_
