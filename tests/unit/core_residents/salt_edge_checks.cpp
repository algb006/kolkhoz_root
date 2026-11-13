// The checks of step 4 «край» and «Без соли» (salt_edge_checks.h; #1630 (C)).
//
// THE KNOWN ANSWERS at econ's numbers: step 4 from 20 days; half the families on it 8 days running
// raise kSaltEndingDue once; a family 8 days into step 4 (count 28) leaves whole; with the death
// share at 1 every child and elder of a step-4 family dies that day, the adults do not.

#include "salt_edge_checks.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "life_config.h"
#include "salt_edge.h"
#include "salt_issue.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

std::size_t Count(const core::WorldState& world, core::EventKind kind) {
  std::size_t count = 0;
  for (const core::SimEvent& event : world.step_events) {
    count += event.kind == kind ? 1U : 0U;
  }
  return count;
}

/// Two families of two adults (30 biological years on day 0 at life x4); the
/// first family's count `days0`, the second's `days1`.
core::WorldState Village(float days0, float days1, const core::SaltIssueConfig& salt) {
  core::WorldState world;
  world.calendar.day = 200;
  for (const float days : {days0, days1}) {
    core::FamilyRow family;
    family.salt_hunger_days = days;
    family.salt_step = core::SaltStepOf(salt, days);
    core::AppendRow(world.families, family);
  }
  for (std::size_t index = 0; index < 4; ++index) {
    core::ResidentRow person;
    person.family = world.families.row_ids[index / 2];
    person.birth_day = 200 - static_cast<std::int32_t>(30U * core::kDaysPerYear / 4U);
    core::AppendRow(world.residents, person);
  }
  return world;
}

}  // namespace

int CheckTheSaltEdge() {
  int failures = 0;
  core::LifeConfig config;
  config.salt.on = true;
  failures +=
      Expect(core::SaltStepOf(config.salt, 19.9F) == 3 && core::SaltStepOf(config.salt, 20.0F) == 4,
             "salt edge: step 4 from the 20th day without salt");

  // THE RUN: one family of two on step 4 is half - 8 days raise the ending once.
  core::WorldState run = Village(21.0F, 0.0F, config.salt);
  for (int index = 0; index < 7; ++index) {
    core::RunSaltEdgeDay(config, run);
  }
  failures += Expect(run.salt.half_on_edge_days == 7 && run.salt.ending_raised == 0,
                     "salt edge: 7 days of half the families on step 4 raise nothing yet");
  run.step_events.clear();
  core::RunSaltEdgeDay(config, run);
  failures +=
      Expect(run.salt.ending_raised == 1 && Count(run, core::EventKind::kSaltEndingDue) == 1,
             "salt edge: the 8th day raises kSaltEndingDue");
  run.step_events.clear();
  core::RunSaltEdgeDay(config, run);
  failures += Expect(Count(run, core::EventKind::kSaltEndingDue) == 0,
                     "salt edge: the ending is raised once a campaign");
  // The run breaks the day the share falls under half.
  run.families.rows[0].salt_step = 3;
  core::RunSaltEdgeDay(config, run);
  failures += Expect(run.salt.half_on_edge_days == 0, "salt edge: under half breaks the run");

  // THE LEAVING: count 28 (8 days into step 4) - the family goes whole, booked.
  core::WorldState leave = Village(28.0F, 0.0F, config.salt);
  core::RunSaltEdgeDay(config, leave);
  failures +=
      Expect(leave.residents.rows.size() == 2 && leave.families.rows.size() == 1 &&
                 Count(leave, core::EventKind::kFamilyLeftForSalt) == 1 &&
                 leave.ledger.current.departures == 2 && leave.ledger.current.departures_salt == 2,
             "salt edge: 8 days into step 4 the family leaves whole, booked for the salt");

  // THE DEATHS: the share at 1, a child of 3 and an adult on step 4.
  core::LifeConfig deadly = config;
  deadly.salt.edge_death_share = 1.0F;
  core::WorldState edge = Village(21.0F, 0.0F, deadly.salt);
  edge.residents.rows[0].birth_day = 200 - static_cast<std::int32_t>(3U * core::kDaysPerYear / 4U);
  core::RunSaltEdgeDay(deadly, edge);
  bool salt_cause = false;
  for (const core::SimEvent& event : edge.step_events) {
    salt_cause = salt_cause || (event.kind == core::EventKind::kResidentDied &&
                                event.amount == static_cast<std::int64_t>(core::DeathCause::kSalt));
  }
  failures +=
      Expect(edge.residents.rows.size() == 3 && edge.ledger.current.deaths_salt == 1 &&
                 edge.ledger.current.deaths == 1 && salt_cause,
             "salt edge: the child of a step-4 family dies by the salt's cause, the adult not");

  // OFF: nothing.
  core::LifeConfig off;
  core::WorldState quiet = Village(30.0F, 30.0F, config.salt);
  core::RunSaltEdgeDay(off, quiet);
  failures += Expect(quiet.residents.rows.size() == 4 && quiet.salt.half_on_edge_days == 0,
                     "salt edge: off, nothing happens");
  return failures;
}
