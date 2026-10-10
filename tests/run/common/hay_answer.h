/// @file
/// @brief The run's chairman answers the herds' yellow stage «к укосу сена не
/// хватит» (kHerdHayShortAhead) as a chairman who READS THE LAMP would: more
/// hands on the cut while the lamp says «the cut», and the heads the lamp's
/// advice names handed back to the district — the heads above the floors,
/// never the pair named below them.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THIS EXISTS (boss-core-start-no-yards-2026-09-30 [20], [21], [27],
/// [29]). Since 0.37.62 a herd calves by the share of it under a roof, the
/// canon's cows grow by a third, and the hay is shared in the herds' order.
/// On seeds 1934 and 1938 the team starved in the spring of year 5 — 22
/// horses to 7 — and nobody ploughed again; the yellow had lit, and the
/// canon's chairman had no answer to it.
///
/// THE ANSWER IS THE LAMP'S SINCE 0.37.142, AND NO LONGER THIS FILE'S OWN
/// (boss-all-carts-carry-people-go-2026-10-02 [114], [121], [131]). Until
/// then the chairman kept floors of his own — the horses beyond the harness's
/// peak, the cows over the milk plan's floor, the horses over the
/// ploughing's — counted the cows' yield and the highest milk position
/// himself, and sized an order off the lamp's `amount` (the heads unfed on
/// the worst day), half a herd at most. The core's advice named other heads
/// in another order, and a chairman who followed IT (host's novice, 27
/// villages of 0.37.138) ended with three cows: the lamp and the canon's
/// chairman were two rules, and only one of them was measured. Now there is
/// one: the floors are the core's (subprojects/core_production/
/// herd_floors.h), the numbers are the advice's (Alarm::hand_over_horses,
/// hand_over_stock — the LEAST heads that feed the rest), and this file
/// turns them into orders.
/// - WHEN: the yellow stands, every day it stands. «The cut» advised — the
///   avral on every meadow in its cut not yet rushed to the top. «Fewer
///   heads» advised, first move or second — the heads named, on that day.
///   The orders settle in the next day's steps, and the next evening's lamp
///   is read after them;
/// - WHOM: `hand_over_horses` adults of the kolkhoz's horse herds and
///   `hand_over_stock` adults of its cow herds, herd by herd in row order —
///   the forecast's own order of taking them. The core takes the oldest;
/// - «BUY FEED» advised — the amount the lamp names (the LEAST purchase that
///   feeds the herds since 0.37.142) in lots of the first lot carrying that
///   feed which the door takes today, and the heads named behind it, if any.
///   The first form of 0.37.142 bought nothing, and the lamp stood 28-44
///   days a year in every village of the canon: on 245 yellow days of year
///   4's 363 the advice was «buy feed» with no head behind it;
/// - NOT ANSWERED, counted and printed: «the granary» (the building is the
///   building chairman's); and the pair BELOW the floors (Alarm::below_floor_stock,
///   below_floor_horses) — the price of the plan is the player's to choose,
///   and the canon's chairman does not choose it: the hunger decides, as it
///   did before this answer.
///
/// Called by LimitPolicy::RunDay (the district's policy — the hand-over is
/// the district's deal), so every run and host's canon that runs LimitPolicy
/// answers the yellow without being wired again.
#ifndef TESTS_RUN_COMMON_HAY_ANSWER_H_
#define TESTS_RUN_COMMON_HAY_ANSWER_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core_catalog/limit_catalog.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/office_views.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief One day's answer to the herds' hay yellow, if it stands.
class HayAnswer {
 public:
  explicit HayAnswer(const core::ITableSet& tables) {
    const core::ITable* livestock = tables.FindTable("livestock");
    if (livestock == nullptr) {
      return;
    }
    const std::uint32_t home = livestock->FindColumn("home_unit");
    for (std::uint32_t row = 0; row < livestock->RowCount(); ++row) {
      const std::string_view key =
          home == core::kNoTableColumn ? std::string_view{} : livestock->CellText(row, home);
      kinds_.push_back(Kind{.horse = key == "horse_yard", .cow = key == "cattle_yard"});
    }
  }

  /// @brief One day of the chairman's attention. Call once a day, after the
  /// day's steps.
  /// @param catalog The district's lots (LimitPolicy's parsed catalogue), or
  ///        nullptr — then «buy feed» is not answered, as before 0.37.142.
  void RunDay(core::ISimulation& simulation, const core::LimitCatalog* catalog = nullptr) {
    const core::WorldState& world = simulation.CompletedState();
    NoteTheDay(world);
    std::vector<core::Alarm> alarms;
    simulation.CollectAlarms(alarms);
    const core::Alarm* yellow = nullptr;
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kHerdHayShortAhead) {
        yellow = &alarm;
        break;
      }
    }
    if (yellow == nullptr) {
      return;
    }
    // EVERY YELLOW DAY IS COUNTED AND EVERY ONE IS ANSWERED. Until 0.37.142
    // an answer was followed by a day not looked at («one order a recount»)
    // and not counted either. The day is not needed: the orders staged this
    // evening settle in tomorrow's steps, and tomorrow evening's lamp is read
    // after them. With it the lamp stood two days for every answer — 11-15
    // days a village a year on the second form of 0.37.142.
    ++yellow_days_;
    YearOf(world).yellow_days += 1;
    const bool heads_advised = yellow->advice == core::AlarmAdvice::kReduceHerd ||
                               yellow->advice_more == core::AlarmAdvice::kReduceHerd;
    if (yellow->below_floor_stock + yellow->below_floor_horses > 0) {
      ++floor_days_;
      YearOf(world).below_floor_days += 1;
    }
    if (yellow->advice == core::AlarmAdvice::kBuyFeed ||
        yellow->advice == core::AlarmAdvice::kGranaryForFeed) {
      YearOf(world).feed_advice_days += 1;
    }
    std::vector<core::OrderRow> orders;
    if (yellow->advice == core::AlarmAdvice::kCutHay) {
      RushTheCut(world, orders);
    } else {
      // «BUY FEED»: the amount the lamp names, in lots. Until 0.37.142's
      // fourth form the amount was the LARGEST purchase the door would take
      // and the chairman bought one lot an answer instead; it is the least
      // that feeds the herds now, and he buys it whole.
      if (yellow->advice == core::AlarmAdvice::kBuyFeed && catalog != nullptr) {
        BuyFeed(simulation, *catalog, yellow->advice_resource, yellow->advice_amount, orders);
      }
      if (heads_advised) {
        HandOver(world, *yellow, orders);
      }
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  /// @brief What the answer did, for a run to print.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": the hay's yellow stood " << yellow_days_
              << " days; the chairman rushed " << rushes_ << " meadow cuts and handed "
              << horses_handed_ << " horses and " << cows_handed_ << " cows in " << hand_orders_
              << " orders to the district as the lamp's advice named them (the core takes the "
                 "oldest), and bought "
              << feed_lots_ << " lots of feed on its «buy feed»; on " << floor_days_
              << " days the advice named heads below the floors, and none of those was handed; "
                 "the fewest it saw: "
              << least_cows_ << " cows, " << least_horses_ << " horses (adults, the kolkhoz's)\n";
    // BY YEAR, one line each, for a reader that takes lines by their prefix:
    // the yellow's days, the heads handed, the days the advice went below
    // the floors, the days it was «buy feed» or «the granary» (not answered
    // here), and the adults standing on the year's last day noted.
    for (std::size_t year = 0; year < years_.size(); ++year) {
      const YearCount& count = years_[year];
      std::cout << "YA,year=" << (year + 1) << ",yellow_days=" << count.yellow_days
                << ",horses_handed=" << count.horses_handed << ",cows_handed=" << count.cows_handed
                << ",below_floor_days=" << count.below_floor_days
                << ",feed_advice_days=" << count.feed_advice_days
                << ",feed_lots=" << count.feed_lots << ",horses_named=" << count.horses_named
                << ",adult_horses_end=" << count.adult_horses
                << ",adult_cows_end=" << count.adult_cows << '\n';
    }
  }

 private:
  struct Kind {
    bool horse = false;
    bool cow = false;
  };

  struct YearCount {
    std::uint32_t yellow_days = 0;
    std::uint32_t below_floor_days = 0;
    std::uint32_t feed_advice_days = 0;
    std::uint32_t feed_lots = 0;
    std::uint64_t horses_handed = 0;
    std::uint64_t cows_handed = 0;
    /// The horses the advice named on the days looked at, handed or not.
    std::uint64_t horses_named = 0;
    std::int64_t adult_horses = 0;
    std::int64_t adult_cows = 0;
  };

  YearCount& YearOf(const core::WorldState& world) {
    const std::size_t year = world.calendar.day / core::kDaysPerYear;
    if (years_.size() <= year) {
      years_.resize(year + 1U);
    }
    return years_[year];
  }

  /// The kolkhoz's adults of the kind `pick` selects.
  template <typename Pick>
  std::int64_t Adults(const core::WorldState& world, Pick pick) const {
    std::int64_t adults = 0;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.kind.value < kinds_.size() &&
          pick(kinds_[herd.kind.value])) {
        adults += herd.adult_count;
      }
    }
    return adults;
  }

  static bool IsHorse(const Kind& kind) { return kind.horse; }

  static bool IsCow(const Kind& kind) { return kind.cow; }

  /// The fewest adults seen, and the adults of the year's last day noted.
  void NoteTheDay(const core::WorldState& world) {
    const std::int64_t cows = Adults(world, IsCow);
    const std::int64_t horses = Adults(world, IsHorse);
    least_cows_ = noted_any_ ? std::min(least_cows_, cows) : cows;
    least_horses_ = noted_any_ ? std::min(least_horses_, horses) : horses;
    noted_any_ = true;
    YearCount& year = YearOf(world);
    year.adult_horses = horses;
    year.adult_cows = cows;
  }

  /// The rush on EVERY meadow in its cut with mowing left that is not rushed
  /// to the top on this cut — the lamp's own condition of «the cut»
  /// (CutCanBeHurried: a rush counts only on the phase it was declared in).
  /// One meadow an answer until 0.37.142's third form: seven yellow days a
  /// village in year 3 were the meadows taken one by one.
  void RushTheCut(const core::WorldState& world, std::vector<core::OrderRow>& orders) {
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      const bool rushed = field.rush_phase == field.phase && field.rush_step >= core::kMaxRushStep;
      // EVERY LAND THAT IS NOT ARABLE, as the lamp asks it — the floodplain
      // meadow too. Until 0.37.142's fourth form only LandKind::kMeadow was
      // rushed, and the lamp said «the cut» for a week over the floodplain.
      if (field.kind == core::LandKind::kArable || field.phase != core::FieldPhase::kHarvest ||
          !(field.work_days_remaining > 0.0F) || rushed) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kDeclareRush;
      order.field = world.fields.row_ids[row];
      order.amount = core::kMaxRushStep;
      orders.push_back(order);
      ++rushes_;
    }
  }

  /// `heads` of the kolkhoz herds `pick` selects, herd by herd in row order —
  /// an order a herd. A cow herd is asked for its adults; a HORSE herd for any
  /// of its heads, because the door hands a horse herd's young above what it
  /// keeps first and its adults only while the lamp has been dark a year
  /// (district_limit.cpp, OrderHandStock) — the advice counted the horses in
  /// that same order. Returns the heads put into orders.
  template <typename Pick>
  std::int64_t Hand(const core::WorldState& world,
                    Pick pick,
                    std::int64_t heads,
                    std::vector<core::OrderRow>& orders) {
    std::int64_t handed = 0;
    for (std::uint32_t row = 0; row < world.herds.rows.size() && handed < heads; ++row) {
      const core::HerdRow& herd = world.herds.rows[row];
      if (herd.household_owned != 0 || herd.kind.value >= kinds_.size() ||
          !pick(kinds_[herd.kind.value])) {
        continue;
      }
      const std::int64_t young =
          kinds_[herd.kind.value].horse ? herd.juvenile_count + herd.newborn_count : 0;
      const std::int64_t askable = static_cast<std::int64_t>(herd.adult_count) + young;
      if (askable == 0) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kHandStock;
      order.herd = world.herds.row_ids[row];
      order.amount = std::min<std::int64_t>(heads - handed, askable);
      handed += order.amount;
      orders.push_back(order);
      ++hand_orders_;
    }
    return handed;
  }

  /// `grams` of `feed` in lots of the first lot carrying it that the door
  /// would take today (the catalogue's order), rounded up to whole lots and
  /// no more of them than the year's points cover.
  void BuyFeed(core::ISimulation& simulation,
               const core::LimitCatalog& catalog,
               core::ResourceId feed,
               core::Grams grams,
               std::vector<core::OrderRow>& orders) {
    const core::LimitBook book = simulation.OfficeLimit();
    for (const core::LimitLotLine& line : book.catalogue) {
      if (line.lot.value >= catalog.lots.size() || line.orderable != core::OrderRefusal::kNone) {
        continue;
      }
      const core::ResourceAmounts& goods = catalog.lots[line.lot.value].goods;
      if (feed.value >= goods.size() || goods[feed.value] <= 0) {
        continue;
      }
      const core::Grams a_lot = goods[feed.value];
      std::int64_t lots = std::max<core::Grams>(1, (grams + a_lot - 1) / a_lot);
      if (line.points > 0) {
        lots = std::min<std::int64_t>(lots, book.points / line.points);
      }
      for (std::int64_t index = 0; index < lots; ++index) {
        core::OrderRow order;
        order.kind = core::OrderKind::kOrderLimitLot;
        order.lot = line.lot;
        orders.push_back(order);
        ++feed_lots_;
        YearOf(simulation.CompletedState()).feed_lots += 1;
      }
      return;
    }
  }

  /// The heads the advice names above the floors, into orders.
  void HandOver(const core::WorldState& world,
                const core::Alarm& yellow,
                std::vector<core::OrderRow>& orders) {
    YearCount& year = YearOf(world);
    year.horses_named +=
        static_cast<std::uint64_t>(std::max<std::int64_t>(0, yellow.hand_over_horses));
    const std::int64_t horses = Hand(world, IsHorse, yellow.hand_over_horses, orders);
    const std::int64_t cows = Hand(world, IsCow, yellow.hand_over_stock, orders);
    horses_handed_ += static_cast<std::uint64_t>(horses);
    cows_handed_ += static_cast<std::uint64_t>(cows);
    year.horses_handed += static_cast<std::uint64_t>(horses);
    year.cows_handed += static_cast<std::uint64_t>(cows);
  }

  /// By LivestockKindId.
  std::vector<Kind> kinds_;

  /// By year of the run, 0-based.
  std::vector<YearCount> years_;

  bool noted_any_ = false;

  std::int64_t least_cows_ = 0;

  std::int64_t least_horses_ = 0;

  std::uint32_t yellow_days_ = 0;

  std::uint32_t floor_days_ = 0;

  std::uint32_t rushes_ = 0;

  std::uint32_t hand_orders_ = 0;

  std::uint32_t feed_lots_ = 0;

  std::uint64_t horses_handed_ = 0;

  std::uint64_t cows_handed_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_ANSWER_H_
