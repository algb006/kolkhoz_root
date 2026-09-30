/// @file
/// @brief The straw the run's chairman puts on the walls of the lived-in
/// houses before the winter.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN ORDERS THIS. kInsulateUnit exists since 0.25.0 (unit rules
/// §16) and no run had ever seen a warm house, because a run has no chairman
/// to order one (boss, parcel 364: the prosthesis insulates, low priority —
/// "a sensible chairman insulates the housing for the first winter, with
/// straw, when there is straw"). The run plays the player here and says so
/// out loud.
///
/// THE RULE IS THE RUN'S, NOT THE GAME'S: nothing before the chairman's yard
/// stands; then, in October and November, after the harvest, one lived-in
/// house at a time that
/// is not warm yet — and only while the village holds the job's straw plus a
/// reserve for the herds, since straw is feed too (feed_links.csv).

#ifndef TESTS_RUN_COMMON_INSULATION_POLICY_H_
#define TESTS_RUN_COMMON_INSULATION_POLICY_H_

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

/// @brief Insulates the lived-in houses one by one before each winter.
class InsulationPolicy {
 public:
  explicit InsulationPolicy(const core::ITableSet& tables) {
    const core::ITable* resources = tables.FindTable("resources");
    const std::uint32_t row =
        resources == nullptr ? core::kNoTableRow : resources->FindRowByKey("straw");
    if (row != core::kNoTableRow) {
      straw_ = core::ResourceId{static_cast<std::uint16_t>(row)};
    }
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
    ReadWarmRungs(tables);
    const core::ITable* construction = tables.FindTable("construction");
    const std::uint32_t price_row =
        construction == nullptr ? core::kNoTableRow
                                : construction->FindRowByKey("insulation_livestock_straw_t");
    const std::uint32_t value_column =
        construction == nullptr ? core::kNoTableColumn : construction->FindColumn("value");
    if (price_row != core::kNoTableRow && value_column != core::kNoTableColumn) {
      const std::optional<float> tonnes = construction->CellReal(price_row, value_column);
      herd_straw_ =
          tonnes ? static_cast<core::Grams>(*tonnes * static_cast<float>(core::kGramsPerTonne))
                 : herd_straw_;
    }
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (straw_.value == core::kInvalidDefIdValue) {
      return;
    }
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    if (month < kFirstMonth || month > kLastMonth) {
      return;
    }
    core::Grams straw = 0;
    std::uint32_t house = core::kNoRow;
    std::uint32_t byre = core::kNoRow;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.construction.phase == core::ConstructionPhase::kInsulating) {
        return;  // one at a time
      }
      if (unit.level > 0) {
        straw += core::UnreservedOf(unit, straw_);
      }
      const bool free = unit.level > 0 && unit.insulated == 0 &&
                        unit.construction.phase == core::ConstructionPhase::kNone;
      if (house == core::kNoRow && free && unit.household.value != core::kInvalidEntityIdValue &&
          IsHousing(unit)) {
        house = row;
      }
      if (byre == core::kNoRow && free && !WarmRung(unit) &&
          HerdUnderItsRoof(
              world, world.units.rows.size() > row ? world.units.row_ids[row] : core::UnitId{})) {
        byre = row;
      }
    }
    // THE HERDS' ROOF FIRST (0.37.62; Livestock design, «Числа лестницы —
    // Эпоха I»): a cold place freezes a herd, and a cold house freezes
    // nobody to death — «человек от холода не умирает, скот умирает». Not
    // behind the stable either: the cows' pen stands long before the horses'.
    std::uint32_t target = core::kNoRow;
    core::Grams price = 0;
    if (byre != core::kNoRow) {
      target = byre;
      price = herd_straw_;
    } else if (house != core::kNoRow && world.chairman.horses_stabled != 0) {
      target = house;
      price = kHouseStraw;
    }
    if (target == core::kNoRow || straw < price + kHerdReserve) {
      return;
    }
    byres_ += target == byre ? 1U : 0U;
    core::OrderRow order;
    order.kind = core::OrderKind::kInsulateUnit;
    order.unit = world.units.row_ids[target];
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ++ordered_;
    if (first_order_day_ < 0) {
      first_order_day_ = static_cast<std::int64_t>(world.calendar.day);
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman insulates with "
                 "straw, one at a time, in October and November after the harvest, while 20 t "
                 "of straw stay for the herds (boss, parcel 364): first a livestock unit whose "
                 "kolkhoz herd stands under a cold roof (0.37.62, the cold ladder), then, once "
                 "the chairman's yard stands, the lived-in houses\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(const core::WorldState& world, std::string_view run_name) const {
    std::uint32_t lived_in = 0;
    std::uint32_t warm = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level > 0 && unit.household.value != core::kInvalidEntityIdValue &&
          IsHousing(unit)) {
        ++lived_in;
        warm += unit.insulated;
      }
    }
    std::cout << run_name << ": insulation — " << ordered_ << " orders (first on day "
              << first_order_day_ << "), " << byres_ << " of them livestock units; " << warm
              << " of " << lived_in << " lived-in houses warm at the end\n";
  }

 private:
  /// October to November, 0-based by core::Month (0 = January): AFTER the
  /// harvest. The first cut began in August, and its crews — a hundred and
  /// fifty houses of two man-days — came off the fields in the reaping months:
  /// food_year's leanest day fell under its recorded 22.80 to 22.29 on the
  /// tables before boss's export of 2026-09-15.
  static constexpr std::uint32_t kFirstMonth = 9;
  static constexpr std::uint32_t kLastMonth = 10;

  /// A house's straw (construction.csv, 2 t) and what is left for the herds.
  static constexpr core::Grams kHouseStraw = 2 * core::kGramsPerTonne;
  static constexpr core::Grams kHerdReserve = 20 * core::kGramsPerTonne;

  bool IsHousing(const core::UnitRow& unit) const {
    return unit.type.value < definitions_.units.is_housing.size() &&
           definitions_.units.is_housing[unit.type.value] != 0;
  }

  /// unit_levels.csv `warm_place` by type and level (index level - 1).
  void ReadWarmRungs(const core::ITableSet& tables) {
    const core::ITable* levels = tables.FindTable("unit_levels");
    const core::ITable* types = tables.FindTable("unit_types");
    if (levels == nullptr || types == nullptr) {
      return;
    }
    const std::uint32_t unit_column = levels->FindColumn("unit");
    const std::uint32_t level_column = levels->FindColumn("level");
    const std::uint32_t warm_column = levels->FindColumn("warm_place");
    if (unit_column == core::kNoTableColumn || level_column == core::kNoTableColumn ||
        warm_column == core::kNoTableColumn) {
      return;
    }
    warm_.resize(types->RowCount());
    for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
      const std::uint32_t type = types->FindRowByKey(levels->CellText(row, unit_column));
      const std::optional<float> level = levels->CellReal(row, level_column);
      const std::optional<float> warm = levels->CellReal(row, warm_column);
      if (type == core::kNoTableRow || !level || !(*level >= 1.0F)) {
        continue;
      }
      const auto index = static_cast<std::size_t>(*level) - 1U;
      if (warm_[type].size() <= index) {
        warm_[type].resize(index + 1U, 0U);
      }
      warm_[type][index] = warm && *warm > 0.0F ? 1U : 0U;
    }
  }

  /// Whether the unit's rung is warm by the table (the straw is a separate
  /// question, asked before this).
  bool WarmRung(const core::UnitRow& unit) const {
    const std::size_t index = static_cast<std::size_t>(unit.level) - 1U;
    return unit.level > 0 && unit.type.value < warm_.size() &&
           index < warm_[unit.type.value].size() && warm_[unit.type.value][index] != 0;
  }

  /// Whether a kolkhoz herd stands at this unit with heads under its roof.
  static bool HerdUnderItsRoof(const core::WorldState& world, core::UnitId unit) {
    for (const core::HerdRow& herd : world.herds.rows) {
      const std::uint32_t heads =
          static_cast<std::uint32_t>(herd.newborn_count) + herd.juvenile_count + herd.adult_count;
      if (herd.household_owned == 0 && herd.unit.value == unit.value &&
          heads > herd.billeted_count) {
        return true;
      }
    }
    return false;
  }

  std::vector<std::vector<std::uint8_t>> warm_;

  /// One livestock unit's straw (construction.csv insulation_livestock_straw_t).
  core::Grams herd_straw_ = 6 * core::kGramsPerTonne;

  std::uint32_t byres_ = 0;

  core::ResourceId straw_;

  core::Definitions definitions_;

  std::uint32_t ordered_ = 0;

  std::int64_t first_order_day_ = -1;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_INSULATION_POLICY_H_
