/// @file
/// @brief The morning's tread (trail_tread_checks.h).

#include "trail_tread_checks.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "assignment.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/trail_state.h"
#include "core_common/world_state.h"
#include "trail_tread.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// A field 800 m from a home, a road far south (open ground weighs 1.2), one resident placed
/// on the field with `kind`.
core::WorldState FieldDay(core::WorkKind kind) {
  core::WorldState world;
  core::RoadRow road;
  road.axis.push_back(core::RoadPoint{.position = {.x = 0.0F, .y = -2000.0F}});
  road.axis.push_back(core::RoadPoint{.position = {.x = 2000.0F, .y = -2000.0F}});
  core::AppendRow(world.roads, road);
  core::FieldRow field;
  field.center = {.x = 900.0F, .y = 100.0F};
  field.area_ga = 0.0F;  // no disc: rule 9 cannot bite here
  const core::FieldId field_id = core::AppendRow(world.fields, field);
  core::ResidentRow person;
  person.work.kind = kind;
  person.work.field = field_id;
  core::AppendRow(world.residents, person);
  world.road_index = core::BuildRoadIndex(world.roads, world.trails, world.weather.road_beds);
  return world;
}

}  // namespace

int CheckTheTread() {
  int failures = 0;
  core::TrailRules rules;
  std::vector<core::AssignmentCandidate> candidates(1);
  candidates[0].resident_row = 0;
  candidates[0].home = {.x = 100.0F, .y = 100.0F};
  const std::vector<std::uint32_t> plan = {0};

  // A SOWER WALKS (no brigade cart out): he treads, and the morning's index is NOT re-dated -
  // today's tread changes tomorrow's ways (static review: it changed today's travel time).
  core::WorldState sowing = FieldDay(core::WorkKind::kSowing);
  const std::shared_ptr<const core::RoadIndex> before = sowing.road_index;
  core::TreadPlacedWalkers(rules, sowing, candidates, plan, core::LivestockKindId{});
  failures += Expect(!sowing.trails.rows.empty() && sowing.road_index == before,
                     "tread: a sower on foot treads his way, and the day's index stays the "
                     "morning's (re-dated only by the next morning)");
  core::DecayTrailsThisMorning(rules, sowing);
  failures += Expect(sowing.road_index != before,
                     "tread: the next morning's re-dating puts yesterday's tread into effect");

  // A PLOUGHMAN RIDES (RidesOut): he treads nothing - rule 8 by the world's own predicate.
  core::WorldState ploughing = FieldDay(core::WorkKind::kPlowing);
  core::TreadPlacedWalkers(rules, ploughing, candidates, plan, core::LivestockKindId{});
  failures += Expect(ploughing.trails.rows.empty(),
                     "tread: a ploughman rides to his field (WorkRidesOut) and treads nothing - "
                     "the filter that read only rides_horse let the brigade cart's riders tread");
  return failures;
}
