/// @file
/// @brief «Manure is the last winter cart» in the placement (manure_last_checks.h).

#include "manure_last_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "assignment.h"
#include "core_common/world_state.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

core::AssignmentParams SnowDay(std::uint32_t horses) {
  core::AssignmentParams params;
  params.window_hours = 7.0F;
  params.walk_hours_per_km = 2.4F;
  params.harness_hours_per_km = 1.0F;
  params.people_cart_hours_per_km = params.harness_hours_per_km;
  params.travel_limit_hours = 6.0F;
  params.min_usable_hours = 1.0F;
  params.standard_day_hours = 10.0F;
  params.placement_level = 0;
  params.draught_horses = horses;
  return params;
}

core::AssignmentJob Cart(float east_m, float work_days) {
  core::AssignmentJob job;
  job.kind = core::WorkKind::kHauling;
  job.position = core::Vec2{east_m, 0.0F};
  job.work_days_remaining = work_days;
  job.harnessed = true;
  return job;
}

}  // namespace

int CheckManureIsTheLastWinterCart() {
  int failures = 0;
  // A dose on field 1 and a stand's logs on stand 9, both windowless carting.
  // Without the key the bare ids decide (field 1 < stand 9) and the dose goes
  // first: the y2 January mornings of village 1931.
  core::AssignmentJob dose = Cart(400.0F, 3.0F);
  dose.field = core::FieldId{1};
  dose.manure_dose = true;
  core::AssignmentJob logs = Cart(400.0F, 3.0F);
  logs.stand = core::TimberStandId{9};
  core::AssignmentCandidate hand;
  hand.resident_row = 0;
  hand.home = core::Vec2{0.0F, 0.0F};
  hand.efficiency = 1.0F;
  const std::vector<std::uint32_t> placed =
      core::PlanDayAssignments({dose, logs}, {hand}, SnowDay(1));
  failures += Expect(placed.size() == 1 && placed[0] == 1,
                     "manure last: the one horse goes to the stand's logs, not the dose");
  // And with the logs gone the dose takes the horse: last, not never.
  const std::vector<std::uint32_t> alone = core::PlanDayAssignments({dose}, {hand}, SnowDay(1));
  failures += Expect(alone.size() == 1 && alone[0] == 0,
                     "manure last: with no other cart the dose is carted");
  return failures;
}
