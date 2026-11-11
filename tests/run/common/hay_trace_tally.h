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

#include <algorithm>
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
      straw_ = resources->FindRowByKey("straw");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        resource_keys_.emplace_back(resources->CellText(row, 0));
      }
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
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kStockHandedOver) {
        day_handed_ += event.amount;  // the bot's hand-overs that fed the rest
      }
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
    // THE DAY'S ACTUALS (boss: «the forecast's short days and death day vs actual
    // unfed days / deaths, and what fed the herd that the forecast did not
    // count»): the book's rises today - the herds' hay eaten, all feed eaten,
    // the feed they went short, the hay cut - and the herds unfed tonight.
    const auto rise = [](core::Grams now_sum, core::Grams& last) {
      const core::Grams today = now_sum >= last ? now_sum - last : now_sum;
      last = now_sum;
      return today;
    };
    const auto sum = [](const core::ResourceAmounts& amounts) {
      core::Grams total = 0;
      for (const core::Grams grams : amounts) {
        total += grams;
      }
      return total;
    };
    const core::YearLedger& book = now.ledger.current;
    day_hay_eaten_ = rise(sum(book.herd_hay_eaten), last_hay_eaten_);
    day_feed_eaten_ = rise(sum(book.feed), last_feed_eaten_);
    day_feed_short_ = rise(sum(book.herd_feed_short), last_feed_short_);
    day_cut_ = rise(hay_ < book.harvest.size() ? book.harvest[hay_] : 0, last_cut_);
    // The straw: what the threshing lays (harvest) and the herds eat (feed),
    // today - a feed whose INCOME after today the forecast may not count.
    day_straw_laid_ =
        rise(straw_ < book.harvest.size() ? book.harvest[straw_] : 0, last_straw_laid_);
    day_straw_eaten_ = rise(straw_ < book.feed.size() ? book.feed[straw_] : 0, last_straw_eaten_);
    // EVERY FEED THE HERDS ATE TODAY, by resource (the straw read 0: the
    // non-hay 56 t a month was something else).
    last_feed_by_.resize(book.feed.size(), 0);
    day_feed_by_.assign(book.feed.size(), 0);
    for (std::size_t resource = 0; resource < book.feed.size(); ++resource) {
      day_feed_by_[resource] = rise(book.feed[resource], last_feed_by_[resource]);
    }
    unfed_herds_ = 0;
    max_unfed_days_ = 0.0F;
    for (const core::HerdRow& herd : now.herds.rows) {
      if (herd.household_owned == 0 && herd.unfed_days > 0.0F) {
        ++unfed_herds_;
        max_unfed_days_ = std::max(max_unfed_days_, herd.unfed_days);
      }
    }
    PrintDay(simulation, out);
    day_deaths_.clear();
    day_handed_ = 0;
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
        << " heap_hauled_today_kg " << day_hauled_ / core::kGramsPerKilogram << " need_u_a_day "
        << (t.horizon_days > 0 ? t.need_units / t.horizon_days : 0.0F) << " hay_eaten_today_kg "
        << day_hay_eaten_ / core::kGramsPerKilogram << " feed_eaten_today_kg "
        << day_feed_eaten_ / core::kGramsPerKilogram << " feed_short_today_kg "
        << day_feed_short_ / core::kGramsPerKilogram << " hay_cut_today_kg "
        << day_cut_ / core::kGramsPerKilogram << " straw_laid_today_kg "
        << day_straw_laid_ / core::kGramsPerKilogram << " straw_eaten_today_kg "
        << day_straw_eaten_ / core::kGramsPerKilogram << " handed_today " << day_handed_
        << " unfed_herds_now " << unfed_herds_ << " max_unfed_days " << max_unfed_days_
        << " fed_by";
    bool any_feed = false;
    for (std::size_t resource = 0; resource < day_feed_by_.size(); ++resource) {
      if (day_feed_by_[resource] > 0) {
        out << ' '
            << (resource < resource_keys_.size() ? resource_keys_[resource]
                                                 : std::to_string(resource))
            << ':' << day_feed_by_[resource] / core::kGramsPerKilogram;
        any_feed = true;
      }
    }
    if (!any_feed) {
      out << " -";
    }
    out << " died_today";
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
    // #1606: the forecast's need over the horizon by kind, its projected
    // head-days, and today's real heads - kind:need_u/head_days/heads_today;
    // last on the line, so the readers of the fields before it stand.
    out << " by_kind";
    for (std::size_t kind = 0; kind < t.heads_today_by_kind.size(); ++kind) {
      const float need = kind < t.need_units_by_kind.size() ? t.need_units_by_kind[kind] : 0.0F;
      const float head_days = kind < t.head_days_by_kind.size() ? t.head_days_by_kind[kind] : 0.0F;
      if (need > 0.0F || t.heads_today_by_kind[kind] > 0) {
        out << ' ' << KindKey(static_cast<std::uint32_t>(kind)) << ':' << need << '/' << head_days
            << '/' << t.heads_today_by_kind[kind];
      }
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
  core::Grams day_hay_eaten_ = 0;
  core::Grams last_hay_eaten_ = 0;
  core::Grams day_feed_eaten_ = 0;
  core::Grams last_feed_eaten_ = 0;
  core::Grams day_feed_short_ = 0;
  core::Grams last_feed_short_ = 0;
  core::Grams day_cut_ = 0;
  core::Grams last_cut_ = 0;
  std::uint32_t straw_ = core::kNoTableRow;
  std::vector<std::string> resource_keys_;
  std::vector<core::Grams> day_feed_by_;
  std::vector<core::Grams> last_feed_by_;
  core::Grams day_straw_laid_ = 0;
  core::Grams last_straw_laid_ = 0;
  core::Grams day_straw_eaten_ = 0;
  core::Grams last_straw_eaten_ = 0;
  std::int64_t day_handed_ = 0;
  std::uint32_t unfed_herds_ = 0;
  float max_unfed_days_ = 0.0F;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_TRACE_TALLY_H_
