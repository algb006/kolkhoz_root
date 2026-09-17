/// @file
/// @brief The run's chairman raises the KOLKHOZ's buildings to the level the
/// era transition asks of them, one at a time.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THIS EXISTS. Measured 2026-09-18, nine villages over thirty-three
/// years: ONE building of every village had ever reached level two, and it
/// was the chairman's yard rising to its stable by a rule of its own. No
/// fixture sent `kUpgradeUnit` for anything else, so the transition block
/// «все юниты эпохи доведены до 2 уровня» stood shut in every year — a
/// blocker measuring the fixture rather than the world, which is the third of
/// these this tree has found in two days.
///
/// THE FAMILIES' HOUSES ARE NOT IN IT (boss, design commit ac48054e). Two
/// lines of the design decide it and neither is about how much work it would
/// be: the funds component already says «жилые дома семей не входят», and
/// housing §6 says «уровень дома на комфорт не влияет — изба первого уровня в
/// порядке тоже 100 %». Requiring a level of something the game calls
/// unimportant is requiring nothing worth having. The measured denominator
/// fell from 180 buildings to 51 with that one reading, which is the village
/// the design's «несколько сезонов, а не десятилетие» was written about.
///
/// BY THE CATALOGUE AND NOT BY NAME, as the social objects are: every type
/// the score counts is a type this raises, so a kind added to the design base
/// is one the fixture keeps at level without anybody coming back here.
///
/// ONE AT A TIME, AND AFTER EVERYTHING ELSE. An upgrade is the least urgent
/// thing a chairman does — the design calls it «наведение порядка, а не
/// перестройка» — so it waits behind the farm's own shortage, the houses, the
/// school, the office and the social objects, and never has two going at once.

#ifndef TESTS_RUN_COMMON_UPGRADE_POLICY_H_
#define TESTS_RUN_COMMON_UPGRADE_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_common/order_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"
#include "core_world/world.h"
#include "start_gate.h"

namespace run {

/// @brief Raises one kolkhoz building a time to the era's level.
class UpgradePolicy {
 public:
  explicit UpgradePolicy(const core::ITableSet& tables) {
    const core::ReadinessCatalog catalog = core::ReadReadinessCatalog(tables, core::Epoch::kOne);
    kolkhoz_ = catalog.kolkhoz_types;
    // THE LADDER, BECAUSE MOST TYPES HAVE NONE. Of 111 types in
    // unit_levels.csv, 77 carry a single level and cannot be upgraded at all —
    // units rules §11 says so in words: «у остальных пока только первая, и это
    // значит „ещё не расписано"». Without this the policy ordered the first
    // un-upgradable building it met, was refused with kRuleForbids, returned,
    // and did the same the next day: 428 orders a village, one building
    // raised, and the world identical to a policy that never ran.
    const core::ITable* types = tables.FindTable("unit_types");
    const core::ITable* levels = tables.FindTable("unit_levels");
    if (types == nullptr || levels == nullptr) {
      return;
    }
    ladder_.assign(types->RowCount(), 0);
    const std::uint32_t unit_column = levels->FindColumn("unit");
    if (unit_column == core::kNoTableColumn) {
      return;
    }
    for (std::uint32_t row = 0; row < levels->RowCount(); ++row) {
      const std::uint32_t type_row = types->FindRowByKey(levels->CellText(row, unit_column));
      if (type_row != core::kNoTableRow && type_row < ladder_.size()) {
        ++ladder_[type_row];
      }
    }
  }

  /// @brief The question asked before every upgrade (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day, last of the
  ///        buildings.
  /// @param farm_first The farm's own shortage has a site waiting: nothing is
  ///        raised today.
  void RunDay(core::ISimulation& simulation, bool farm_first) {
    if (kolkhoz_.empty() || farm_first) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    if (world.chairman.horses_stabled == 0) {
      return;  // the same scar as the office's: not before the farm stands
    }
    // ONE UPGRADE AT A TIME — AND ONLY AN UPGRADE COUNTS.
    //
    // WRITTEN THE WRONG WAY FIRST, one hour after removing the same mistake
    // from the social objects. The first version returned when ANY unit was
    // building, and the village has a house going up on almost every day of
    // thirty-three years, so the policy never acted once: measured 1 of 50.7
    // before and 1 of 50.7 after. A veto that reads "something is being
    // built" in a settlement that always builds is a veto that never lifts.
    //
    // A unit already standing (level >= 1) and building is one being RAISED;
    // a level-nought site is a new building and none of this policy's
    // business.
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.dead == 0 && unit.level >= 1 &&
          unit.construction.phase == core::ConstructionPhase::kBuilding) {
        return;
      }
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.level == 0 || unit.dead != 0 || unit.level >= kEraLevel || !Kolkhoz(unit.type)) {
        continue;
      }
      // A type whose ladder ends here has no next level to ask for, and
      // asking is not free: the refusal ends this day's attention.
      if (unit.type.value >= ladder_.size() || ladder_[unit.type.value] < kEraLevel) {
        continue;
      }
      const auto next = static_cast<std::uint8_t>(unit.level + 1U);
      if (!GateOpen(start_gate_, world, unit.type, next)) {
        continue;
      }
      // THE NEXT LEVEL'S RECIPE, and the door for it already existed.
      //
      // This call was added, measured to change the order count by zero
      // thousandths, and removed as "a guard that cannot fail" — and that
      // reading was wrong twice over. `MaterialsShortFor` answers precisely
      // this question for a standing unit with a ladder: construction_system
      // returns `ShortfallOf(row, level + 1)` for phase kNone, level > 0 and
      // a rung left. It returned empty then because every candidate the
      // policy could reach was a SINGLE-LEVEL type, for which there is no
      // next rung to be short of — so the refusals were kRuleForbids and the
      // guard was right to pass them.
      //
      // The lesson is the older one: a number that does not move says the
      // cause is elsewhere, and it does not say WHICH elsewhere. I read a
      // missing door out of it and offered to build one that was already
      // there.
      if (!simulation.MaterialsShortFor(world.units.row_ids[row]).empty()) {
        continue;
      }
      core::OrderRow order;
      order.kind = core::OrderKind::kUpgradeUnit;
      order.unit = world.units.row_ids[row];
      const std::array<core::OrderRow, 1> one = {order};
      simulation.StageOrders(std::span<const core::OrderRow>(one.data(), one.size()), {});
      ++ordered_;
      return;
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — once the chairman's yard stands, the "
                 "run's chairman raises the KOLKHOZ's buildings to level 2 one at a time, taking "
                 "the list from the design base rather than by name; the families' houses are "
                 "NOT in it, because a house at level one in good repair is a hundred-per-cent "
                 "house (housing §6) and the funds component already excludes them. Measured "
                 "before this existed: one building per village had ever reached level 2 in "
                 "thirty-three years, and it was the yard (boss, design ac48054e)\n";
  }

  std::uint32_t ordered() const { return ordered_; }

 private:
  /// The level every Era I unit must reach for the I -> II transition (units
  /// rules §11). Not a stub: the design's own table.
  static constexpr std::uint8_t kEraLevel = 2;

  bool Kolkhoz(core::UnitTypeId type) const {
    for (const core::UnitTypeId id : kolkhoz_) {
      if (id.value == type.value) {
        return true;
      }
    }
    return false;
  }

  std::vector<core::UnitTypeId> kolkhoz_;

  /// How many levels each type has, dense by type row.
  std::vector<std::uint32_t> ladder_;

  StartGate start_gate_;

  std::uint32_t ordered_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_UPGRADE_POLICY_H_
