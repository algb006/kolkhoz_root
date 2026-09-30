/// @file
/// @brief The run's chairman answers the herds' yellow stage «к укосу сена не
/// хватит» (kHerdHayShortAhead) as a living chairman would: more hands on the
/// cut while the meadows are being mown, and, when the cut is closed or the
/// hands are on it, heads handed back to the district by the fodder — down to
/// a floor of each kind, never below it.
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
/// BOSS'S RULE [29], and the three forms it replaced on the canon before the
/// push. «Hand the yellow's heads out of the cows every four days» sold the
/// cows 443 -> 7 (nine seeds' sum) by year 10. «Hand what stands over its
/// roof's places» answered the cold's question, not the hunger's. «The
/// horses beyond the team's need, then the cows» ([27]) sold every cow
/// again — about forty a village, the milk failed in years 6-8 on all nine
/// seeds — because the need was read off the peak of the harnessed
/// assignment-days, which grows with the village, and no horse was ever
/// beyond it (0 handed in thirty years) while the team bred 16 -> 95. The
/// measure is the FODDER, and each kind has a FLOOR:
/// - WHEN: the yellow stands, and the cut is closed or every meadow in its
///   cut is rushed already; at most one order a day, and the next only if
///   the forecast after it still falls short;
/// - HOW MANY: the fewest heads whose ration covers the shortfall — the
///   yellow's `amount` is heads of its subject herd unfed on the worst day,
///   so the shortfall is that many of the subject kind's rations
///   (livestock.csv `feed_units_per_real_day`); at most half the adults an
///   order, and never below the kind's floor;
/// - WHOM, in this order:
///   1. the horses beyond the peak of the harnessed assignment-days of one
///      day over the last game year (TractionWatch::week_harnessed) — it
///      hardly ever fires, and stays;
///   2. THE COWS OVER THE MILK PLAN'S FLOOR: the HIGHEST milk position the
///      district has named over a cow's yield of the last closed year,
///      times kMilkFloorMargin (STUB). The highest, not this year's: the
///      position follows the herd down, a floor read off this year's
///      followed it too, and the cows went 53 -> 7 with every verdict
///      green. No closed year to read the yield from — no cow is handed;
///   3. THE HORSES OVER THE PLOUGHING'S FLOOR: the teams that plough the
///      plan's base of worked hectares (PlanState::worked_ha_last_year;
///      all the arable before the base is written) in the window —
///      hectares x field_phases.csv plowing `labor_days_per_ha` (real
///      man-days, / 7 a game day, a horse to a ploughman) over
///      kPloughWindowDays (STUB: two spring months);
///   4. the forecast still short — nothing more is handed, and the hunger
///      decides, as it did before this answer.
///
/// Called by LimitPolicy::RunDay (the district's policy — the hand-over is
/// the district's deal), so every run and host's canon that runs LimitPolicy
/// answers the yellow without being wired again.
#ifndef TESTS_RUN_COMMON_HAY_ANSWER_H_
#define TESTS_RUN_COMMON_HAY_ANSWER_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief One day's answer to the herds' hay yellow, if it stands.
class HayAnswer {
 public:
  explicit HayAnswer(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      milk_ = resources->FindRowByKey("milk");
    }
    if (const core::ITable* const phases = tables.FindTable("field_phases")) {
      const std::uint32_t row = phases->FindRowByKey("plowing");
      const std::uint32_t column = phases->FindColumn("labor_days_per_ha");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        plough_days_per_ha_ = phases->CellReal(row, column).value_or(0.0F);
      }
    }
    const core::ITable* livestock = tables.FindTable("livestock");
    if (livestock == nullptr) {
      return;
    }
    const std::uint32_t home = livestock->FindColumn("home_unit");
    const std::uint32_t ration = livestock->FindColumn("feed_units_per_real_day");
    for (std::uint32_t row = 0; row < livestock->RowCount(); ++row) {
      const std::string_view key =
          home == core::kNoTableColumn ? std::string_view{} : livestock->CellText(row, home);
      const std::optional<float> units = ration == core::kNoTableColumn
                                             ? std::optional<float>{}
                                             : livestock->CellReal(row, ration);
      kinds_.push_back(Kind{.horse = key == "horse_yard",
                            .cow = key == "cattle_yard",
                            .ration = units && *units > 0.0F ? *units : 0.0F});
    }
  }

  /// @brief One day of the chairman's attention. Call once a day, after the
  /// day's steps.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    NoteTheDay(world);
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
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
    ++yellow_days_;
    core::OrderRow order;
    if (RushTheCut(world, order) || HandOver(world, *yellow, order)) {
      simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
      cooldown_ = kRecountDays;
    }
  }

  /// @brief What the answer did, for a run to print.
  void Report(std::string_view run_name) const {
    std::cout << run_name << ": the hay's yellow stood " << yellow_days_
              << " days; the chairman rushed " << rushes_ << " meadow cuts and handed "
              << horses_handed_ << " horses and " << cows_handed_ << " cows in " << hand_orders_
              << " orders to the district (the core takes the oldest); it stood at a floor "
              << floor_days_ << " days and handed nothing; the fewest it saw: " << least_cows_
              << " cows, " << least_horses_ << " horses (adults, the kolkhoz's)\n";
  }

 private:
  struct Kind {
    bool horse = false;
    bool cow = false;
    float ration = 0.0F;  ///< livestock.csv feed_units_per_real_day
  };

  /// One order a recount of the forecast: the core's is daily.
  static constexpr std::uint32_t kRecountDays = 1;

  /// STUB (boss [29]): the cows kept over what the milk position needs.
  static constexpr double kMilkFloorMargin = 1.25;

  /// STUB (boss [29]): the ploughing's window, game days — two spring
  /// months of four days.
  static constexpr double kPloughWindowDays = 8.0;

  /// Real man-days to a game day (CLAUDE.md §9).
  static constexpr double kRealDaysPerGameDay = 7.0;

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

  /// The day's harnessed assignment-days, kept for a game year; the cows'
  /// head-days of the open year; a cow's yield, when a year closes; the
  /// highest milk position named.
  void NoteTheDay(const core::WorldState& world) {
    const std::uint32_t day = world.calendar.day;
    if (day == last_noted_day_ && noted_any_) {
      return;
    }
    last_noted_day_ = day;
    harness_[day % core::kDaysPerYear] =
        world.traction_watch.week_harnessed[day % core::kHarnessWeekDays];
    const std::int64_t cows = Adults(world, IsCow);
    const std::int64_t horses = Adults(world, IsHorse);
    least_cows_ = noted_any_ ? std::min(least_cows_, cows) : cows;
    least_horses_ = noted_any_ ? std::min(least_horses_, horses) : horses;
    const std::uint16_t closed = world.ledger.closed.year;
    if (noted_any_ && closed != closed_year_) {
      // A year closed: its milk over its mean cows.
      const double mean_cows = cow_days_noted_ > 0 ? static_cast<double>(cow_days_) /
                                                         static_cast<double>(cow_days_noted_)
                                                   : 0.0;
      const double milk = milk_ < world.ledger.closed.herd_produce.size()
                              ? static_cast<double>(world.ledger.closed.herd_produce[milk_])
                              : 0.0;
      cow_yield_grams_ = mean_cows > 0.0 ? milk / mean_cows : 0.0;
      cow_days_ = 0;
      cow_days_noted_ = 0;
    }
    closed_year_ = closed;
    if (milk_ < world.plan.due.size()) {
      highest_due_ = std::max(highest_due_, static_cast<double>(world.plan.due[milk_]));
    }
    cow_days_ += cows;
    ++cow_days_noted_;
    noted_any_ = true;
  }

  /// The peak of the harnessed assignment-days of one day over the last
  /// game year, a horse for each.
  std::int64_t HarnessPeak() const {
    const float most = *std::max_element(harness_.begin(), harness_.end());
    return static_cast<std::int64_t>(std::ceil(most));
  }

  /// The cows the highest milk position needs (the file's step 2); every
  /// cow, while there is a position and no closed year to read a cow's
  /// yield from. The closed book's position stands in for the spring's
  /// before it is named.
  std::int64_t CowFloor(const core::WorldState& world) const {
    const auto at = [&](const core::ResourceAmounts& amounts) {
      return milk_ < amounts.size() ? static_cast<double>(amounts[milk_]) : 0.0;
    };
    const double due =
        std::max({highest_due_, at(world.plan.due), at(world.ledger.closed.plan_due)});
    if (!(due > 0.0)) {
      return 0;
    }
    if (!(cow_yield_grams_ > 0.0)) {
      return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(std::ceil(kMilkFloorMargin * due / cow_yield_grams_));
  }

  /// The teams that plough the arable in the window (the file's step 3).
  std::int64_t HorseFloor(const core::WorldState& world) const {
    double arable_ha = 0.0;
    for (const core::FieldRow& field : world.fields.rows) {
      if (field.kind == core::LandKind::kArable) {
        arable_ha += static_cast<double>(field.area_ga);
      }
    }
    // The plan's own base of worked hectares, when it has one: the canon's
    // arable is about 370 ha with 70 worked, and a floor off all of it stood
    // at 66 teams and ate the cows' hay.
    if (world.plan.worked_ha_last_year > 0.0F) {
      arable_ha = static_cast<double>(world.plan.worked_ha_last_year);
    }
    const double team_days =
        arable_ha * static_cast<double>(plough_days_per_ha_) / kRealDaysPerGameDay;
    return static_cast<std::int64_t>(std::ceil(team_days / kPloughWindowDays));
  }

  /// The rush on the first meadow in its cut that is not rushed to the top.
  bool RushTheCut(const core::WorldState& world, core::OrderRow& order) {
    for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
      const core::FieldRow& field = world.fields.rows[row];
      if (field.kind != core::LandKind::kMeadow || field.phase != core::FieldPhase::kHarvest ||
          field.rush_step >= core::kMaxRushStep) {
        continue;
      }
      order.kind = core::OrderKind::kDeclareRush;
      order.field = world.fields.row_ids[row];
      order.amount = core::kMaxRushStep;
      ++rushes_;
      return true;
    }
    return false;
  }

  /// The largest kolkhoz herd of the kind `pick` selects, or kNoRow.
  template <typename Pick>
  std::uint32_t LargestHerd(const core::WorldState& world, Pick pick) const {
    std::uint32_t best = core::kNoRow;
    std::uint16_t best_adults = 0;
    for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
      const core::HerdRow& herd = world.herds.rows[row];
      if (herd.household_owned != 0 || herd.kind.value >= kinds_.size() ||
          !pick(kinds_[herd.kind.value]) || herd.adult_count <= best_adults) {
        continue;
      }
      best = row;
      best_adults = herd.adult_count;
    }
    return best;
  }

  /// The hand-over by the fodder (the file's rule), down to the floors.
  bool HandOver(const core::WorldState& world, const core::Alarm& yellow, core::OrderRow& order) {
    if (yellow.amount <= 0 || yellow.herd.value == core::kInvalidEntityIdValue) {
      return false;
    }
    const std::uint32_t subject_row = core::FindRow(world.herds, yellow.herd);
    if (subject_row == core::kNoRow || world.herds.rows[subject_row].kind.value >= kinds_.size()) {
      return false;
    }
    // The shortfall in feed units a real day: the subject's rations unfed.
    const double short_units =
        static_cast<double>(yellow.amount) *
        static_cast<double>(kinds_[world.herds.rows[subject_row].kind.value].ration);
    if (!(short_units > 0.0)) {
      return false;
    }
    // The heads of the herd in `row` an order may take: what covers the
    // shortfall, at most half the herd's adults, at most `over` the floor.
    const auto heads_of = [&](std::uint32_t row, std::int64_t over) {
      if (row == core::kNoRow || over <= 0) {
        return std::int64_t{0};
      }
      const double ration = static_cast<double>(kinds_[world.herds.rows[row].kind.value].ration);
      if (!(ration > 0.0)) {
        return std::int64_t{0};
      }
      const auto cover = static_cast<std::int64_t>(std::ceil(short_units / ration));
      const auto half = static_cast<std::int64_t>(world.herds.rows[row].adult_count / 2U);
      return std::min({cover, half, over});
    };
    const std::int64_t horses = Adults(world, IsHorse);
    const std::int64_t cows = Adults(world, IsCow);
    const std::uint32_t team = LargestHerd(world, IsHorse);
    const std::uint32_t dairy = LargestHerd(world, IsCow);
    // 1. THE HORSES BEYOND THE HARNESS'S PEAK.
    std::int64_t heads = heads_of(team, horses - std::max(HarnessPeak(), HorseFloor(world)));
    if (heads > 0) {
      Hand(world, team, heads, order);
      horses_handed_ += static_cast<std::uint64_t>(heads);
      return true;
    }
    // 2. THE COWS OVER THE MILK PLAN'S FLOOR, the oldest first (the core's).
    const std::int64_t cow_floor = CowFloor(world);
    heads = cow_floor >= cows ? 0 : heads_of(dairy, cows - cow_floor);
    if (heads > 0) {
      Hand(world, dairy, heads, order);
      cows_handed_ += static_cast<std::uint64_t>(heads);
      return true;
    }
    // 3. THE HORSES OVER THE PLOUGHING'S FLOOR.
    heads = heads_of(team, horses - HorseFloor(world));
    if (heads > 0) {
      Hand(world, team, heads, order);
      horses_handed_ += static_cast<std::uint64_t>(heads);
      return true;
    }
    // 4. Both at their floors: the hunger decides.
    ++floor_days_;
    return false;
  }

  void Hand(const core::WorldState& world,
            std::uint32_t row,
            std::int64_t heads,
            core::OrderRow& order) {
    order.kind = core::OrderKind::kHandStock;
    order.herd = world.herds.row_ids[row];
    order.amount = heads;
    ++hand_orders_;
  }

  /// By LivestockKindId.
  std::vector<Kind> kinds_;

  /// resources.csv row of the milk.
  std::uint32_t milk_ = core::kNoTableRow;

  /// field_phases.csv plowing, real man-days a hectare.
  float plough_days_per_ha_ = 0.0F;

  /// Harnessed assignment-days by day of the game year.
  std::array<float, core::kDaysPerYear> harness_{};

  std::uint32_t last_noted_day_ = 0;

  bool noted_any_ = false;

  /// The closed book's year at the last note.
  std::uint16_t closed_year_ = 0;

  /// The open year's cow adults, summed over its noted days.
  std::int64_t cow_days_ = 0;

  std::int64_t cow_days_noted_ = 0;

  /// A cow's milk of the last closed year, grams; 0 = no year closed yet.
  double cow_yield_grams_ = 0.0;

  /// The highest milk position the district has named, grams.
  double highest_due_ = 0.0;

  std::int64_t least_cows_ = 0;

  std::int64_t least_horses_ = 0;

  std::uint32_t cooldown_ = 0;

  std::uint32_t yellow_days_ = 0;

  std::uint32_t floor_days_ = 0;

  std::uint32_t rushes_ = 0;

  std::uint32_t hand_orders_ = 0;

  std::uint64_t horses_handed_ = 0;

  std::uint64_t cows_handed_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HAY_ANSWER_H_
