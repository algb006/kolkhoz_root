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
#include "insulation_policy.h"
#include "limit_policy.h"
#include "night_pasture_policy.h"
#include "office_policy.h"
#include "repair_policy.h"
#include "sawmill_policy.h"
#include "school_policy.h"
#include "social_objects_policy.h"
#include "transition_policy.h"
#include "upgrade_policy.h"
#include "watchman_policy.h"
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
        office(tables),
        social(tables),
        upgrades(tables),
        watchman(tables),
        insulation(tables),
        digging(tables) {
    WireStartGates(yard, fixture, houses, sawmill);
    WireSchoolGate(school, sawmill);
    WireSawGate(office, sawmill);
    // The social objects ask the saw's question too: none of them is worth
    // the boards the sawmill itself is built of.
    WireSawGate(social, sawmill);
    WireSawGate(upgrades, sawmill);
    WireRiseWatches(yard, felling, limit, digging);
  }

  /// @brief Tells the felling, the limit and the digging of the step the
  /// chairman's yard waits to take, as the saw is told (rise_watch.h).
  static void WireRiseWatches(const YardPolicy& yard_policy,
                              FellingPolicy& felling_policy,
                              LimitPolicy& limit_policy,
                              ExtractionPolicy& digging_policy) {
    const RiseWatch watch = [&yard_policy](const core::WorldState& world) {
      return yard_policy.RowWaitingToRise(world);
    };
    felling_policy.SetRiseWatch(watch);
    limit_policy.SetRiseWatch(watch);
    digging_policy.SetRiseWatch(watch);
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

  /// @brief The office asks the sawmill's question too, for the same reason
  /// the school does: the boards the saw is built of go to nobody else until
  /// it stands (parcel 305).
  /// A template over the policy and not two copies of the lambda: the office
  /// and the social objects ask the saw exactly the same question, and a
  /// second copy of one question is the second home this tree keeps paying
  /// for.
  template <typename Policy>
  static void WireSawGate(Policy& policy, const SawmillPolicy& sawmill_policy) {
    policy.SetStartGate([&sawmill_policy](const core::WorldState& world,
                                          core::UnitTypeId type,
                                          std::uint8_t level) {
      return sawmill_policy.SparesBoardsFor(world, type, level);
    });
  }

  /// @brief Hands every policy that starts sites the sawmill's question: the
  /// boards it is built of go to nobody else until it stands (parcel 305).
  /// A free function of the four so thirty_years, which wires its policies
  /// by hand, asks it the same way. And the saw is told of the stable the
  /// yard waits to rise to, which no marked site carries (SetRiseWatch).
  static void WireStartGates(YardPolicy& yard_policy,
                             FixturePolicy& fixture_policy,
                             HousePolicy& house_policy,
                             SawmillPolicy& sawmill_policy) {
    const StartGate gate = [&sawmill_policy](const core::WorldState& world,
                                             core::UnitTypeId type,
                                             std::uint8_t level) {
      return sawmill_policy.SparesBoardsFor(world, type, level);
    };
    yard_policy.SetStartGate(gate);
    fixture_policy.SetStartGate(gate);
    house_policy.SetStartGate(gate);
    sawmill_policy.SetRiseWatch([&yard_policy](const core::WorldState& world) {
      return yard_policy.RowWaitingToRise(world);
    });
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
    OfficePolicy::Declare(run_name);
    SocialObjectsPolicy::Declare(run_name);
    UpgradePolicy::Declare(run_name);
    WatchmanPolicy::Declare(run_name);
    InsulationPolicy::Declare(run_name);
    ExtractionPolicy::Declare(run_name);
    TransitionPolicy::Declare(run_name);
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
    // The transition first: it asks nothing of the day's work, and a door
    // that opened last night should not wait behind a day of building.
    transition.RunDay(simulation);
    fixture.RunDay(simulation);
    felling.RunDay(simulation, sawmill.LogsForMissingBoards(simulation.CompletedState()));
    sawmill.RunDay(simulation);
    limit.RunDay(simulation);
    night_pasture.RunDay(simulation);
    repairs.RunDay(simulation);
    // The farm's own shortage before any house (boss, parcel 298).
    const bool farm_first = fixture.HoldsHousesBack(simulation) || yard.HoldsHousesBack(simulation);
    houses.RunDay(simulation, farm_first);
    // The school after the houses: a family without a roof comes first.
    school.RunDay(simulation, farm_first);
    // And the office after the school. It is the farm's FIRST ORDERED
    // building, which is a fact about who pays for it and not a claim on the
    // queue: children and roofs come first, as everywhere else here.
    office.RunDay(simulation, farm_first);
    // AND THE OFFICE IS PUT IN ORDER WHEN THE REST IS READY, because that is
    // when a chairman would be thinking of putting the question to a meeting
    // — which is the moment units rules §11 checks its wear at.
    const core::ReadinessState& readiness = simulation.CompletedState().readiness;
    office.RunMeetingUpkeep(
        simulation,
        readiness.blocks.food_variety != 0 && readiness.blocks.social_objects != 0 &&
            readiness.blocks.own_traction != 0 && readiness.blocks.wintering_two_years != 0 &&
            readiness.blocks.units_at_level != 0);
    // And the era's social objects last of the buildings, because they are
    // the least urgent of them and the most easily starved: measured before
    // this line existed, one of the six was ever marked in thirty-three years
    // (social_objects_policy.h).
    social.RunDay(simulation, farm_first);
    // The upgrades last of the buildings: «наведение порядка, а не
    // перестройка» waits behind everything that houses or feeds anybody.
    // Yesterday's order read at the unit before today's is placed. NOT the
    // book: it was swept the step the order settled (upgrade_policy.h).
    upgrades.ReadAtSubject(simulation);
    upgrades.RunDay(simulation, farm_first);
    watchman.RunDay(simulation);
    insulation.RunDay(simulation);
    digging.RunDay(simulation);
  }

  // Public on purpose: a run reports on the ones it cares about.
  YardPolicy yard;
  FixturePolicy fixture;
  FellingPolicy felling;
  SawmillPolicy sawmill;
  LimitPolicy limit;
  NightPasturePolicy night_pasture;
  RepairPolicy repairs;
  HousePolicy houses;
  SchoolPolicy school;
  OfficePolicy office;
  SocialObjectsPolicy social;
  UpgradePolicy upgrades;
  WatchmanPolicy watchman;
  InsulationPolicy insulation;
  ExtractionPolicy digging;
  TransitionPolicy transition;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_BUILDING_CHAIRMAN_H_
