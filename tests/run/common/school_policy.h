/// @file
/// @brief The school the run's chairman puts up once the village has children
/// of school age and no school.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN BUILDS THIS. The core enrols pupils only at a standing school
/// and the district sends a teacher only to one (schooling.h,
/// specialist_arrival.h). The start canon gives the village no school, and a
/// run has no chairman — so no run had ever seen a pupil, a teacher or the
/// primary stage counted (boss, parcel 358: the prosthesis builds a school, the
/// first debt after the queue). The run plays the player here, as it does for
/// the houses (house_policy.h), and says so out loud.
///
/// THE RULE IS THE RUN'S, NOT THE GAME'S: nothing before the chairman's yard
/// stands (the horses stabled); then one wooden school (level 1) once ten
/// children of the enrolment ages stand in the village, marked near the
/// village's houses, and started when its recipe is covered — only while the
/// farm has no shortage waiting and the housing queue is empty: no couple
/// waiting, no family without a roof, no house going up. A family without a
/// roof comes before a classroom.

#ifndef TESTS_RUN_COMMON_SCHOOL_POLICY_H_
#define TESTS_RUN_COMMON_SCHOOL_POLICY_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/calendar.h"
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

/// @brief Marks one school when enough children wait for one, and starts it
///        when nothing the village needs more is waiting.
class SchoolPolicy {
 public:
  explicit SchoolPolicy(const core::ITableSet& tables) {
    school_ = TypeByKey(tables, "school");
    house_ = TypeByKey(tables, "wooden_house");
    std::string error;
    core::LoadDefinitions(tables, core::StubTables::kAllowed, definitions_, error);
    life_speedup_ = Knob(tables, "life", "life_speedup", life_speedup_);
    age_from_years_ = Knob(tables, "world_params", "school_enroll_age_from_years", age_from_years_);
    age_to_years_ = Knob(tables, "world_params", "school_enroll_age_to_years", age_to_years_);
  }

  /// @brief The question asked before every start (start_gate.h).
  void SetStartGate(StartGate gate) { start_gate_ = std::move(gate); }

  /// @brief One day of the chairman's attention. Call once a day, after the
  ///        houses.
  /// @param farm_first The farm's own shortage has a site waiting for its
  ///        recipe (FixturePolicy::HoldsHousesBack): nothing starts today.
  void RunDay(core::ISimulation& simulation, bool farm_first) {
    if (school_.value == core::kInvalidDefIdValue) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    // NOT BEFORE THE FARM STANDS. A school marked on the first day and
    // started in a quiet week took the logs and the plot the chairman's yard
    // was waiting on; the yard was never built, the team never came in, and
    // the farm stopped ploughing (seed 1929). The yard's milestone is in the
    // world itself: the horses stabled.
    if (world.chairman.horses_stabled == 0) {
      return;
    }
    std::vector<core::OrderRow> orders;
    bool a_school = false;
    // THE HOUSING QUEUE EMPTY, NOT MERELY A HOUSE SITE COVERED. The first cut
    // started the school whenever no marked house was short of its recipe;
    // its 130 logs and crew of twelve then took what the next houses needed,
    // and on seed 1929 the village fell from 107 to 56 by year five as
    // families left for want of a roof (claude/analysis/
    // school_prosthesis_predictions.md). A family without a roof comes first,
    // and so does every house going up.
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
      if (unit.type.value != school_.value) {
        continue;
      }
      a_school = true;
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
    if (!a_school && ChildrenOfSchoolAge(world) >= kChildrenForASchool) {
      core::OrderRow mark;
      mark.kind = core::OrderKind::kBuildUnit;
      mark.unit_type = school_;
      const std::vector<float>& radii = definitions_.units.keep_out_radius_m;
      const float radius = school_.value < radii.size() ? radii[school_.value] : 0.0F;
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
              << ": FIXTURE DIFFERS FROM THE START CANON — once the chairman's yard stands, "
                 "the run's chairman marks ONE SCHOOL when ten children of the enrolment ages "
                 "live in the village, and starts it when its recipe is covered and no farm "
                 "shortage, couple, roofless family or house site is waiting (boss, parcel 358)\n";
  }

  /// @brief What the fixture did, for the run to print at the end.
  void Report(const core::WorldState& world, std::string_view run_name) const {
    std::uint32_t standing = 0;
    std::uint32_t sites = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.type.value == school_.value) {
        standing += unit.level > 0 ? 1U : 0U;
        sites += unit.level == 0 ? 1U : 0U;
      }
    }
    std::uint32_t pupils = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      pupils += person.school.value != core::kInvalidEntityIdValue ? 1U : 0U;
    }
    std::cout << run_name << ": school — marked " << marked_ << " (first on day " << first_mark_day_
              << "), start orders " << started_ << "; " << standing << " standing, " << sites
              << " sites; " << pupils << " pupils at the end\n";
  }

  /// @brief Schools standing (level > 0).
  std::uint32_t Standing(const core::WorldState& world) const {
    std::uint32_t standing = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      standing += unit.type.value == school_.value && unit.level > 0 ? 1U : 0U;
    }
    return standing;
  }

 private:
  static constexpr std::uint32_t kChildrenForASchool = 10;

  static core::UnitTypeId TypeByKey(const core::ITableSet& tables, std::string_view key) {
    const core::ITable* types = tables.FindTable("unit_types");
    const std::uint32_t row = types == nullptr ? core::kNoTableRow : types->FindRowByKey(key);
    return row == core::kNoTableRow ? core::UnitTypeId{}
                                    : core::UnitTypeId{static_cast<std::uint16_t>(row)};
  }

  static float Knob(const core::ITableSet& tables,
                    std::string_view table,
                    std::string_view key,
                    float fallback) {
    const core::ITable* found = tables.FindTable(table);
    if (found == nullptr) {
      return fallback;
    }
    const std::uint32_t row = found->FindRowByKey(key);
    const std::uint32_t column = found->FindColumn("value");
    if (row == core::kNoTableRow || column == core::kNoTableColumn) {
      return fallback;
    }
    const float value = std::strtof(std::string(found->CellText(row, column)).c_str(), nullptr);
    return value > 0.0F ? value : fallback;
  }

  std::uint32_t ChildrenOfSchoolAge(const core::WorldState& world) const {
    std::uint32_t children = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      const float age =
          core::BiologicalAgeYears(life_speedup_, person.birth_day, world.calendar.day);
      children += age >= age_from_years_ && age < age_to_years_ ? 1U : 0U;
    }
    return children;
  }

  core::UnitTypeId school_;

  core::UnitTypeId house_;

  core::Definitions definitions_;

  float life_speedup_ = 4.0F;

  float age_from_years_ = 6.5F;

  float age_to_years_ = 11.0F;

  std::uint32_t marked_ = 0;

  std::uint32_t started_ = 0;

  std::int64_t first_mark_day_ = -1;

  StartGate start_gate_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SCHOOL_POLICY_H_
