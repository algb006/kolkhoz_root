/// @file
/// @brief #1644, the run chairman's smokehouse (econ, econ-boss-1644-smokehouse
/// [1]-[2]; boss accepted): the canon never built or staffed it, so «salted
/// made 0» in every arm and the meat and fish spoiled unmeasured.
/// @threading SINGLE_THREADED — between steps, on the sim thread.
///
/// THE RULE (STUB econ):
///   * WHEN: the meat and fish spoiled in the last 12 game months (rolling, by
///     the book's daily rises, the turn's closed tail taken) reach 1 t;
///   * WHAT: the smokehouse marked on the food yard (the core finds its parent
///     from the position), started; a food_master appointed at the yard (the
///     smokehouse's work goes to its parent's post holders, labor_system.cpp
///     PutOnModuleWork) - an adult with no post. None free: the day counts as
///     «no hand» (the design's «post floors» have no home in the core: named);
///   * NO YARD STANDING: the day counts as «no yard» - the food yard is
///     FixturePolicy's to put up;
///   * the order «salt first while salt is above the table's reserve, smoke the
///     rest» is the CORE's (processing_shops.cpp), not this rule's.
/// OFF unless asked (--smokehouse, #1644's arm), until econ reads the pair.
/// Prints SMOKE1644 a village-year (K1-K7) and once a village.

#ifndef TESTS_RUN_COMMON_SMOKEHOUSE_POLICY_H_
#define TESTS_RUN_COMMON_SMOKEHOUSE_POLICY_H_

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "core_catalog/world_conventions.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class SmokehousePolicy {
 public:
  static constexpr core::Grams kTriggerGrams = 1'000'000;  ///< econ: 1 t a rolling year
  static constexpr std::uint32_t kYears = 16;

  explicit SmokehousePolicy(const core::ITableSet& tables) {
    const auto row_of = [&tables](const char* table, const char* key) {
      const core::ITable* const t = tables.FindTable(table);
      return t != nullptr ? t->FindRowByKey(key) : core::kNoTableRow;
    };
    yard_ = row_of("unit_types", "food_yard");
    smokehouse_ = row_of("unit_types", "smokehouse");
    master_ = row_of("professions", "food_master");
    meat_ = row_of("resources", "meat");
    fish_ = row_of("resources", "fish");
    salt_ = row_of("resources", "salt");
    firewood_ = row_of("resources", "firewood");
    salted_meat_ = row_of("resources", "salted_meat");
    salted_fish_ = row_of("resources", "salted_fish");
    smoked_meat_ = row_of("resources", "smoked_meat");
    smoked_fish_ = row_of("resources", "smoked_fish");
    life_speedup_ = core::LifeSpeedupOr(tables, life_speedup_);
    if (const core::ITable* const world = tables.FindTable("world_params")) {
      const std::uint32_t column = world->FindColumn("value");
      const auto read = [world, column](const char* key, double& into) {
        const std::uint32_t row = world->FindRowByKey(key);
        if (row != core::kNoTableRow && column != core::kNoTableColumn) {
          if (const std::optional<float> value = world->CellReal(row, column)) {
            into = static_cast<double>(*value);
          }
        }
      };
      read("salt_norm_g_per_eater_day", norm_g_);
      read("salt_table_reserve_months", reserve_months_);
    }
  }

  void Enable(bool on) { on_ = on; }

  [[nodiscard]] bool On() const { return on_; }

  /// @brief The fixture difference, BEFORE the run measures.
  void Declare(std::string_view run_name) const {
    if (on_) {
      std::cout << run_name
                << ": ARM — --smokehouse (#1644): at 1 t of meat and fish spoiled in a rolling "
                   "year the run's chairman marks the smokehouse on the food yard and appoints a "
                   "food_master there\n";
    }
  }

  /// @brief One day: the books' rises, the trigger, at most one order. Once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    CountDay(world);
    if (!on_ || !triggered_) {
      return;
    }
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    core::OrderRow order;
    if (!NextOrder(world, order)) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    cooldown_ = 1;
  }

  /// @brief The village's lines: one a year, then once.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: SMOKE1644 seed " << seed << ' ' << line << '\n';
    }
    out << "population_curve: SMOKE1644 seed " << seed << " arm " << (on_ ? 1 : 0)
        << " trigger_day " << Opt(trigger_day_) << " smokehouse_marked_day " << Opt(marked_day_)
        << " smokehouse_built_day " << Opt(built_day_) << " master_day " << Opt(master_day_)
        << " no_hand_days " << no_hand_days_ << " no_yard_days " << no_yard_days_ << '\n';
  }

 private:
  static std::string Opt(const std::optional<core::SimDay>& day) {
    return day ? std::to_string(*day) : std::string("never");
  }

  static core::Grams Of(const core::ResourceAmounts& amounts, std::uint32_t resource) {
    return resource < amounts.size() ? amounts[resource] : 0;
  }

  core::Grams Held(const core::WorldState& world, std::uint32_t resource) const {
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      held += Of(unit.stock, resource);
    }
    return held;
  }

  std::uint32_t FindOfType(const core::WorldState& world, std::uint32_t type) const {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (world.units.rows[row].type.value == type) {
        return row;
      }
    }
    return core::kNoRow;
  }

  /// The day's rise of a book column, the turn's closed tail taken (#1580).
  core::Grams Rise(const core::WorldState& world,
                   core::Grams now_sum,
                   core::Grams closed_sum,
                   core::Grams& last) const {
    const bool turn = world.calendar.day > 0 && world.calendar.day % core::kDaysPerYear == 0;
    const core::Grams today = turn ? now_sum + (closed_sum > last ? closed_sum - last : 0)
                                   : (now_sum >= last ? now_sum - last : now_sum);
    last = now_sum;
    return today;
  }

  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && year - 1U <= kYears) {
      CloseYear(world, year - 1U);
    }
    const core::YearLedger& book = world.ledger.current;
    const core::YearLedger& closed = world.ledger.closed;
    const core::Grams spoiled = Rise(world,
                                     Of(book.spoiled, meat_) + Of(book.spoiled, fish_),
                                     Of(closed.spoiled, meat_) + Of(closed.spoiled, fish_),
                                     last_spoiled_);
    const core::Grams salt_in =
        Rise(world, Of(book.processed, salt_), Of(closed.processed, salt_), last_salt_in_);
    // THE ROLLING YEAR, by game month.
    const std::uint32_t month = day / core::kDaysPerMonth;
    if (month != month_) {
      month_ = month;
      months_[month % months_.size()] = 0;
    }
    months_[month % months_.size()] += spoiled;
    core::Grams rolling = 0;
    for (const core::Grams grams : months_) {
      rolling += grams;
    }
    if (!triggered_ && rolling >= kTriggerGrams) {
      triggered_ = true;
      trigger_day_ = day;
    }
    // K3: a day the shops took salt while the store's salt stood under the
    // table's reserve (residents x norm x the month's days x the months).
    const double reserve = norm_g_ * static_cast<double>(world.residents.rows.size()) *
                           core::kDaysPerMonth * reserve_months_;
    if (salt_in > 0 && static_cast<double>(Held(world, salt_)) < reserve) {
      ++under_reserve_days_;
    }
    const core::Grams firewood = Held(world, firewood_);
    if (!firewood_low_ || firewood < *firewood_low_) {
      firewood_low_ = firewood;
    }
    const std::uint32_t house = FindOfType(world, smokehouse_);
    if (house != core::kNoRow && world.units.rows[house].level > 0 && !built_day_) {
      built_day_ = day;
    }
    // WHERE THE MEAT LIES AND WHO WORKS IT (#1644's first read: the arm salted
    // 0.2-1.1 t a year and spoiled 96 % of the base): the stores' meat and fish
    // the shop can take, the yards' pantries it cannot; the shop's asked days;
    // the days a resident worked in it; whether a food_master holds the yard.
    ++days_;
    store_meat_sum_ += Of(StockOf(world), meat_) + Of(StockOf(world), fish_);
    for (const core::FamilyRow& family : world.families.rows) {
      pantry_meat_sum_ += Of(family.pantry, meat_) + Of(family.pantry, fish_);
    }
    if (house != core::kNoRow) {
      const core::UnitRow& shop = world.units.rows[house];
      demand_days_ += shop.production_days_written > 0.0F ? 1U : 0U;
      const core::UnitId shop_id = world.units.row_ids[house];
      bool worked = false;
      bool held = false;
      for (const core::ResidentRow& person : world.residents.rows) {
        worked = worked || person.work.unit.value == shop_id.value;
        held = held || person.post.profession.value == master_;
      }
      worked_days_ += worked ? 1U : 0U;
      master_held_days_ += held ? 1U : 0U;
    }
  }

  /// Every store's stock summed, by resource.
  core::ResourceAmounts StockOf(const core::WorldState& world) const {
    core::ResourceAmounts sum;
    for (const core::UnitRow& unit : world.units.rows) {
      if (sum.size() < unit.stock.size()) {
        sum.resize(unit.stock.size(), 0);
      }
      for (std::size_t resource = 0; resource < unit.stock.size(); ++resource) {
        sum[resource] += unit.stock[resource];
      }
    }
    return sum;
  }

  bool NextOrder(const core::WorldState& world, core::OrderRow& order) {
    const std::uint32_t yard = FindOfType(world, yard_);
    if (yard == core::kNoRow || world.units.rows[yard].level == 0) {
      ++no_yard_days_;
      return false;
    }
    const std::uint32_t house = FindOfType(world, smokehouse_);
    if (house == core::kNoRow) {
      order.kind = core::OrderKind::kBuildUnit;
      order.unit_type = core::UnitTypeId{static_cast<std::uint16_t>(smokehouse_)};
      order.position = world.units.rows[yard].position;
      if (!marked_day_) {
        marked_day_ = world.calendar.day;
      }
      return true;
    }
    const core::UnitRow& site = world.units.rows[house];
    if (site.level == 0) {
      if (site.construction.phase != core::ConstructionPhase::kMarked) {
        return false;  // delivering or building
      }
      order.kind = core::OrderKind::kStartBuild;
      order.unit = world.units.row_ids[house];
      return true;
    }
    const core::UnitId yard_id = world.units.row_ids[yard];
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.post.profession.value == master_ && person.post.unit.value == yard_id.value) {
        return false;  // held
      }
    }
    const std::uint32_t candidate = NextCandidate(world);
    if (candidate == core::kNoRow) {
      ++no_hand_days_;
      return false;
    }
    order.kind = core::OrderKind::kAppoint;
    order.resident = world.residents.row_ids[candidate];
    order.unit = yard_id;
    order.profession = core::ProfessionId{static_cast<std::uint16_t>(master_)};
    if (!master_day_) {
      master_day_ = world.calendar.day;
    }
    return true;
  }

  /// An adult with no post, a different one each time the last did not take.
  std::uint32_t NextCandidate(const core::WorldState& world) {
    const std::uint32_t skip = candidate_++;
    std::uint32_t seen = 0;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      if (resident.post.profession.value != core::kInvalidDefIdValue) {
        continue;
      }
      if (core::BiologicalAgeYears(life_speedup_, resident.birth_day, world.calendar.day) <
          kAdultYears) {
        continue;
      }
      if (seen++ < skip % (world.residents.rows.size() + 1U)) {
        continue;
      }
      return row;
    }
    candidate_ = 0;
    return core::kNoRow;
  }

  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    const core::YearLedger& book = world.ledger.closed;
    lines_ << "year " << year << " arm " << (on_ ? 1 : 0) << " salted_made_kg "
           << (Of(book.made, salted_meat_) + Of(book.made, salted_fish_)) / 1000
           << " smoked_made_kg "
           << (Of(book.made, smoked_meat_) + Of(book.made, smoked_fish_)) / 1000
           << " meat_fish_spoiled_kg " << (Of(book.spoiled, meat_) + Of(book.spoiled, fish_)) / 1000
           << " salt_to_barrels_kg " << Of(book.processed, salt_) / 1000 << " firewood_to_shops_kg "
           << Of(book.processed, firewood_) / 1000 << " firewood_lowest_kg "
           << (firewood_low_ ? *firewood_low_ / 1000 : -1) << " days_salting_under_reserve "
           << under_reserve_days_ << " store_meatfish_mean_kg "
           << (days_ > 0 ? store_meat_sum_ / days_ / 1000 : 0) << " pantry_meatfish_mean_kg "
           << (days_ > 0 ? pantry_meat_sum_ / days_ / 1000 : 0) << " shop_asked_days "
           << demand_days_ << " shop_worked_days " << worked_days_ << " master_held_days "
           << master_held_days_ << " days " << days_ << '\n';
    under_reserve_days_ = 0;
    firewood_low_.reset();
    days_ = 0;
    store_meat_sum_ = 0;
    pantry_meat_sum_ = 0;
    demand_days_ = 0;
    worked_days_ = 0;
    master_held_days_ = 0;
  }

  static constexpr float kAdultYears = 16.0F;
  bool on_ = false;
  bool triggered_ = false;
  std::uint32_t cooldown_ = 0;
  std::uint32_t candidate_ = 0;
  std::uint32_t yard_ = core::kNoTableRow;
  std::uint32_t smokehouse_ = core::kNoTableRow;
  std::uint32_t master_ = core::kNoTableRow;
  std::uint32_t meat_ = core::kNoTableRow;
  std::uint32_t fish_ = core::kNoTableRow;
  std::uint32_t salt_ = core::kNoTableRow;
  std::uint32_t firewood_ = core::kNoTableRow;
  std::uint32_t salted_meat_ = core::kNoTableRow;
  std::uint32_t salted_fish_ = core::kNoTableRow;
  std::uint32_t smoked_meat_ = core::kNoTableRow;
  std::uint32_t smoked_fish_ = core::kNoTableRow;
  float life_speedup_ = 4.0F;
  double norm_g_ = 125.0;
  double reserve_months_ = 3.0;
  std::array<core::Grams, 12> months_{};
  std::uint32_t month_ = 0;
  core::Grams last_spoiled_ = 0;
  core::Grams last_salt_in_ = 0;
  std::uint32_t under_reserve_days_ = 0;
  std::int64_t days_ = 0;
  core::Grams store_meat_sum_ = 0;
  core::Grams pantry_meat_sum_ = 0;
  std::uint32_t demand_days_ = 0;
  std::uint32_t worked_days_ = 0;
  std::uint32_t master_held_days_ = 0;
  std::optional<core::Grams> firewood_low_;
  std::optional<core::SimDay> trigger_day_;
  std::optional<core::SimDay> marked_day_;
  std::optional<core::SimDay> built_day_;
  std::optional<core::SimDay> master_day_;
  std::uint32_t no_hand_days_ = 0;
  std::uint32_t no_yard_days_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SMOKEHOUSE_POLICY_H_
