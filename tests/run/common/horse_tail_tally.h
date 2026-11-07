#ifndef TESTS_RUN_COMMON_HORSE_TAIL_TALLY_H_
#define TESTS_RUN_COMMON_HORSE_TAIL_TALLY_H_

/// @file horse_tail_tally.h
/// @brief #1573's horse tail (econ, econ-boss-1573-horse-tail-2026-11-07 [1] §3; gates H1-H5 on
/// y7-y16): by
///        year y1-kYears, one village a line - «HORSE1573 seed S | y1 ... | y2 ...»:
///        - horses / horse_floor: the kolkhoz's adult horses at the year's last day, and the horse
///        herds' plan
///          floor (HerdPlanLine::floor_heads - the spring's horses, the plowing floor x its
///          margin);
///        - cows / cow_floor: the same of the milk kind (the milk plan's cows);
///        - horse_short_days / cow_short_days: days a kolkhoz herd of the kind ended unfed
///        (unfed_days > 0);
///        - horse_hunger / cow_hunger: heads of the kind that died of hunger (kHerdDied, not
///        routine, on a
///          kolkhoz herd - the hunger's own severity, herd_life.cpp);
///        - hay_cut_t, horse_hay_t: the year's hay cut, and the hay the horses ate
///        (YearLedger::herd_hay_eaten);
///          the share above the floor is the reader's (horse_hay x (horses - floor) / horses, a
///          mean-head ESTIMATE, not a book);
///        - milk_t, milk_met: the kolkhoz herds' milk, and whether the milk position was delivered
///        (plan.due
///          against plan.delivered at the year's last day; «-» with no milk position).
///        The lamp's split by year is HayAnswer's (PrintHorseTail, beside this line).
/// @threading SINGLE_THREADED - the run's own loop.

#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/herd_plan_view.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class HorseTailTally {
 public:
  static constexpr std::uint32_t kYears = 30;
  static constexpr std::uint32_t kNoonHour = 12;
  static constexpr std::uint32_t kApril = 3;  ///< 0-based months
  static constexpr std::uint32_t kMay = 4;
  static constexpr std::uint32_t kAugust = 7;
  static constexpr std::uint32_t kOctober = 9;
  /// HerdPlanLine::floor_heads at or above this is «every cow kept», not a floor.
  static constexpr std::uint16_t kEveryCowKept = 65534;

  explicit HorseTailTally(const core::ITableSet& tables) {
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      horse_ = kinds->FindRowByKey("horse");
      cow_ = kinds->FindRowByKey("cow");
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      hay_ = resources->FindRowByKey("hay");
      milk_ = resources->FindRowByKey("milk");
    }
  }

  /// @brief Every step: the hunger's deaths by kind; at noon the «no horse»
  ///        idle (#1584's R4: spring = April-May, hauling = August-October).
  void CountStep(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.date.year;
    if (year < 1 || year > kYears) {
      return;
    }
    if (core::HourFromTick(world.calendar.tick) == kNoonHour) {
      const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
      const bool spring = month == kApril || month == kMay;
      const bool hauling = month >= kAugust && month <= kOctober;
      if (spring || hauling) {
        Year& y = years_[year - 1];
        for (const core::ResidentRow& person : world.residents.rows) {
          const bool working = person.work.kind != core::WorkKind::kNone;
          const bool idle = !working && person.idle_reason != core::IdleReason::kIdleReasonCount;
          if (!working && !idle) {
            continue;
          }
          const bool no_horse = idle && person.idle_reason == core::IdleReason::kNoHorse;
          (spring ? y.spring_heads : y.haul_heads) += 1;
          (spring ? y.spring_no_horse : y.haul_no_horse) += no_horse ? 1U : 0U;
        }
      }
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
      const std::uint32_t kind = world.herds.rows[row].kind.value;
      Year& y = years_[year - 1];
      (kind == horse_ ? y.horse_hunger : (kind == cow_ ? y.cow_hunger : y.other_hunger)) +=
          event.amount;
    }
  }

  /// @brief Once a day after the chairman: short days; on the year's last day its counts and
  /// floors.
  void CountDayEnd(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (year > kYears) {
      return;
    }
    Year& y = years_[year - 1];
    bool horse_short = false;
    bool cow_short = false;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned != 0 || herd.unfed_days <= 0.0F) {
        continue;
      }
      horse_short = horse_short || herd.kind.value == horse_;
      cow_short = cow_short || herd.kind.value == cow_;
    }
    y.horse_short_days += horse_short ? 1U : 0U;
    y.cow_short_days += cow_short ? 1U : 0U;
    // #1584's R1: the hay left at April's end (its last day's close), the
    // stores' unreserved hay in every standing unit.
    if (static_cast<std::uint32_t>(world.calendar.date.month) == kApril &&
        world.calendar.date.day_in_month == core::kDaysPerMonth - 1U && hay_ != core::kNoTableRow) {
      core::Grams held = 0;
      for (const core::UnitRow& unit : world.units.rows) {
        if (unit.level > 0) {
          held += core::UnreservedOf(unit, core::ResourceId{static_cast<std::uint16_t>(hay_)});
        }
      }
      y.hay_april_end_g = held;
    }
    if (world.calendar.day % core::kDaysPerYear != core::kDaysPerYear - 1U) {
      return;
    }
    // The year's last day: the book is still `current`.
    y.closed = true;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0) {
        (herd.kind.value == horse_ ? y.horses : (herd.kind.value == cow_ ? y.cows : y.other)) +=
            herd.adult_count;
      }
    }
    for (const core::HerdPlanLine& line : simulation.HerdPlans()) {
      if (line.kind.value == horse_) {
        y.horse_floor = std::max<std::int64_t>(y.horse_floor, line.floor_heads);
      } else if (line.kind.value == cow_) {
        // «EVERY COW KEPT» (no closed year to read a yield from) is a sentinel,
        // not a floor (boss [10] (5): it was averaged in): printed as -1.
        y.cow_floor = line.floor_heads >= kEveryCowKept
                          ? -1
                          : std::max<std::int64_t>(y.cow_floor, line.floor_heads);
      }
    }
    const core::YearLedger& book = world.ledger.current;
    y.hay_cut_g = hay_ < book.harvest.size() ? book.harvest[hay_] : 0;
    y.horse_hay_g = horse_ < book.herd_hay_eaten.size() ? book.herd_hay_eaten[horse_] : 0;
    y.milk_g = milk_ < book.herd_produce.size() ? book.herd_produce[milk_] : 0;
    const core::Grams due = milk_ < world.plan.due.size() ? world.plan.due[milk_] : 0;
    const core::Grams delivered =
        milk_ < world.plan.delivered.size() ? world.plan.delivered[milk_] : 0;
    y.milk_met = due <= 0 ? -1 : (delivered >= due ? 1 : 0);
    // #1584: R7, hay as a plan position (due and delivered at the year's
    // last day, 0 / 0 when the district names none), and the book's hunger
    // deaths of every herd, yards' included, beside the kolkhoz's own above.
    y.hay_due_g = hay_ < world.plan.due.size() ? world.plan.due[hay_] : 0;
    y.hay_delivered_g = hay_ < world.plan.delivered.size() ? world.plan.delivered[hay_] : 0;
    y.book_hunger = book.herd_deaths_hunger;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: HORSE1573 seed " << seed;
    for (std::uint32_t year = 0; year < kYears; ++year) {
      const Year& y = years_[year];
      out << " | y" << year + 1;
      if (!y.closed) {
        out << " -";
        continue;
      }
      out << " horses " << y.horses << " horse_floor " << y.horse_floor << " cows " << y.cows
          << " cow_floor " << y.cow_floor << " horse_short_days " << y.horse_short_days
          << " cow_short_days " << y.cow_short_days << " horse_hunger " << y.horse_hunger
          << " cow_hunger " << y.cow_hunger << " hay_cut_t "
          << static_cast<double>(y.hay_cut_g) / 1e6 << " horse_hay_t "
          << static_cast<double>(y.horse_hay_g) / 1e6 << " milk_t "
          << static_cast<double>(y.milk_g) / 1e6 << " milk_met "
          << (y.milk_met < 0 ? std::string("-") : std::to_string(y.milk_met)) << " other_hunger "
          << y.other_hunger << " book_hunger_all_herds " << y.book_hunger << " hay_april_end_t "
          << static_cast<double>(y.hay_april_end_g) / 1e6 << " hay_due_t "
          << static_cast<double>(y.hay_due_g) / 1e6 << " hay_delivered_t "
          << static_cast<double>(y.hay_delivered_g) / 1e6 << " spring_no_horse "
          << y.spring_no_horse << " spring_heads " << y.spring_heads << " haul_no_horse "
          << y.haul_no_horse << " haul_heads " << y.haul_heads;
    }
    out << '\n';
  }

 private:
  struct Year {
    bool closed = false;
    std::int64_t horses = 0;
    std::int64_t cows = 0;
    std::int64_t other = 0;
    std::int64_t horse_floor = 0;
    std::int64_t cow_floor = 0;
    std::uint32_t horse_short_days = 0;
    std::uint32_t cow_short_days = 0;
    std::int64_t horse_hunger = 0;
    std::int64_t cow_hunger = 0;
    std::int64_t other_hunger = 0;
    std::uint32_t book_hunger = 0;
    core::Grams hay_april_end_g = 0;
    core::Grams hay_due_g = 0;
    core::Grams hay_delivered_g = 0;
    std::uint64_t spring_heads = 0;
    std::uint64_t spring_no_horse = 0;
    std::uint64_t haul_heads = 0;
    std::uint64_t haul_no_horse = 0;
    core::Grams hay_cut_g = 0;
    core::Grams horse_hay_g = 0;
    core::Grams milk_g = 0;
    int milk_met = -1;
  };

  std::uint32_t horse_ = core::kNoTableRow;
  std::uint32_t cow_ = core::kNoTableRow;
  std::uint32_t hay_ = core::kNoTableRow;
  std::uint32_t milk_ = core::kNoTableRow;
  std::array<Year, kYears> years_ = {};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HORSE_TAIL_TALLY_H_
