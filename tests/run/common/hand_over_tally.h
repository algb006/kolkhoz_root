/// @file
/// @brief #1570's instrument: every kolkhoz cow handed to the district in
/// years 2-3 - by which door (the hay lamp's answer or another), the forecast
/// the answer acted on, the feed in stock that day, and whether the herds then
/// actually went short within the forecast's horizon (a true lamp or a false one).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// 0.37.366's milk -2.3 t a village rode on +62 cows handed in y2-y3 in 17
/// villages (core-boss-1503 [31]); boss [32] asks which door, and whether the
/// shortfall the hay answer acted on came true. The door: a kStockHandedOver of
/// a kolkhoz cow on the day after a HayAnswer::HandLog (orders staged at the
/// evening settle in the next day's steps) is the hay answer's; any other is
/// «other». «Went short»: the kolkhoz cows' herd_feed_short (the book's need
/// left uncovered, priced in hay) grew in the days from the hand-over to its
/// forecast's loss day.

#ifndef TESTS_RUN_COMMON_HAND_OVER_TALLY_H_
#define TESTS_RUN_COMMON_HAND_OVER_TALLY_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "hay_answer.h"

namespace run {

class HandOverTally {
 public:
  static constexpr std::uint32_t kFirstYear = 2;
  static constexpr std::uint32_t kLastYear = 3;

  explicit HandOverTally(const core::ITableSet& tables) {
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      cow_ = kinds->FindRowByKey("cow");
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      hay_ = resources->FindRowByKey("hay");
      straw_ = resources->FindRowByKey("straw");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        keys_.emplace_back(resources->CellText(row, 0));
      }
    }
  }

  /// @brief Once a day after the chairman: the kolkhoz cows handed today (the
  ///        difference of `cows_handed_this_year`, HerdTally's step count, the
  ///        year's running total), the cows' feed short today, and a snapshot
  ///        for each hand-over the hay answer ordered today.
  void CountDay(const core::WorldState& world,
                const std::vector<HayAnswer::HandLog>& logs,
                std::int64_t cows_handed_this_year) {
    const std::int64_t handed_today = cows_handed_this_year >= last_handed_
                                          ? cows_handed_this_year - last_handed_
                                          : cows_handed_this_year;
    last_handed_ = cows_handed_this_year;
    if (handed_today > 0 && InYears(world)) {
      handed_by_day_[world.calendar.day] += handed_today;
    }
    const core::Grams short_now = cow_ < world.ledger.current.herd_feed_short.size()
                                      ? world.ledger.current.herd_feed_short[cow_]
                                      : 0;
    const core::Grams grown =
        short_now >= last_short_ ? short_now - last_short_ : short_now;  // the year's turn
    last_short_ = short_now;
    short_by_day_[world.calendar.day] = grown;
    if (InYears(world)) {
      short_total_ += grown;
      short_days_ += grown > 0 ? 1U : 0U;
    }
    for (; seen_ < logs.size(); ++seen_) {
      const HayAnswer::HandLog& log = logs[seen_];
      if (!InYears(world)) {
        continue;
      }
      Snapshot snap;
      snap.log = log;
      snap.hay_g = Held(world, hay_);
      snap.straw_g = Held(world, straw_);
      snaps_.push_back(snap);
    }
  }

  /// @brief One HAND1570 line a hand-over (hay answer) and one a day of cows
  ///        handed by another door, then a summary line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::int64_t by_hay = 0;
    std::int64_t by_other = 0;
    std::uint32_t came_true = 0;
    for (const Snapshot& snap : snaps_) {
      core::Grams after = 0;
      const auto from = static_cast<core::SimDay>(snap.log.day + 1);
      const auto to = static_cast<core::SimDay>(snap.log.day + 1 +
                                                std::max<std::int64_t>(snap.log.days_to_loss, 1));
      for (auto it = short_by_day_.lower_bound(from); it != short_by_day_.end() && it->first < to;
           ++it) {
        after += it->second;
      }
      came_true += after > 0 ? 1U : 0U;
      const std::string key = snap.log.first_short.value < keys_.size()
                                  ? keys_[snap.log.first_short.value]
                                  : std::to_string(snap.log.first_short.value);
      out << "population_curve: HAND1570 seed " << seed << " day " << snap.log.day
          << " door hay cows " << snap.log.cows << " short_heads " << snap.log.short_heads
          << " first_short " << key << " days_to_loss " << snap.log.days_to_loss << " hay_kg "
          << snap.hay_g / 1000 << " straw_kg " << snap.straw_g / 1000 << " cows_fed_short_after_g "
          << after << '\n';
    }
    for (const auto& [day, cows] : handed_by_day_) {
      const bool hay = std::ranges::any_of(snaps_, [day](const Snapshot& snap) {
        return snap.log.day + 1 == day || snap.log.day == day;
      });
      (hay ? by_hay : by_other) += cows;
      if (!hay) {
        out << "population_curve: HAND1570 seed " << seed << " day " << day << " door other cows "
            << cows << '\n';
      }
    }
    out << "population_curve: HAND1570SUM seed " << seed << " y" << kFirstYear << "-y" << kLastYear
        << " cows_handed_by_hay " << by_hay << " cows_handed_by_other " << by_other
        << " hay_hand_overs " << snaps_.size() << " came_true "
        << came_true
        // THE MEASURE'S OWN LIFE: the cows' need left uncovered over y2-y3 at
        // all, so «came_true 0» reads either «never short» or «short elsewhere».
        << " cows_fed_short_y2y3_g " << short_total_ << " cows_short_days_y2y3 " << short_days_
        << '\n';
  }

 private:
  struct Snapshot {
    HayAnswer::HandLog log;
    core::Grams hay_g = 0;
    core::Grams straw_g = 0;
  };

  static bool InYears(const core::WorldState& world) {
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    return year >= kFirstYear && year <= kLastYear;
  }

  static core::Grams Held(const core::WorldState& world, std::uint32_t resource) {
    if (resource == core::kNoTableRow) {
      return 0;
    }
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level > 0) {
        held += core::UnreservedOf(unit, core::ResourceId{static_cast<std::uint16_t>(resource)});
      }
    }
    return held;
  }

  std::uint32_t cow_ = core::kNoTableRow;
  std::uint32_t hay_ = core::kNoTableRow;
  std::uint32_t straw_ = core::kNoTableRow;
  std::vector<std::string> keys_;
  std::map<core::SimDay, std::int64_t> handed_by_day_;
  std::map<core::SimDay, core::Grams> short_by_day_;
  core::Grams last_short_ = 0;
  std::int64_t last_handed_ = 0;
  core::Grams short_total_ = 0;
  std::uint32_t short_days_ = 0;
  std::size_t seen_ = 0;
  std::vector<Snapshot> snaps_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAND_OVER_TALLY_H_
