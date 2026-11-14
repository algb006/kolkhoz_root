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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
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
    cow_kind_ = livestock->FindRowByKey("cow");
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      hay_resource_ = resources->FindRowByKey("hay");
    }
  }

  /// @brief One day of the chairman's attention. Call once a day, after the
  /// day's steps.
  /// @param alarms The completed state's alarms, collected ONCE a day by the
  ///        caller and shared with its other reader (#700: each collection
  ///        cost ~0.4 s at year 29, and the limit policy made two a day).
  /// @param catalog The district's lots (LimitPolicy's parsed catalogue), or
  ///        nullptr — then «buy feed» is not answered, as before 0.37.142.
  void RunDay(core::ISimulation& simulation,
              const std::vector<core::Alarm>& alarms,
              const core::LimitCatalog* catalog = nullptr) {
    const core::WorldState& world = simulation.CompletedState();
    NoteTheDay(world);
    NoteTheCut(world);
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
    // A DATED MOVE IS ANOTHER DAY'S (Alarm::advice_day, 0.37.250): he waits
    // for its day - a day off still ahead, the heads named for the day after
    // the cut closes.
    if (yellow->advice_day != 0) {
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
      YearCount& year = YearOf(world);
      year.below_floor_days += 1;
      const std::int64_t pair = yellow->below_floor_stock + yellow->below_floor_horses;
      year.below_floor_heads_sum += static_cast<std::uint64_t>(pair);
      year.below_after_feed_sum += static_cast<std::uint64_t>(yellow->below_floor_after_feed);
      year.below_floor_heads_max = std::max(year.below_floor_heads_max, pair);
      year.below_after_feed_max =
          std::max(year.below_after_feed_max, yellow->below_floor_after_feed);
    }
    if (yellow->advice == core::AlarmAdvice::kBuyFeed ||
        yellow->advice == core::AlarmAdvice::kGranaryForFeed) {
      YearOf(world).feed_advice_days += 1;
    }
    // #1573: «buy feed» with no head beside it (the cheap rung, or the feed
    // rung with nobody above the floors), and the lit days with no move.
    if (yellow->advice == core::AlarmAdvice::kBuyFeed &&
        yellow->advice_more == core::AlarmAdvice::kNone) {
      YearOf(world).buy_no_heads_days += 1;
    }
    if (yellow->advice == core::AlarmAdvice::kNone) {
      YearOf(world).no_move_days += 1;
    }
    // #1573's horse tail (econ [1] §3.3): the lit day by what it named - the
    // tolerance's silence, a feed move, heads.
    if (yellow->advice_tolerated != 0) {
      YearOf(world).tolerated_days += 1;
    } else if (yellow->advice == core::AlarmAdvice::kBuyFeed ||
               yellow->advice == core::AlarmAdvice::kGranaryForFeed) {
      YearOf(world).feed_move_days += 1;
    }
    if (heads_advised) {
      YearOf(world).heads_days += 1;
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
      // NOT WHILE THE CHAIRMAN IS SETTING THE HERDS' PLANS (#1289; herd_plan_
      // policy.h): its evenings set plans in steps, and a plan of the first
      // step read as the advice's would hand over heads the last step keeps.
      if (heads_advised && !plans_being_set_ && !hand_over_off_) {
        HandOver(world, *yellow, orders);
        GuardTheHandOver(world, *yellow, CowFloor(simulation));
      }
    }
    LogTheDay(simulation, *yellow);
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders), {});
    }
  }

  /// #1570: one cow hand-over the lamp's answer ordered - the day, the cows,
  /// and the forecast it answered (kHerdHayShortAhead's heads short on its
  /// worst day, the feed that runs out first, the days to that loss).
  struct HandLog {
    core::SimDay day = 0;
    std::int64_t cows = 0;
    std::int64_t short_heads = 0;
    core::ResourceId first_short;
    std::int64_t days_to_loss = 0;
  };

  /// @brief Every cow hand-over the answer ordered, in order (#1570's print).
  const std::vector<HandLog>& HandLogs() const { return hand_log_; }

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

  /// @brief #1573's answer line, a village: «F1573A seed S | y1 yellow Y
  ///        buy_no_heads B no_move N feed_lots L feed_points P cows_handed C
  ///        horses_handed H | y2 ...», y1-`years`.
  /// @brief #1689: «HAY1689 seed S | y1 below_days D heads_no_buy_mean M max X
  /// heads_after_feed_mean
  ///        A max B | y2 ...» - the lamp's below-floor heads with no purchase and with the maximal
  ///        purchase counted (Alarm::below_floor_after_feed), y1-`years`.
  void PrintBelowFloor(std::ostream& out, std::uint64_t seed, std::uint32_t years) const {
    out << "population_curve: HAY1689 seed " << seed;
    for (std::uint32_t year = 0; year < years; ++year) {
      const YearCount count = year < years_.size() ? years_[year] : YearCount{};
      const double days = count.below_floor_days > 0 ? count.below_floor_days : 1.0;
      out << " | y" << year + 1 << " below_days " << count.below_floor_days << " heads_no_buy_mean "
          << static_cast<double>(count.below_floor_heads_sum) / days << " max "
          << count.below_floor_heads_max << " heads_after_feed_mean "
          << static_cast<double>(count.below_after_feed_sum) / days << " max "
          << count.below_after_feed_max;
    }
    out << '\n';
  }

  void PrintFeedRung(std::ostream& out, std::uint64_t seed, std::uint32_t years) const {
    out << "population_curve: F1573A seed " << seed;
    for (std::uint32_t year = 0; year < years; ++year) {
      const YearCount count = year < years_.size() ? years_[year] : YearCount{};
      out << " | y" << year + 1 << " yellow " << count.yellow_days << " buy_no_heads "
          << count.buy_no_heads_days << " no_move " << count.no_move_days << " feed_lots "
          << count.feed_lots << " feed_points " << count.feed_points << " cows_handed "
          << count.cows_handed << " horses_handed " << count.horses_handed;
    }
    out << '\n';
  }

  /// @brief #1573's horse tail, the lamp's half (econ [1] §3.2-3.4): «HAY1573T seed S | y1 yellow Y
  ///        tolerated T feed_move F heads H horses_first A horses_behind_feed B feed_lots L
  ///        feed_points P cows_handed C | y2 ...», y1-`years`.
  void PrintHorseTail(std::ostream& out, std::uint64_t seed, std::uint32_t years) const {
    out << "population_curve: HAY1573T seed " << seed;
    for (std::uint32_t year = 0; year < years; ++year) {
      const YearCount count = year < years_.size() ? years_[year] : YearCount{};
      out << " | y" << year + 1 << " yellow " << count.yellow_days << " tolerated "
          << count.tolerated_days << " feed_move " << count.feed_move_days << " heads "
          << count.heads_days << " horses_first " << count.horses_handed_first
          << " horses_behind_feed " << count.horses_handed_behind_feed << " feed_lots "
          << count.feed_lots << " feed_points " << count.feed_points << " cows_handed "
          << count.cows_handed;
    }
    out << '\n';
  }

  /// @brief Holds the hand-over while the herds' plans are being set (#1289).
  void HoldHandOverForPlans(bool hold) { plans_being_set_ = hold; }

  /// @brief #1697's by-day print (econ, ladder proportion [1]): every lit day of
  ///        years kLogFirstYear..kLogLastYear - «LADDER1697 seed S y Y d D advice A more M
  ///        short_heads N days_to_loss L hand_stock C hand_horses H below_stock B
  ///        cow_floor F cows K horses R handed_cows X handed_horses Z hay_store_t T» - and
  ///        a year's eve of the first cut, «LADDERCUT1697 seed S y Y d D hay_store_t T» (the
  ///        hay in every store the day before the first non-arable field opened its cut).
  /// @brief #1697's guard, a village, y1-y16: «HAYL1697 seed S events_first E cows_first C
  ///        events_behind E cows_behind C below_floor_events B below_floor_heads H cap_breaches
  ///        K below WHERE cap WHERE» - every count beside its events; a breach SHOUTS (CAP1697).
  void PrintGuard(std::ostream& out, std::uint64_t seed) const {
    const auto join = [](const std::vector<std::string>& notes) {
      std::string text;
      for (const std::string& note : notes) {
        text += (text.empty() ? "" : ",") + note;
      }
      return text.empty() ? std::string("-") : text;
    };
    out << "population_curve: HAYL1697 seed " << seed << " events_first " << guard_.events_first
        << " cows_first " << guard_.cows_first << " events_behind " << guard_.events_behind
        << " cows_behind " << guard_.cows_behind << " below_floor_events "
        << guard_.below_floor_events << " below_floor_heads " << guard_.below_floor_heads
        << " cap_breaches " << guard_.cap_breaches << " below " << join(guard_.below_notes)
        << " cap " << join(guard_.cap_notes) << '\n';
    if (guard_.below_floor_events + guard_.cap_breaches > 0) {
      out << "population_curve: CAP1697 SEED " << seed << " THE LADDER HANDED "
          << guard_.below_floor_events << " TIMES UNDER THE MILK FLOOR AND PAST ITS SEASON CAP "
          << guard_.cap_breaches << " TIMES\n";
    }
  }

  void PrintLadderDays(std::ostream& out, std::uint64_t seed) const {
    for (const std::string& line : day_log_) {
      out << "population_curve: " << line.substr(0, line.find(' ')) << " seed " << seed
          << line.substr(line.find(' ')) << '\n';
    }
  }

  /// @brief #1570's counterfactual arm (--hay-hand-over=off): the lamp's
  ///        «reduce herd» is never answered, so whether the herds it named
  ///        would have gone short can be read. Off by default: the canon hands.
  void SetHandOverOff(bool off) { hand_over_off_ = off; }

 private:
  bool hand_over_off_ = false;
  bool plans_being_set_ = false;

  struct Kind {
    bool horse = false;
    bool cow = false;
  };

  struct YearCount {
    std::uint32_t yellow_days = 0;
    std::uint32_t below_floor_days = 0;
    /// #1689: on the below-floor days, the heads named below the floors with
    /// no purchase (the pair) and with the maximal purchase, summed and at most.
    std::uint64_t below_floor_heads_sum = 0;
    std::uint64_t below_after_feed_sum = 0;
    std::int64_t below_floor_heads_max = 0;
    std::int64_t below_after_feed_max = 0;
    std::uint32_t feed_advice_days = 0;
    std::uint32_t feed_lots = 0;
    /// #1573: the points of the feed lots ordered (the door may still refuse
    /// one: ordered, not spent), the «buy, no head» days, the no-move days.
    std::uint32_t feed_points = 0;
    std::uint32_t buy_no_heads_days = 0;
    std::uint32_t no_move_days = 0;
    /// #1573's horse tail: lit days inside the tolerance, with a feed move,
    /// with heads named (first or behind the feed); horses handed by the
    /// heads rung named first, and behind a feed move.
    std::uint32_t tolerated_days = 0;
    std::uint32_t feed_move_days = 0;
    std::uint32_t heads_days = 0;
    std::uint64_t horses_handed_first = 0;
    std::uint64_t horses_handed_behind_feed = 0;
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
        YearOf(simulation.CompletedState()).feed_points += static_cast<std::uint32_t>(line.points);
      }
      return;
    }
  }

  static constexpr std::uint32_t kLogFirstYear = 7;  ///< #1697's print, 1-based
  static constexpr std::uint32_t kLogLastYear = 10;

  static std::uint32_t YearNumber(const core::WorldState& world) {
    return (world.calendar.day / core::kDaysPerYear) + 1U;
  }

  core::Grams HayStore(const core::WorldState& world) const {
    core::Grams hay = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      hay += hay_resource_ < unit.stock.size() ? unit.stock[hay_resource_] : 0;
    }
    return hay;
  }

  /// #1697: the store the day before the year's first cut opened on non-arable land.
  void NoteTheCut(const core::WorldState& world) {
    const std::uint32_t year = YearNumber(world);
    const core::Grams store = HayStore(world);
    const bool cutting = std::ranges::any_of(world.fields.rows, [](const core::FieldRow& field) {
      return field.kind != core::LandKind::kArable && field.phase == core::FieldPhase::kHarvest;
    });
    if (cutting && season_year_ != year) {  // #1697's guard: a hay season runs cut to cut
      season_year_ = year;
      season_handed_ = 0;
      season_cap_ = -1;
    }
    if (cutting && cut_year_ != year && year >= kLogFirstYear && year <= kLogLastYear) {
      cut_year_ = year;
      day_log_.push_back("LADDERCUT1697 y " + std::to_string(year) + " d " +
                         std::to_string(world.calendar.day % core::kDaysPerYear) + " hay_store_t " +
                         std::to_string(yesterday_hay_ / 1000000));
    }
    yesterday_hay_ = store;
  }

  /// #1697: the milk floor as the herd plan window reads it (HerdPlanLine::floor_heads, the
  /// core's HerdFloors::cows_kept), the largest of the cow lines.
  std::int64_t CowFloor(const core::ISimulation& simulation) const {
    std::int64_t floor = 0;
    for (const core::HerdPlanLine& line : simulation.HerdPlans()) {
      floor =
          line.kind.value == cow_kind_ ? std::max<std::int64_t>(floor, line.floor_heads) : floor;
    }
    return floor;
  }

  /// #1697's guard (econ, econ-boss-1697-both-rungs [1]): an evening's hand-over by its route
  /// (heads named first / behind a feed move), whether it leaves the adult cows under the milk
  /// floor, and the season's heads against a cap of ceil(max(1, the season's first lit
  /// short_heads) x 1.25) - the lamp's worst-day unfed heads, a WIDER cap than econ's
  /// deficit-over-a-head's-need (the run cannot read a head's need); a guard, not a rule.
  void GuardTheHandOver(const core::WorldState& world,
                        const core::Alarm& yellow,
                        std::int64_t floor) {
    const std::uint32_t year = YearNumber(world);
    if (year > kGuardYears) {
      return;
    }
    const bool first = yellow.advice == core::AlarmAdvice::kReduceHerd;
    if (day_cows_ + day_horses_ > 0) {
      (first ? guard_.events_first : guard_.events_behind) += 1;
      (first ? guard_.cows_first : guard_.cows_behind) += day_cows_;
    }
    const std::int64_t adults_after = Adults(world, IsCow) - day_cows_;
    if (day_cows_ > 0 && adults_after < floor) {
      guard_.below_floor_events += 1;
      guard_.below_floor_heads += std::min(day_cows_, floor - adults_after);
      if (guard_.below_notes.size() < kGuardNotes) {
        guard_.below_notes.push_back("y" + std::to_string(year) + "d" +
                                     std::to_string(world.calendar.day % core::kDaysPerYear) + ":" +
                                     std::to_string(day_cows_) + "->" +
                                     std::to_string(adults_after) + "<" + std::to_string(floor));
      }
    }
    if (season_cap_ < 0) {
      const auto worst = static_cast<double>(std::max<std::int64_t>(1, yellow.amount));
      season_cap_ = static_cast<std::int64_t>(std::ceil(worst * kGuardCapShare));
    }
    season_handed_ += day_cows_ + day_horses_;
    if (season_handed_ > season_cap_ && season_breached_year_ != season_year_) {
      season_breached_year_ = season_year_;
      guard_.cap_breaches += 1;
      if (guard_.cap_notes.size() < kGuardNotes) {
        guard_.cap_notes.push_back("y" + std::to_string(year) + ":" +
                                   std::to_string(season_handed_) + ">" +
                                   std::to_string(season_cap_));
      }
    }
  }

  /// #1697: one lit day's line, after the answer's orders were staged.
  void LogTheDay(const core::ISimulation& simulation, const core::Alarm& yellow) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = YearNumber(world);
    const std::int64_t handed_cows = day_cows_;
    const std::int64_t handed_horses = day_horses_;
    day_cows_ = 0;
    day_horses_ = 0;
    if (year < kLogFirstYear || year > kLogLastYear) {
      return;
    }
    std::int64_t floor = 0;
    for (const core::HerdPlanLine& line : simulation.HerdPlans()) {
      floor =
          line.kind.value == cow_kind_ ? std::max<std::int64_t>(floor, line.floor_heads) : floor;
    }
    // Each kolkhoz cow herd: plan_heads/adults/young - the #1289 rung counts the young above
    // the plan, and the order hands adults.
    std::string herds;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.kind.value == cow_kind_) {
        herds += (herds.empty() ? "" : ",") +
                 (herd.plan_heads == core::kNoHerdPlan ? std::string("none")
                                                       : std::to_string(herd.plan_heads)) +
                 "/" + std::to_string(herd.adult_count) + "/" +
                 std::to_string(herd.juvenile_count + herd.newborn_count);
      }
    }
    day_log_.push_back(
        "LADDER1697 y " + std::to_string(year) + " d " +
        std::to_string(world.calendar.day % core::kDaysPerYear) + " advice " +
        std::to_string(static_cast<int>(yellow.advice)) + " more " +
        std::to_string(static_cast<int>(yellow.advice_more)) + " short_heads " +
        std::to_string(yellow.amount) + " days_to_loss " + std::to_string(yellow.days_to_loss) +
        " hand_stock " + std::to_string(yellow.hand_over_stock) + " hand_horses " +
        std::to_string(yellow.hand_over_horses) + " below_stock " +
        std::to_string(yellow.below_floor_stock) + " cow_floor " + std::to_string(floor) +
        " cows " + std::to_string(Adults(world, IsCow)) + " horses " +
        std::to_string(Adults(world, IsHorse)) + " handed_cows " + std::to_string(handed_cows) +
        " handed_horses " + std::to_string(handed_horses) + " hay_store_t " +
        std::to_string(HayStore(world) / 1000000) + " short_grams_t " +
        std::to_string(yellow.short_grams / 1000000) + " first_short " +
        std::to_string(yellow.first_short_resource.value) + " cow_herds_plan_adults_young " +
        (herds.empty() ? std::string("-") : herds));
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
    if (cows > 0) {
      hand_log_.push_back(HandLog{.day = world.calendar.day,
                                  .cows = cows,
                                  .short_heads = yellow.amount,
                                  .first_short = yellow.resource,
                                  .days_to_loss = yellow.days_to_loss});
    }
    day_cows_ = cows;
    day_horses_ = horses;
    horses_handed_ += static_cast<std::uint64_t>(horses);
    cows_handed_ += static_cast<std::uint64_t>(cows);
    year.horses_handed += static_cast<std::uint64_t>(horses);
    year.cows_handed += static_cast<std::uint64_t>(cows);
    (yellow.advice == core::AlarmAdvice::kReduceHerd ? year.horses_handed_first
                                                     : year.horses_handed_behind_feed) +=
        static_cast<std::uint64_t>(horses);
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

  std::vector<HandLog> hand_log_;

  /// #1697's print.
  std::uint32_t cow_kind_ = core::kNoTableRow;
  std::uint32_t hay_resource_ = core::kNoTableRow;
  std::uint32_t cut_year_ = 0;
  core::Grams yesterday_hay_ = 0;
  std::int64_t day_cows_ = 0;
  std::int64_t day_horses_ = 0;
  std::vector<std::string> day_log_;

  /// #1697's guard.
  static constexpr std::uint32_t kGuardYears = 16;
  static constexpr std::size_t kGuardNotes = 6;
  static constexpr double kGuardCapShare = 1.25;

  struct Guard {
    std::int64_t events_first = 0;
    std::int64_t cows_first = 0;
    std::int64_t events_behind = 0;
    std::int64_t cows_behind = 0;
    std::int64_t below_floor_events = 0;
    std::int64_t below_floor_heads = 0;
    std::int64_t cap_breaches = 0;
    std::vector<std::string> below_notes;
    std::vector<std::string> cap_notes;
  };

  Guard guard_;
  std::uint32_t season_year_ = 0;
  std::uint32_t season_breached_year_ = 0;
  std::int64_t season_handed_ = 0;
  std::int64_t season_cap_ = -1;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_ANSWER_H_
