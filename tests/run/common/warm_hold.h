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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

  /// @brief True when a start of `type` at `level` spares the warm (the rule).
  ///        The houses' exemption for a family waiting for a roof is the
  ///        wiring's (BuildingChairman): this answers for the materials alone.
  bool SparesTheWarmFor(const core::WorldState& world,
                        core::UnitTypeId type,
                        std::uint8_t level) const {
    if (!Holding(world) || type.value == cattle_.value) {
      return true;
    }
    for (const Cost& warm : warm_) {
      const core::Grams takes = CostGrams(type, level, warm.resource);
      if (takes > 0 && Held(world, warm.resource) - takes < warm.grams) {
        return false;
      }
    }
    return true;
  }

  /// @brief True while the hold stands (the rule's three conditions).
  bool Holding(const core::WorldState& world) const {
    if (cattle_.value == core::kInvalidDefIdValue || warm_.empty()) {
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
