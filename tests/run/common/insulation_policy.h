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
#include <span>
#include <string>
#include <string_view>

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
  }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    if (straw_.value == core::kInvalidDefIdValue || world.chairman.horses_stabled == 0) {
      return;
    }
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    if (month < kFirstMonth || month > kLastMonth) {
      return;
    }
    core::Grams straw = 0;
    std::uint32_t target = core::kNoRow;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.construction.phase == core::ConstructionPhase::kInsulating) {
        return;  // one at a time
      }
      if (unit.level > 0) {
        straw += core::UnreservedOf(unit, straw_);
      }
      if (target == core::kNoRow && unit.level > 0 && unit.insulated == 0 &&
          unit.construction.phase == core::ConstructionPhase::kNone &&
          unit.household.value != core::kInvalidEntityIdValue && IsHousing(unit)) {
        target = row;
      }
    }
    if (target == core::kNoRow || straw < kHouseStraw + kHerdReserve) {
      return;
    }
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
              << ": FIXTURE DIFFERS FROM THE START CANON — once the chairman's yard stands, the "
                 "run's chairman insulates the lived-in houses with straw, one at a time, "
                 "in October and November after the harvest, while 20 t of straw stay for the "
                 "herds (boss, parcel 364)\n";
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
              << first_order_day_ << "); " << warm << " of " << lived_in
              << " lived-in houses warm at the end\n";
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

  core::ResourceId straw_;

  core::Definitions definitions_;

  std::uint32_t ordered_ = 0;

  std::int64_t first_order_day_ = -1;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_INSULATION_POLICY_H_
