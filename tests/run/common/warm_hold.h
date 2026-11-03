/// @file
/// @brief The first pen's warm before every other works in its window: the
/// run's chairman starts no works that would take the village's straw, logs
/// or clay below the warm's own recipe while the first cattle yard's level 2
/// is still to be ordered (MR.2's design cure, «the bot builds L2 in its first
/// wave»; boss, the #1056 thread [119]-[121], form (B)).
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY. The first cattle yard's level 1 stands free on the day it is marked
/// (#714); its level 2 is an upgrade the core refuses until the whole recipe
/// lies in the village (kMaterialsShort), and the bot orders it only in the
/// clay months (fixture_policy.h). The start's straw is one heap, and the
/// start's other works drew on it first: in 1934 and 1938 of MR.2's pair the
/// barrack (and in 1938 a consumer not named) took it a day before the warm
/// was asked, the warm found 4-5 t of its 6, missed September's clay and
/// stood in year 2 - the year-2 milk plan read the cold yard's yield, 19.38 t
/// against 25.83, and the canon lost 4-5 cows a village by year 4.
///
/// THE RULE. While kolkhoz cattle stand, no cattle yard stands at level 2 or
/// has its level 2 under works, and the month is one of the warm's (clay)
/// months: a start of any other type is refused when its recipe would leave
/// less unreserved straw, log or clay in the village's built units than the
/// warm's recipe asks. A family waiting for a roof is not held back: its house
/// comes first, as everywhere in this chairman. Outside the clay months the
/// hold lifts - the warm waits for spring anyway, and nothing waits for it.

#ifndef TESTS_RUN_COMMON_WARM_HOLD_H_
#define TESTS_RUN_COMMON_WARM_HOLD_H_

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

/// @brief The hold of the first pen's warm (the file's rule), asked as a start
///        gate by every policy that starts sites.
class WarmHold {
 public:
  explicit WarmHold(const core::ITableSet& tables) {
    const core::ITable* const types = tables.FindTable("unit_types");
    if (types != nullptr) {
      const std::uint32_t row = types->FindRowByKey("cattle_yard");
      cattle_ = row == core::kNoTableRow ? core::UnitTypeId{}
                                         : core::UnitTypeId{static_cast<std::uint16_t>(row)};
    }
    ReadCattleKinds(tables);
    ReadCosts(tables);
  }

  /// @brief The difference from the start canon, in words, before the run.
  static void Declare() {
    std::cout << "FIXTURE DIFFERS FROM THE START CANON — the run's chairman holds the first "
                 "cattle yard's warm (level 2) ahead of every other works in the clay months: "
                 "no other works start that would leave the village less straw, logs or clay "
                 "than the warm's recipe, until the warm is ordered; a family waiting for a "
                 "roof is not held back (MR.2's design cure, «L2 in the first wave»; boss, the "
                 "#1056 thread [121])\n";
  }

  /// @brief #713's must-not arm: the hold never stands (BuildingChairman::
  /// SetWarmMovesOff).
  void SetOff() { off_ = true; }

  /// @brief #713 (w1′)'s pair base: the hold as it stood before the cure -
  /// each start against the morning's stock, no feed margin.
  void UseOldRule() { old_rule_ = true; }

  /// @brief #713 (a′)'s pair base: the houses' exemption read as before - a
  /// couple in the wedding queue counts as waiting for a roof
  /// (BuildingChairman::WireWarmHold reads it).
  void UseOldRoofRule() { old_roof_rule_ = true; }

  bool OldRoofRule() const { return old_roof_rule_; }

  /// @brief True when a start of `type` at `level` spares the warm (the rule).
  ///        The houses' exemption for a family waiting for a roof is the
  ///        wiring's (BuildingChairman): this answers for the materials alone.
  ///
  ///        START BY START, AND WITH THE FEED'S MARGIN (#713 (w1′); boss,
  ///        boss-core-1205-birth-prints [56]-[57]): every start asked was
  ///        measured against the morning's stock, so two or three starts of
  ///        one day each passed and together took the straw below the warm -
  ///        1937 y1 day 13, 22 t -> 6 t - and the hold kept the warm's grams to
  ///        the gram while the herds' ration took grams of straw a day: 5 999
  ///        of 6 000 kg, no warm in the clay window, 30 heads frozen at y2's
  ///        start in 4 of 32 villages. Now a start this answer lets through
  ///        is counted against today's later ones (an answer, not an order:
  ///        a «yes» the policy does not act on still counts - the safe side),
  ///        and the warm keeps, besides its recipe, the open year's daily feed
  ///        of the resource times the days left in the clay window.
  bool SparesTheWarmFor(const core::WorldState& world,
                        core::UnitTypeId type,
                        std::uint8_t level) const {
    if (!Holding(world) || type.value == cattle_.value) {
      return true;
    }
    if (old_rule_) {  // the pair's base: every start against the morning, no margin
      for (const Cost& warm : warm_) {
        const core::Grams takes = CostGrams(type, level, warm.resource);
        if (takes > 0 && Held(world, warm.resource) - takes < warm.grams) {
          ++held_back_[type.value];
          return false;
        }
      }
      return true;
    }
    if (world.calendar.day != promised_day_) {
      promised_day_ = world.calendar.day;
      promised_.assign(warm_.size(), 0);
    }
    for (std::size_t index = 0; index < warm_.size(); ++index) {
      const Cost& warm = warm_[index];
      const core::Grams takes = CostGrams(type, level, warm.resource);
      if (takes > 0 && Held(world, warm.resource) - promised_[index] - takes <
                           warm.grams + FeedMargin(world, warm.resource)) {
        ++held_back_[type.value];
        return false;
      }
    }
    for (std::size_t index = 0; index < warm_.size(); ++index) {
      promised_[index] += CostGrams(type, level, warm_[index].resource);
    }
    return true;
  }

  /// @brief The starts the hold refused, by unit_types row - the price of its
  ///        margin, for the run to print.
  const std::map<std::uint16_t, std::uint32_t>& HeldBack() const { return held_back_; }

  /// @brief True while the hold stands (the rule's three conditions).
  bool Holding(const core::WorldState& world) const {
    if (off_ || cattle_.value == core::kInvalidDefIdValue || warm_.empty()) {
      return false;
    }
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    if (month < kClayFirstMonth || month > kClayLastMonth) {
      return false;
    }
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == cattle_.value && unit.dead == 0 &&
          (unit.level >= kWarmLevel || unit.construction.target_level >= kWarmLevel)) {
        return false;
      }
    }
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.kind.value < cattle_kind_.size() &&
          cattle_kind_[herd.kind.value] != 0 &&
          herd.adult_count + herd.juvenile_count + herd.newborn_count > 0) {
        return true;
      }
    }
    return false;
  }

 private:
  bool off_ = false;
  bool old_rule_ = false;
  bool old_roof_rule_ = false;
  mutable core::SimDay promised_day_ = 0xFFFFFFFFU;
  mutable std::vector<core::Grams> promised_;
  mutable std::map<std::uint16_t, std::uint32_t> held_back_;

  /// The open year's daily draw of `resource` by the herds' ration out of the
  /// stores, times the days left in the clay window (#713 (w1′)).
  static core::Grams FeedMargin(const core::WorldState& world, core::ResourceId resource) {
    const core::Grams fed = resource.value < world.ledger.current.feed.size()
                                ? world.ledger.current.feed[resource.value]
                                : 0;
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    const std::uint32_t window_end = (kClayLastMonth + 1U) * core::kDaysPerMonth;
    const std::uint32_t days_left = window_end > day_of_year ? window_end - day_of_year : 0U;
    // MULTIPLIED FIRST, ROUNDED UP (#1374's pair, seed 2035; econ via boss
    // [16]): the mean day's draw was divided out first, in whole grams, so a
    // ration that took 16 g of straw in 18 days read 0 g a day and the margin
    // was 0 - a start took the heap to 5 999 981 g of the warm's 6 000 000,
    // and the warm stood unordered from y1 day 18 to 30. Any draw at all now
    // keeps a margin, rounded up to the gram (seed 2035, day 17: 16 g over 18
    // days, 19 days left -> 17 g, and the 3 t start that day is refused).
    const auto days = static_cast<core::Grams>(day_of_year + 1U);
    return ((fed * static_cast<core::Grams>(days_left)) + days - 1) / days;
  }

  /// The clay months, 0-based (fixture_policy.h kClayWarmFirstMonth/LastMonth:
  /// March to September). Kept equal to the warm order's own window.
  static constexpr std::uint32_t kClayFirstMonth = 2;
  static constexpr std::uint32_t kClayLastMonth = 8;
  static constexpr std::uint8_t kWarmLevel = 2;

  struct Cost {
    core::UnitTypeId type;
    std::uint8_t level = 0;
    core::ResourceId resource;
    core::Grams grams = 0;
  };

  void ReadCattleKinds(const core::ITableSet& tables) {
    const core::ITable* const livestock = tables.FindTable("livestock");
    if (livestock == nullptr) {
      return;
    }
    const std::uint32_t home = livestock->FindColumn("home_unit");
    for (std::uint32_t row = 0; home != core::kNoTableColumn && row < livestock->RowCount();
         ++row) {
      cattle_kind_.push_back(livestock->CellText(row, home) == "cattle_yard" ? 1U : 0U);
    }
  }

  /// Every straw, log and clay line of unit_level_cost.csv in grams (the
  /// resource's own unit times resources.csv kg_per_unit), and the warm's
  /// own three apart.
  void ReadCosts(const core::ITableSet& tables) {
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    const core::ITable* const types = tables.FindTable("unit_types");
    const core::ITable* const resources = tables.FindTable("resources");
    if (costs == nullptr || types == nullptr || resources == nullptr) {
      return;
    }
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    const std::uint32_t kg_col = resources->FindColumn("kg_per_unit");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      const std::string_view name = costs->CellText(row, resource_col);
      if (name != "straw" && name != "log" && name != "clay") {
        continue;
      }
      const std::uint32_t type_row = types->FindRowByKey(costs->CellText(row, unit_col));
      const std::uint32_t resource_row = resources->FindRowByKey(name);
      const std::optional<std::int64_t> level = costs->CellInteger(row, level_col);
      const std::optional<float> amount = costs->CellReal(row, amount_col);
      const std::optional<float> kg = resources->CellReal(resource_row, kg_col);
      if (type_row == core::kNoTableRow || resource_row == core::kNoTableRow || !level || !amount ||
          !kg) {
        continue;
      }
      const Cost cost{.type = core::UnitTypeId{static_cast<std::uint16_t>(type_row)},
                      .level = static_cast<std::uint8_t>(*level),
                      .resource = core::ResourceId{static_cast<std::uint16_t>(resource_row)},
                      .grams = core::GramsFromKilograms(*amount * *kg)};
      costs_.push_back(cost);
      if (cost.type.value == cattle_.value && cost.level == kWarmLevel) {
        warm_.push_back(cost);
      }
    }
  }

  core::Grams CostGrams(core::UnitTypeId type,
                        std::uint8_t level,
                        core::ResourceId resource) const {
    for (const Cost& cost : costs_) {
      if (cost.type.value == type.value && cost.level == level &&
          cost.resource.value == resource.value) {
        return cost.grams;
      }
    }
    return 0;
  }

  /// What the built units hold of `resource` that nobody's works hold back
  /// (SawmillPolicy::Held's reading).
  static core::Grams Held(const core::WorldState& world, core::ResourceId resource) {
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level > 0) {
        held += core::UnreservedOf(unit, resource);
      }
    }
    return held;
  }

  core::UnitTypeId cattle_;
  std::vector<std::uint8_t> cattle_kind_;
  std::vector<Cost> costs_;
  std::vector<Cost> warm_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_WARM_HOLD_H_
