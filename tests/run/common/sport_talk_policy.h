/// @file
/// @brief The run's chairman talks to the drinker (#1238; econ, master-eye-636
///        §3a; boss, core-boss-1238-sport-talk [1], [2]): once a calendar
///        season, kTalkToSport to the most drinking man who can go.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN ORDERS THIS. The canon ordered no talk at all (#636's first
/// print: 0 in 90 village-years), so the master's eye's talk half and the
/// talk's own effect on drinking were measured nowhere.
///
/// THE RULE, once a day after the day's steps:
///   * eligible: a man (the door judges his age), no talk running
///     (talk_until_day), alcoholism above the drinking rule's line - food.csv
///     plot_drinker_alcoholism_threshold, 50, read off the table (boss [2]
///     (a): crime's threshold has no reader in the core);
///   * the most drinking eligible man not set aside is asked;
///   * THE DOOR JUDGES THE PLACE (a stadium or the reading hut in reach,
///     kNowhereToGo) and the season (kOncePerSeason); the run copies neither
///     rule. It reads the answer off the state: a talk that took sets the
///     man's talk_until_day and the chairman's last_talk_season by the next
///     day. One that did not sets that man aside for a season's days
///     (kSeasonDays), and the next is asked the next day;
///   * after a talk that took, nobody is asked for kSeasonDays: the next day
///     the door could take is in the next season at the earliest.
/// THE INSTRUMENT (Report and the ST lines): each talk ordered and taken;
/// econ's known-direction print - the talked man's alcoholism a season after
/// against at the talk, which MUST fall: «DOOR DEAD» when it does not; and
/// per closed year the talks taken, the asks refused, the eligible count -
/// «NOT MEASURED: no eligible man» when it is nought.
#ifndef TESTS_RUN_COMMON_SPORT_TALK_POLICY_H_
#define TESTS_RUN_COMMON_SPORT_TALK_POLICY_H_

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>
#include <unordered_map>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Orders the season's talk to the most drinking man who can go.
class SportTalkPolicy {
 public:
  /// @brief Reads the drinking rule's line off the core's own row (food.csv
  ///        plot_drinker_alcoholism_threshold, 50; boss [2] (a)), so the run
  ///        and the household plot judge «a drinker» by one number.
  explicit SportTalkPolicy(const core::ITableSet& tables) {
    if (const core::ITable* const food = tables.FindTable("food")) {
      const std::uint32_t row = food->FindRowByKey("plot_drinker_alcoholism_threshold");
      const std::uint32_t column = food->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        drinker_line_ = food->CellReal(row, column).value_or(drinker_line_);
      }
    }
  }

  /// @brief A season's days: three months of four.
  static constexpr core::SimDay kSeasonDays = 3U * core::kDaysPerMonth;

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay today = world.calendar.day;
    ReadYesterdaysAsk(world, today);
    FollowUp(world, today);
    CloseYear(world, today);
    eligible_today_ = 0;
    if (today < quiet_until_) {
      CountEligible(world, today);
      return;
    }
    const std::uint32_t row = MostDrinkingMan(world, today);
    if (row == core::kNoRow) {
      return;
    }
    core::OrderRow order;
    order.kind = core::OrderKind::kTalkToSport;
    order.status = core::OrderStatus::kPending;
    order.resident = world.residents.row_ids[row];
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    asked_ = order.resident;
    asked_alcoholism_ = world.residents.rows[row].alcoholism;
    asked_season_ = world.chairman.last_talk_season;
    asked_day_ = today;
  }

 private:
  struct Follow {
    core::SimDay due = 0;
    float before = 0.0F;
  };

  bool Eligible(const core::ResidentRow& man, core::SimDay today) const {
    return man.sex == core::Sex::kMale && man.alcoholism > drinker_line_ &&
           !(man.talk_until_day != 0 && today <= man.talk_until_day);
  }

  void CountEligible(const core::WorldState& world, core::SimDay today) {
    for (const core::ResidentRow& man : world.residents.rows) {
      eligible_today_ += Eligible(man, today) ? 1U : 0U;
    }
    eligible_year_max_ = std::max(eligible_year_max_, eligible_today_);
  }

  std::uint32_t MostDrinkingMan(const core::WorldState& world, core::SimDay today) {
    std::uint32_t best = core::kNoRow;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& man = world.residents.rows[row];
      if (!Eligible(man, today)) {
        continue;
      }
      ++eligible_today_;
      const auto set_aside = set_aside_until_.find(world.residents.row_ids[row].value);
      if (set_aside != set_aside_until_.end() && today < set_aside->second) {
        continue;
      }
      if (best == core::kNoRow || man.alcoholism > world.residents.rows[best].alcoholism) {
        best = row;
      }
    }
    eligible_year_max_ = std::max(eligible_year_max_, eligible_today_);
    return best;
  }

  /// The answer to yesterday's ask, off the state: taken, or set aside.
  void ReadYesterdaysAsk(const core::WorldState& world, core::SimDay today) {
    if (asked_.value == core::kInvalidEntityIdValue || asked_day_ == today) {
      return;
    }
    const core::ResidentId man = asked_;
    asked_ = core::ResidentId{};
    const std::uint32_t row = core::FindRow(world.residents, man);
    const bool taken = row != core::kNoRow && world.chairman.last_talk_season != asked_season_ &&
                       world.residents.rows[row].talk_until_day >= today;
    if (!taken) {
      ++refused_year_;
      set_aside_until_[man.value] = today + kSeasonDays;
      return;
    }
    ++taken_year_;
    quiet_until_ = asked_day_ + kSeasonDays;
    follow_[man.value] = Follow{.due = asked_day_ + kSeasonDays, .before = asked_alcoholism_};
    std::printf("ST,y=%u,d=%u,man=%u,alcoholism=%.1f,result=taken\n",
                (asked_day_ / core::kDaysPerYear) + 1U,
                asked_day_ % core::kDaysPerYear,
                man.value,
                static_cast<double>(asked_alcoholism_));
  }

  /// ECON'S KNOWN DIRECTION: a season after, the man's alcoholism must be
  /// below what it was at the talk.
  void FollowUp(const core::WorldState& world, core::SimDay today) {
    for (auto it = follow_.begin(); it != follow_.end();) {
      if (today < it->second.due) {
        ++it;
        continue;
      }
      const std::uint32_t row = core::FindRow(world.residents, core::ResidentId{it->first});
      if (row != core::kNoRow) {
        const float after = world.residents.rows[row].alcoholism;
        const bool fell = after < it->second.before;
        std::printf("STAFTER,man=%u,before=%.1f,after=%.1f,%s\n",
                    it->first,
                    static_cast<double>(it->second.before),
                    static_cast<double>(after),
                    fell ? "fell" : "DOOR DEAD: did not fall");
        fell_ += fell ? 1U : 0U;
        did_not_fall_ += fell ? 0U : 1U;
      }
      it = follow_.erase(it);
    }
  }

  void CloseYear(const core::WorldState& world, core::SimDay today) {
    (void)world;
    if (today == 0 || today % core::kDaysPerYear != 0 || today == closed_day_) {
      return;
    }
    closed_day_ = today;
    std::printf("STY,y=%u,taken=%u,refused=%u,eligible_max=%u%s\n",
                today / core::kDaysPerYear,
                taken_year_,
                refused_year_,
                eligible_year_max_,
                eligible_year_max_ == 0 ? ",NOT MEASURED: no eligible man" : "");
    taken_year_ = refused_year_ = eligible_year_max_ = 0;
  }

 public:
  /// @brief The run's last word: the known direction's count.
  void Report() const {
    std::printf(
        "sport_talk: a season after the talk, alcoholism fell for %u men and did not for "
        "%u%s\n",
        fell_,
        did_not_fall_,
        fell_ + did_not_fall_ == 0 ? " - NOT MEASURED: no talk taken"
        : did_not_fall_ > 0        ? " - DOOR DEAD for some"
                                   : "");
  }

 private:
  float drinker_line_ = 50.0F;
  core::ResidentId asked_;
  float asked_alcoholism_ = 0.0F;
  std::uint32_t asked_season_ = 0;
  core::SimDay asked_day_ = 0;
  core::SimDay quiet_until_ = 0;
  core::SimDay closed_day_ = 0;
  std::unordered_map<std::uint32_t, core::SimDay> set_aside_until_;
  std::unordered_map<std::uint32_t, Follow> follow_;
  std::uint32_t eligible_today_ = 0;
  std::uint32_t eligible_year_max_ = 0;
  std::uint32_t taken_year_ = 0;
  std::uint32_t refused_year_ = 0;
  std::uint32_t fell_ = 0;
  std::uint32_t did_not_fall_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SPORT_TALK_POLICY_H_
