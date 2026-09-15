/// @file
/// @brief The whole building chairman a run plays, in one place: the yard,
/// the granaries and cattle yards, felling, the sawmill, the limit, repairs,
/// the houses and the digging — in the one order thirty_years settled on.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY ONE CLASS. Once the core raised no house from nothing (boss, parcel
/// 257) every run that lives past a winter needs a chairman who builds, and a
/// house needs the whole chain behind it: logs, boards, clay, a store for the
/// harvest first. Wiring eight policies by hand into six runs is six chances
/// to order them differently or to drop one, and a run with no digging is a
/// run whose houses wait for clay for ever — which reads as a demography
/// finding and is a wiring slip. thirty_years keeps its own wiring because it
/// reports on each policy and interleaves the orders policy; the order below
/// is its order.

#ifndef TESTS_RUN_COMMON_BUILDING_CHAIRMAN_H_
#define TESTS_RUN_COMMON_BUILDING_CHAIRMAN_H_

#include <cstdint>
#include <string>

#include "core_tables/tables.h"
#include "core_world/world.h"
#include "extraction_policy.h"
#include "felling_policy.h"
#include "fixture_policy.h"
#include "house_policy.h"
#include "limit_policy.h"
#include "repair_policy.h"
#include "sawmill_policy.h"
#include "school_policy.h"
#include "yard_policy.h"

namespace run {

/// @brief The chairman who builds, as one daily call.
class BuildingChairman {
 public:
  explicit BuildingChairman(const core::ITableSet& tables)
      : yard(tables),
        fixture(tables),
        felling(tables),
        sawmill(tables),
        limit(tables),
        repairs(tables),
        houses(tables),
        school(tables),
        digging(tables) {
    WireStartGates(yard, fixture, houses, sawmill);
    WireSchoolGate(school, sawmill);
  }

  /// @brief The school asks the sawmill's question too (parcel 305): the
  /// boards the saw is built of go to nobody else until it stands.
  static void WireSchoolGate(SchoolPolicy& school_policy, const SawmillPolicy& sawmill_policy) {
    school_policy.SetStartGate([&sawmill_policy](const core::WorldState& world,
                                                 core::UnitTypeId type,
                                                 std::uint8_t level) {
      return sawmill_policy.SparesBoardsFor(world, type, level);
    });
  }

  /// @brief Hands every policy that starts sites the sawmill's question: the
  /// boards it is built of go to nobody else until it stands (parcel 305).
  /// A free function of the four so thirty_years, which wires its policies
  /// by hand, asks it the same way.
  static void WireStartGates(YardPolicy& yard_policy,
                             FixturePolicy& fixture_policy,
                             HousePolicy& house_policy,
                             const SawmillPolicy& sawmill_policy) {
    const StartGate gate = [&sawmill_policy](const core::WorldState& world,
                                             core::UnitTypeId type,
                                             std::uint8_t level) {
      return sawmill_policy.SparesBoardsFor(world, type, level);
    };
    yard_policy.SetStartGate(gate);
    fixture_policy.SetStartGate(gate);
    house_policy.SetStartGate(gate);
  }

  // The start gates hold a reference to `sawmill`: a copy would ask the
  // original's sawmill, so there is none.
  BuildingChairman(const BuildingChairman&) = delete;
  BuildingChairman& operator=(const BuildingChairman&) = delete;

  /// @brief Every fixture difference, in words, BEFORE the run measures.
  static void Declare(const std::string& run_name) {
    FixturePolicy::Declare();
    FellingPolicy::Declare(run_name.c_str());
    SawmillPolicy::Declare(run_name.c_str());
    LimitPolicy::Declare(run_name.c_str());
    RepairPolicy::Declare();
    HousePolicy::Declare(run_name);
    SchoolPolicy::Declare(run_name);
    ExtractionPolicy::Declare(run_name);
  }

  /// @brief One day of the chairman's attention. Call once a day, after the
  /// day's steps.
  void RunDay(core::ISimulation& simulation) {
    yard.RunDay(simulation);
    RunDayBeyondTheYard(simulation);
  }

  /// @brief The day without the yard, for a run that holds the yard back on
  /// its own schedule (idle_curve's delayed yard).
  void RunDayBeyondTheYard(core::ISimulation& simulation) {
    fixture.RunDay(simulation);
    felling.RunDay(simulation, sawmill.LogsForMissingBoards(simulation.CompletedState()));
    sawmill.RunDay(simulation);
    limit.RunDay(simulation);
    repairs.RunDay(simulation);
    // The farm's own shortage before any house (boss, parcel 298).
    const bool farm_first = fixture.HoldsHousesBack(simulation) || yard.HoldsHousesBack(simulation);
    houses.RunDay(simulation, farm_first);
    // The school after the houses: a family without a roof comes first.
    school.RunDay(simulation, farm_first);
    digging.RunDay(simulation);
  }

  // Public on purpose: a run reports on the ones it cares about.
  YardPolicy yard;
  FixturePolicy fixture;
  FellingPolicy felling;
  SawmillPolicy sawmill;
  LimitPolicy limit;
  RepairPolicy repairs;
  HousePolicy houses;
  SchoolPolicy school;
  ExtractionPolicy digging;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_BUILDING_CHAIRMAN_H_
