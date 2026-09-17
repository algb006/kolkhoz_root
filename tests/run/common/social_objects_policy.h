/// @file
/// @brief The run's chairman builds the era's SOCIAL OBJECTS, so that the
/// transition block that counts them has a world to happen in.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THIS EXISTS, and it is the third time this tree has written the same
/// note: the office, the bathhouse, and now four more. A rule no instrument
/// can reach is not a rule, and the repair is a world where the chairman
/// builds — not a world where the building is already up.
///
/// MEASURED 2026-09-18, nine villages over thirty-three years: of the era's
/// six social objects, ONE was ever marked and one ever finished — the
/// school, which has a policy of its own. The bathhouse, the culture house,
/// the selpo, the field canteen and the stadium had no rule anywhere, so the
/// block «4 соцобъекта из 6» stood shut in every year of every village and
/// the component weighing twenty points of ninety read nought for ever.
///
/// ONE RULE OVER THE CATALOGUE, NOT FIVE POLICIES BY NAME. The list is taken
/// where the score takes it — class `social`, this era's column, no parent
/// (core_world/era_readiness.h) — so a seventh object entered in the design
/// base is one the fixture raises without anybody remembering to come back
/// here. That is what "fix the gate, not the list" means where it is true:
/// the rule is one and carries no names.
///
/// THE SCHOOL KEEPS ITS OWN POLICY and is skipped here. Its gate is measured
/// working — it marks at ten children and finishes — and its rule carries a
/// condition this one does not have (the children).
///
/// ONE AT A TIME, AND HOUSES FIRST. A village that raises five public
/// buildings at once is not a village the rest of the run was measured on.
/// The housing queue keeps its veto on MARKING a new object, but not on
/// finishing one already marked: a settlement that always has a house going
/// up would otherwise never raise anything else, which is precisely the gate
/// that kept these five at nought.

#ifndef TESTS_RUN_COMMON_SOCIAL_OBJECTS_POLICY_H_
#define TESTS_RUN_COMMON_SOCIAL_OBJECTS_POLICY_H_

#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/order_state.h"
#include "core_common/plot.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/stub_tables.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"
#include "core_world/world.h"
#include "house_policy.h"
#include "start_gate.h"

namespace run {

/// @brief Marks the era's social objects one at a time once the farm stands,
///        and starts each when its recipe is covered.
class SocialObjectsPolicy {
 public:
  explicit SocialObjectsPolicy(const core::ITableSet& tables) {
    const core::ReadinessCatalog catalog = core::ReadReadinessCatalog(tables, core::Epoch::kOne);
    wanted_ = catalog.social_objects;
    school_ = TypeByKey(tables, "school");
    house_ = TypeByKey(tables, "wooden_house");
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
  }

  /// @brief The question asked before every start (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day, after the
  ///        school and the office.
  /// @param farm_first The farm's own shortage has a site waiting for its
  ///        recipe: nothing is marked or started today.
  void RunDay(core::ISimulation& simulation, bool farm_first) {
    if (wanted_.empty()) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    // NOT BEFORE THE FARM STANDS — the same scar as the office's: a public
    // building marked in the first quiet week takes the logs and the plot the
    // chairman's yard is waiting on, and the farm stops ploughing.
    if (world.chairman.horses_stabled == 0) {
      return;
    }
    std::vector<core::OrderRow> orders;
    bool a_house_waits = !world.wedding_waits.rows.empty();
    for (const core::FamilyRow& family : world.families.rows) {
      a_house_waits = a_house_waits || family.house.value == core::kInvalidEntityIdValue;
    }
    bool one_is_going_up = false;
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value == house_.value && unit.level == 0) {
        a_house_waits = true;
      }
      if (!Wanted(unit.type) || unit.dead != 0) {
        continue;
      }
      if (unit.level == 0) {
        one_is_going_up = true;
        const bool marked = unit.construction.phase == core::ConstructionPhase::kMarked;
        // STARTED WITHOUT THE HOUSING VETO, on purpose. The veto belongs on
        // MARKING a new one; applied here it leaves a marked plot standing
        // empty for thirty-three years, which is what the measurement found.
        if (marked && !farm_first &&
            simulation.MaterialsShortFor(world.units.row_ids[row]).empty() &&
            GateOpen(start_gate_, world, unit.type, unit.construction.target_level)) {
          core::OrderRow start;
          start.kind = core::OrderKind::kStartBuild;
          start.unit = world.units.row_ids[row];
          orders.push_back(start);
          ++started_;
        }
      }
    }
    // ONE AT A TIME: nothing new is marked while one is still going up.
    if (!one_is_going_up && !farm_first && !a_house_waits) {
      const core::UnitTypeId next = NextUnbuilt(world);
      if (next.value != core::kInvalidDefIdValue) {
        core::OrderRow mark;
        mark.kind = core::OrderKind::kBuildUnit;
        mark.unit_type = next;
        const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
        const float radius = next.value < radii.size() ? radii[next.value] : 0.0F;
        mark.position = core::FreePlot(
            world.units, definitions_.Plots(), HousePolicy::VillageCentre(world), radius);
        orders.push_back(mark);
        ++marked_;
      }
    }
    if (!orders.empty()) {
      simulation.StageOrders(std::span<const core::OrderRow>(orders.data(), orders.size()), {});
    }
  }

  /// @brief The fixture difference, in words, BEFORE the run measures.
  static void Declare(std::string_view run_name) {
    std::cout << run_name
              << ": FIXTURE DIFFERS FROM THE START CANON — once the chairman's yard stands, the "
                 "run's chairman marks the era's SOCIAL OBJECTS one at a time, taking the list "
                 "from the design base by class and era rather than by name, and starts each "
                 "when its recipe is covered. The school keeps its own rule. Measured on "
                 "2026-09-18 before this existed: one of the six was ever marked in "
                 "thirty-three years of nine villages, so the transition's «4 из 6» block stood "
                 "shut in every year and its component read nought for ever (boss, blockers "
                 "round 2)\n";
  }

  std::uint32_t marked() const { return marked_; }

  std::uint32_t started() const { return started_; }

 private:
  bool Wanted(core::UnitTypeId type) const {
    for (const core::UnitTypeId id : wanted_) {
      if (id.value == type.value) {
        return true;
      }
    }
    return false;
  }

  /// The first object of the list that has neither a site nor a building.
  /// THE SCHOOL IS SKIPPED, having a policy of its own with a condition this
  /// one does not carry.
  core::UnitTypeId NextUnbuilt(const core::WorldState& world) const {
    for (const core::UnitTypeId id : wanted_) {
      if (id.value == school_.value) {
        continue;
      }
      bool exists = false;
      for (const core::UnitRow& unit : world.units.rows) {
        exists = exists || (unit.type.value == id.value && unit.dead == 0);
      }
      if (!exists) {
        return id;
      }
    }
    return core::UnitTypeId{core::kInvalidDefIdValue};
  }

  static core::UnitTypeId TypeByKey(const core::ITableSet& tables, std::string_view key) {
    const core::ITable* types = tables.FindTable("unit_types");
    if (types == nullptr) {
      return core::UnitTypeId{core::kInvalidDefIdValue};
    }
    const std::uint32_t row = types->FindRowByKey(key);
    return row == core::kNoTableRow ? core::UnitTypeId{core::kInvalidDefIdValue}
                                    : core::UnitTypeId{static_cast<std::uint16_t>(row)};
  }

  std::vector<core::UnitTypeId> wanted_;

  core::UnitTypeId school_{core::kInvalidDefIdValue};

  core::UnitTypeId house_{core::kInvalidDefIdValue};

  core::Definitions definitions_;

  StartGate start_gate_;

  std::uint32_t marked_ = 0;

  std::uint32_t started_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SOCIAL_OBJECTS_POLICY_H_
