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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/hay_lamp_trace.h"
#include "core_common/state_table_ops.h"
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
    // A1' needs the fodder units of a kg (resources.csv feed_value), hay's for
    // the forecast's need in hay and every good's for the feed lots bought.
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      const std::uint32_t column = resources->FindColumn("feed_value");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        feed_value_.push_back(column != core::kNoTableColumn
                                  ? resources->CellReal(row, column).value_or(0.0F)
                                  : 0.0F);
      }
      hay_ = resources->FindRowByKey("hay");
    }
  }

  /// @brief A1' (boss [2], econ-boss-1606-a1-redefined): every step, the heads
  ///        handed to the district (kStockHandedOver: day, kind, heads) and the
  ///        district carts that came in with feed (day, fodder units) - the
  ///        world's answers to the lamp, which the counterfactual eve undoes.
  void CountStep(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind != core::EventKind::kStockHandedOver || event.amount <= 0) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.herds, event.herd);
      const std::uint32_t kind = row != core::kNoRow ? world.herds.rows[row].kind.value : 0xFFFFU;
      handed_.push_back(Handed{.day = day, .kind = kind, .heads = event.amount});
    }
    std::vector<std::pair<std::uint32_t, double>> carts;
    for (std::uint32_t row = 0; row < world.limit_deliveries.rows.size(); ++row) {
      double units = 0.0;
      const core::ResourceAmounts& goods = world.limit_deliveries.rows[row].goods;
      for (std::size_t resource = 0; resource < goods.size() && resource < feed_value_.size();
           ++resource) {
        units += static_cast<double>(goods[resource]) / 1000.0 *
                 static_cast<double>(feed_value_[resource]);
      }
      carts.emplace_back(world.limit_deliveries.row_ids[row].value, units);
    }
    for (const auto& [id, units] : carts_) {
      const bool gone = std::ranges::none_of(carts, [id](const auto& c) { return c.first == id; });
      if (gone && units > 0.0) {
        lots_.push_back(Lot{.day = day, .units = units});
      }
    }
    carts_ = std::move(carts);
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
          // The forecast's own need a head-day, by kind, in hay kg (A1').
          read.hay_kg_per_head_day.assign(trace.need_units_by_kind.size(), 0.0);
          const double hay_units = hay_ < feed_value_.size() && feed_value_[hay_] > 0.0F
                                       ? static_cast<double>(feed_value_[hay_])
                                       : 0.45;
          for (std::size_t kind = 0;
               kind < trace.need_units_by_kind.size() && kind < trace.head_days_by_kind.size();
               ++kind) {
            if (trace.head_days_by_kind[kind] > 0.0F) {
              read.hay_kg_per_head_day[kind] = static_cast<double>(trace.need_units_by_kind[kind]) /
                                               static_cast<double>(trace.head_days_by_kind[kind]) /
                                               hay_units;
            }
          }
          read.hay_units_per_kg = hay_units;
        }
      }
      if (read.eve_day && day == *read.eve_day) {
        const core::HayLampTrace trace = simulation.HayLampToday();
        read.actual_kg = trace.store_hay_kg + RawHeapsKg(trace);
        // A1': the counterfactual eve - the handed heads' need from their day
        // to the eve added back, the feed that came after the read taken off.
        double back = 0.0;
        for (const Handed& h : handed_) {
          if (h.day > read.on_day && h.day <= day && h.kind < read.hay_kg_per_head_day.size()) {
            back += static_cast<double>(h.heads) * read.hay_kg_per_head_day[h.kind] *
                    static_cast<double>(day - h.day);
            read.handed_heads += h.heads;
          }
        }
        double bought = 0.0;
        for (const Lot& lot : lots_) {
          if (lot.day > read.on_day && lot.day <= day) {
            // Fodder units are counted off kg already (CountStep): hay kg = units ÷ hay's
            // units a kg. 8036c7ab multiplied by 1000 here too (the first pair's reader
            // recomputed it from the printed terms).
            bought += lot.units / read.hay_units_per_kg;
          }
        }
        read.handed_back_kg = static_cast<float>(back);
        read.bought_kg = static_cast<float>(bought);
        read.counterfactual_kg =
            static_cast<float>(static_cast<double>(read.actual_kg) + back - bought);
      }
    }
  }

  void Print(std::ostream& out) const {
    out << "population_curve: A1HEAP seed " << seed_;
    for (const Read& read : reads_) {
      out << ' ' << read.name << ":read_day=" << read.on_day
          << ",eve_day=" << (read.eve_day ? static_cast<std::int64_t>(*read.eve_day) : -1)
          << ",predicted_kg=" << read.predicted_kg << ",actual_kg=" << read.actual_kg
          << ",counterfactual_kg=" << read.counterfactual_kg
          << ",handed_back_kg=" << read.handed_back_kg << ",bought_kg=" << read.bought_kg
          << ",handed_heads=" << read.handed_heads;
    }
    out << '\n';
    // The per-day record A1' rests on, every village (boss [2] note 1).
    out << "population_curve: A1HANDED seed " << seed_;
    for (const Handed& h : handed_) {
      out << ' ' << h.day << ':' << h.kind << ':' << h.heads;
    }
    out << (handed_.empty() ? " -" : "") << '\n';
    out << "population_curve: A1LOTS seed " << seed_;
    for (const Lot& lot : lots_) {
      out << ' ' << lot.day << ':' << std::llround(lot.units);
    }
    out << (lots_.empty() ? " -" : "") << '\n';
  }

 private:
  struct Read {
    const char* name = "";
    core::SimDay on_day = 0;
    std::optional<core::SimDay> eve_day;
    float predicted_kg = -1.0F;
    float actual_kg = -1.0F;
    float counterfactual_kg = -1.0F;  ///< A1': actual + handed heads' need - feed bought
    float handed_back_kg = 0.0F;
    float bought_kg = 0.0F;
    std::int64_t handed_heads = 0;
    std::vector<double> hay_kg_per_head_day;  ///< the forecast's need, by kind, at the read
    double hay_units_per_kg = 0.45;
  };

  struct Handed {
    core::SimDay day = 0;
    std::uint32_t kind = 0;
    std::int64_t heads = 0;
  };

  struct Lot {
    core::SimDay day = 0;
    double units = 0.0;  ///< fodder units that came in
  };

  std::vector<float> feed_value_;
  std::uint32_t hay_ = core::kNoTableRow;
  std::vector<Handed> handed_;
  std::vector<Lot> lots_;
  std::vector<std::pair<std::uint32_t, double>> carts_;

  /// The heaps as they lie (heap_hay_counted_kg is the raw count above the
  /// plan's debt, whatever the cure counts in the forecast).
  static float RawHeapsKg(const core::HayLampTrace& trace) { return trace.heap_hay_counted_kg; }

  std::uint64_t seed_ = 0;
  std::uint32_t into_cut_ = 3;
  // y1 September's first day, y2 January's first day (4 days a month).
  std::array<Read, 2> reads_ = {MakeRead("y1m9", 8U * core::kDaysPerMonth),
                                MakeRead("y2m1", core::kDaysPerYear)};

  static Read MakeRead(const char* name, core::SimDay on_day) {
    Read read;
    read.name = name;
    read.on_day = on_day;
    return read;
  }
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HEAP_CURE_A1_TALLY_H_
