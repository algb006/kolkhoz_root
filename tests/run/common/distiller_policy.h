/// @file
/// @brief The run's chairman takes the distiller a theft report names, and
///        puts a watchman where the leak is (#1250; econ, core-boss-1250-
///        distiller-rule [3]; boss [4]).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN DOES THIS. The run's chairman bought sugar (#1240) and talked
/// to the drinkers (#1238), and the old drinkers' tail still grew to 32 a
/// village by y10 at 0.94: no run ever took a distiller, and the leak that
/// feeds him stood open behind the one watchman WatchmanPolicy keeps.
///
/// THE RULE (econ [3]):
///   * WHAT THE BOT KNOWS is what the game tells the player (A (a)): a
///     kSugarStolen names the distiller (`resident`) and the first store he
///     took from (`unit`). Nothing reads night_trade to find a man;
///   * ON A THEFT REPORT, at most one a month (D): kTakeNightTrader for the
///     man named, and a watchman at that store's guard (the store, or its
///     parent where the staff table posts the watchman at the parent), unless
///     a sober night watch already holds it;
///   * ON THE VILLAGE'S DRINKING RISING TWO MONTHS RUNNING (night_theft's
///     settlement mean, read at each month's turn): no man is taken - there
///     is no name - and a watchman goes to the unguarded store that holds the
///     most raw material, the leak a player can see without a name;
///   * THE CATCH ALWAYS SUCCEEDS (B): the core runs have no night-catch rule
///     (host_script/night_catch.h judges it in the game), so this is the
///     catch at the design's maximum, an upper bound, and the run says so;
///   * A WATCHMAN is a sober adult (alcoholism ≤ night_sober_keeper_max) with
///     no post: a drinking one is as good as none (night_trade.cpp).
/// THE INSTRUMENT: the DT line a closed year - takes ordered and taken, the
/// watchmen appointed on each trigger and the posts held at the year's end,
/// the reports heard and the rises seen - and a «NOT MEASURED» when nothing
/// was heard all year.
#ifndef TESTS_RUN_COMMON_DISTILLER_POLICY_H_
#define TESTS_RUN_COMMON_DISTILLER_POLICY_H_

#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core_catalog/world_conventions.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Takes the distiller a theft report names and closes the leak.
class DistillerPolicy {
 public:
  explicit DistillerPolicy(const core::ITableSet& tables) {
    if (const core::ITable* const professions = tables.FindTable("professions")) {
      const std::uint32_t row = professions->FindRowByKey("watchman");
      if (row != core::kNoTableRow) {
        watchman_ = core::ProfessionId{static_cast<std::uint16_t>(row)};
      }
    }
    const core::ITable* const types = tables.FindTable("unit_types");
    const core::ITable* const staff = tables.FindTable("unit_staff");
    if (types != nullptr && staff != nullptr) {
      staffed_.assign(types->RowCount(), 0);
      const std::uint32_t unit_col = staff->FindColumn("unit");
      const std::uint32_t post_col = staff->FindColumn("profession");
      for (std::uint32_t row = 0; row < staff->RowCount(); ++row) {
        if (unit_col == core::kNoTableColumn || post_col == core::kNoTableColumn ||
            staff->CellText(row, post_col) != "watchman") {
          continue;
        }
        const std::uint32_t type_row = types->FindRowByKey(staff->CellText(row, unit_col));
        if (type_row != core::kNoTableRow) {
          staffed_[type_row] = 1;
        }
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      for (const std::string_view key : kRawMaterial) {
        const std::uint32_t row = resources->FindRowByKey(key);
        if (row != core::kNoTableRow) {
          raw_.push_back(core::ResourceId{static_cast<std::uint16_t>(row)});
        }
      }
    }
    if (const core::ITable* const world = tables.FindTable("world_params")) {
      const std::uint32_t row = world->FindRowByKey("night_sober_keeper_max");
      const std::uint32_t column = world->FindColumn("value");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        sober_max_ = world->CellReal(row, column).value_or(sober_max_);
      }
    }
    life_speedup_ = core::LifeSpeedupOr(tables, life_speedup_);
  }

  /// @brief The control arm (--no-catch-distiller): the rule does nothing.
  void Enable(bool on) { on_ = on; }

  /// @brief Listens to one step's events: the theft reports come at night,
  ///        and RunDay sees the day's last step alone. Call after EVERY step.
  void WatchStep(const core::WorldState& world) {
    if (!on_) {
      return;
    }
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kSugarStolen && !report_.heard) {
        report_ = Report{.heard = true, .man = event.resident, .store = event.unit};
      }
      reports_year_ += event.kind == core::EventKind::kSugarStolen ? 1U : 0U;
    }
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (!on_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    const core::SimDay today = world.calendar.day;
    ReadYesterday(world);
    CloseYear(world, today);
    const std::uint32_t month = today / core::kDaysPerMonth;
    if (today % core::kDaysPerMonth == 0 && today != last_turn_day_) {
      last_turn_day_ = today;
      const float mean = world.night_theft.settlement_alcoholism;
      // The first turn read has nothing before it to have risen from.
      rises_ = seen_turn_ && mean > last_mean_ ? rises_ + 1U : 0U;
      last_mean_ = mean;
      seen_turn_ = true;
    }
    std::vector<core::OrderRow> orders;
    if (report_.heard && month != took_month_) {
      took_month_ = month;
      core::OrderRow take;
      take.kind = core::OrderKind::kTakeNightTrader;
      take.resident = report_.man;
      orders.push_back(take);
      asked_ = report_.man;
      ++takes_year_;
      AppointAt(world, GuardOf(world, report_.store), orders, appointed_on_take_year_);
      report_ = Report{};
    } else if (rises_ >= 2 && month != watch_month_) {
      watch_month_ = month;
      ++rises_year_;
      AppointAt(world, UnguardedStore(world), orders, appointed_on_rise_year_);
    }
    if (!orders.empty()) {
      simulation.StageOrders(orders, {});
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman TAKES the distiller "
                 "a sugar theft report names, at most one a month, and posts a sober watchman at "
                 "the store he took from; when the village's drinking rises two months running he "
                 "posts one at the unguarded store holding the most raw material. The catch "
                 "always succeeds: an UPPER BOUND, no night-catch rule in core runs (#1250; "
                 "econ, core-boss-1250-distiller-rule [3]); --no-catch-distiller is the control\n";
  }

 private:
  static constexpr std::array<std::string_view, 6> kRawMaterial = {
      "rye", "wheat", "barley", "oat", "potato", "sugar"};
  static constexpr float kAdultYears = 16.0F;
  static constexpr std::uint32_t kNever = 0xFFFFFFFFU;

  struct Report {
    bool heard = false;
    core::ResidentId man;
    core::UnitId store;
  };

  /// The unit whose watchman keeps `store`: the store when the staff table
  /// posts one there, else its parent (a module is kept by its parent's watch).
  std::uint32_t GuardOf(const core::WorldState& world, core::UnitId store) const {
    const std::uint32_t row = core::FindRow(world.units, store);
    if (row == core::kNoRow) {
      return core::kNoRow;
    }
    if (Staffed(world.units.rows[row])) {
      return row;
    }
    const std::uint32_t parent = core::FindRow(world.units, world.units.rows[row].parent);
    return parent != core::kNoRow && Staffed(world.units.rows[parent]) ? parent : core::kNoRow;
  }

  bool Staffed(const core::UnitRow& unit) const {
    return unit.level > 0 && unit.type.value < staffed_.size() && staffed_[unit.type.value] != 0;
  }

  bool SoberWatchAt(const core::WorldState& world, std::uint32_t guard_row) const {
    const std::uint32_t id = world.units.row_ids[guard_row].value;
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.post.profession.value == watchman_.value && person.post.unit.value == id &&
          person.alcoholism <= sober_max_) {
        return true;
      }
    }
    return false;
  }

  /// The staffed guard of a store holding raw material with no sober watch,
  /// the one keeping the most; kNoRow when every leak is closed.
  std::uint32_t UnguardedStore(const core::WorldState& world) const {
    std::uint32_t best = core::kNoRow;
    core::Grams best_grams = 0;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      core::Grams grams = 0;
      for (const core::ResourceId resource : raw_) {
        grams += core::UnreservedOf(world.units.rows[row], resource);
      }
      if (grams <= 0) {
        continue;
      }
      const std::uint32_t guard = GuardOf(world, world.units.row_ids[row]);
      if (guard == core::kNoRow || SoberWatchAt(world, guard) || grams <= best_grams) {
        continue;
      }
      best = guard;
      best_grams = grams;
    }
    return best;
  }

  void AppointAt(const core::WorldState& world,
                 std::uint32_t guard_row,
                 std::vector<core::OrderRow>& orders,
                 std::uint32_t& counter) {
    if (watchman_.value == core::kInvalidDefIdValue || guard_row == core::kNoRow ||
        SoberWatchAt(world, guard_row)) {
      return;
    }
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& person = world.residents.rows[row];
      if (person.post.profession.value != core::kInvalidDefIdValue ||
          person.alcoholism > sober_max_ || person.night_trade != core::NightTrade::kNone ||
          core::BiologicalAgeYears(life_speedup_, person.birth_day, world.calendar.day) <
              kAdultYears) {
        continue;
      }
      core::OrderRow appoint;
      appoint.kind = core::OrderKind::kAppoint;
      appoint.resident = world.residents.row_ids[row];
      appoint.unit = world.units.row_ids[guard_row];
      appoint.profession = watchman_;
      orders.push_back(appoint);
      ++counter;
      return;
    }
  }

  /// Yesterday's take, off the state: the man keeps no trade now, or is gone.
  void ReadYesterday(const core::WorldState& world) {
    if (asked_.value == core::kInvalidEntityIdValue) {
      return;
    }
    const std::uint32_t row = core::FindRow(world.residents, asked_);
    taken_year_ +=
        row == core::kNoRow || world.residents.rows[row].night_trade == core::NightTrade::kNone
            ? 1U
            : 0U;
    asked_ = core::ResidentId{};
  }

  void CloseYear(const core::WorldState& world, core::SimDay today) {
    if (today == 0 || today % core::kDaysPerYear != 0 || today == closed_day_) {
      return;
    }
    closed_day_ = today;
    std::uint32_t posts = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      posts += person.post.profession.value == watchman_.value ? 1U : 0U;
    }
    std::printf(
        "DT,y=%u,reports=%u,takes=%u,taken=%u,rises_acted=%u,watch_on_take=%u,watch_on_rise=%u,"
        "watch_posts_held=%u%s\n",
        today / core::kDaysPerYear,
        reports_year_,
        takes_year_,
        taken_year_,
        rises_year_,
        appointed_on_take_year_,
        appointed_on_rise_year_,
        posts,
        reports_year_ == 0 && rises_year_ == 0 ? ",NOT MEASURED: no report and no rise heard" : "");
    reports_year_ = takes_year_ = taken_year_ = rises_year_ = 0;
    appointed_on_take_year_ = appointed_on_rise_year_ = 0;
  }

  core::ProfessionId watchman_;
  std::vector<std::uint8_t> staffed_;
  std::vector<core::ResourceId> raw_;
  float sober_max_ = 20.0F;
  float life_speedup_ = 4.0F;
  bool on_ = true;
  Report report_;
  core::ResidentId asked_;
  std::uint32_t took_month_ = kNever;
  std::uint32_t watch_month_ = kNever;
  core::SimDay last_turn_day_ = 0;
  core::SimDay closed_day_ = 0;
  float last_mean_ = 0.0F;
  bool seen_turn_ = false;
  std::uint32_t rises_ = 0;
  std::uint32_t reports_year_ = 0;
  std::uint32_t takes_year_ = 0;
  std::uint32_t taken_year_ = 0;
  std::uint32_t rises_year_ = 0;
  std::uint32_t appointed_on_take_year_ = 0;
  std::uint32_t appointed_on_rise_year_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DISTILLER_POLICY_H_
