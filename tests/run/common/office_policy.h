/// @file
/// @brief The run's chairman builds ONE farm office, so that a rule waiting on
/// an office has a world to happen in.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THIS EXISTS AT ALL, and it is not "the canon forgot an office".
///
/// Electrification (district_limit.h, RunEraEvents) waits on three blockers,
/// and one of them is an office standing: it is the first building the farm
/// ORDERS rather than raises, and an order needs an address, papers and
/// somebody for the district to talk to — not the corner of a church annexe
/// (electricity design §3). The start canon ships NO office on purpose, and
/// the chairman begins in `church_store`, "Церковь: склад и кабинет". Adding
/// one to the canon would cancel the blocker the condition was written for:
/// the first ordered building would stop being the first.
///
/// SO THE CANON IS RIGHT AND THE RUN WAS POOR. Measured 2026-09-17: the
/// fixtures raise granaries, cattle yards, clamps, food stores, the church
/// store, a horse yard, a school and houses, and that is the whole list — so
/// the era event fired in the unit acceptance and in no measured world. A
/// rule that no instrument can reach is the shape this tree keeps finding,
/// and the repair is a world where the chairman builds, not a world where the
/// building is already up.
///
/// IN EVERY RUN THAT HAS A CHAIRMAN, not only the ones that need it. Putting
/// a capability in some worlds and not others is how two of the three
/// instrument failures of 2026-09-17 happened — `event_journal` has no
/// chairman at all and `food_year` had no office — and the saving goes to
/// whoever skips it while the bill goes to whoever later asks a run for
/// something its world does not contain.

#ifndef TESTS_RUN_COMMON_OFFICE_POLICY_H_
#define TESTS_RUN_COMMON_OFFICE_POLICY_H_

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
#include "core_world/world.h"
#include "house_policy.h"
#include "start_gate.h"

namespace run {

/// @brief Marks one farm office once the farm stands, and starts it when
///        nothing the village needs more is waiting.
class OfficePolicy {
 public:
  explicit OfficePolicy(const core::ITableSet& tables) {
    office_ = TypeByKey(tables, "farm_office");
    house_ = TypeByKey(tables, "wooden_house");
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
  }

  /// @brief The question asked before every start (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day, after the
  ///        school.
  /// @param farm_first The farm's own shortage has a site waiting for its
  ///        recipe: nothing starts today.
  void RunDay(core::ISimulation& simulation, bool farm_first) {
    if (office_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    // NOT BEFORE THE FARM STANDS, and the school's own scar is the reason:
    // a building marked on the first day and started in a quiet week took the
    // logs and the plot the chairman's yard was waiting on, the yard was
    // never built, the team never came in, and the farm stopped ploughing.
    // The yard's milestone is in the world itself — the horses stabled.
    if (world.chairman.horses_stabled == 0) {
      return;
    }
    std::vector<core::OrderRow> orders;
    bool an_office = false;
    // A FAMILY WITHOUT A ROOF COMES FIRST, and so does every house going up.
    // The office is the first building the farm ORDERS; that is not the same
    // as the first it needs, and the design's own order of care puts people
    // before papers.
    bool a_house_waits = !world.wedding_waits.rows.empty();
    for (const core::FamilyRow& family : world.families.rows) {
      a_house_waits = a_house_waits || family.house.value == core::kInvalidEntityIdValue;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const bool marked =
          unit.level == 0 && unit.construction.phase == core::ConstructionPhase::kMarked;
      if (unit.type.value == house_.value && unit.level == 0) {
        a_house_waits = true;
      }
      if (unit.type.value != office_.value) {
        continue;
      }
      an_office = true;
      if (marked && !farm_first && !a_house_waits &&
          simulation.MaterialsShortFor(world.units.row_ids[row]).empty() &&
          GateOpen(start_gate_, world, unit.type, unit.construction.target_level)) {
        core::OrderRow start;
        start.kind = core::OrderKind::kStartBuild;
        start.unit = world.units.row_ids[row];
        orders.push_back(start);
        ++started_;
      }
    }
    if (!an_office) {
      core::OrderRow mark;
      mark.kind = core::OrderKind::kBuildUnit;
      mark.unit_type = office_;
      const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
      const float radius = office_.value < radii.size() ? radii[office_.value] : 0.0F;
      mark.position = core::FreePlot(
          world.units, definitions_.Plots(), HousePolicy::VillageCentre(world), radius);
      orders.push_back(mark);
      ++marked_;
      if (first_mark_day_ < 0) {
        first_mark_day_ = static_cast<std::int64_t>(world.calendar.day);
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
                 "run's chairman marks ONE FARM OFFICE and starts it when its recipe is covered "
                 "and no farm shortage, couple, roofless family or house site is waiting. THE "
                 "CANON SHIPS NO OFFICE ON PURPOSE — the chairman begins in the church annexe, "
                 "and the office being the farm's FIRST ORDERED building is precisely what "
                 "electrification's third blocker stands on (electricity design §3). Do not "
                 "'fix' this by putting an office in the start layout: that would cancel the "
                 "blocker rather than satisfy it\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(const core::WorldState& world, std::string_view run_name) const {
    std::uint32_t standing = 0;
    std::uint32_t sites = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == office_.value) {
        standing += unit.level > 0 ? 1U : 0U;
        sites += unit.level == 0 ? 1U : 0U;
      }
    }
    std::cout << run_name << ": office — marked " << marked_ << " (first on day " << first_mark_day_
              << "), start orders " << started_ << "; " << standing << " standing, " << sites
              << " sites; electrification "
              << (world.era_events.electrification_unlocked != 0 ? "came" : "did not come") << '\n';
  }

  /// @brief Offices standing (level > 0).
  std::uint32_t Standing(const core::WorldState& world) const {
    std::uint32_t standing = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      standing += unit.type.value == office_.value && unit.level > 0 ? 1U : 0U;
    }
    return standing;
  }

 private:
  static core::UnitTypeId TypeByKey(const core::ITableSet& tables, std::string_view key) {
    const core::ITable* types = tables.FindTable("unit_types");
    const std::uint32_t row = types == nullptr ? core::kNoTableRow : types->FindRowByKey(key);
    return row == core::kNoTableRow ? core::UnitTypeId{}
                                    : core::UnitTypeId{static_cast<std::uint16_t>(row)};
  }

  core::UnitTypeId office_;

  core::UnitTypeId house_;

  core::Definitions definitions_;

  std::uint32_t marked_ = 0;

  std::uint32_t started_ = 0;

  std::int64_t first_mark_day_ = -1;

  StartGate start_gate_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_OFFICE_POLICY_H_
