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
  // PENDING, NOT LAID (save 229): the trails stay the morning's all day, so a mid-day index
  // (the loader's, a laid road's) equals the morning's.
  failures += Expect(
      !sowing.walks.pending.empty() && sowing.trails.rows.empty() && sowing.road_index == before,
      "tread: a sower on foot treads his way into the pending passes, the trails "
      "and the day's index stay the morning's (laid only by the next morning)");
  // The same passes laid at once: what the next morning must leave before its decay.
  core::TrailTable laid_now;
  for (const core::TrailPass& pass : sowing.walks.pending) {
    for (std::uint32_t at = 0; at < pass.passes; ++at) {
      core::TreadTrailEdge(
          laid_now, pass.a, pass.b, sowing.walks.pending_over_snow, rules.gain_per_pass);
    }
  }
  core::DecayTrails(laid_now, rules, false, false);
  const std::size_t passes = sowing.walks.pending.size();
  core::DecayTrailsThisMorning(rules, sowing, {});
  bool same = sowing.trails.rows.size() == laid_now.rows.size() && passes > 0;
  for (std::size_t at = 0; same && at < laid_now.rows.size(); ++at) {
    same = sowing.trails.rows[at].earth == laid_now.rows[at].earth &&
           sowing.trails.rows[at].a.x == laid_now.rows[at].a.x &&
           sowing.trails.rows[at].b.y == laid_now.rows[at].b.y;
  }
  failures += Expect(same && sowing.walks.pending.empty() && sowing.road_index != before,
                     "tread: the next morning lays yesterday's passes, then decays - row for row "
                     "the trails treading them at once would leave - and re-dates the index");

  // A PLOUGHMAN RIDES (RidesOut): he treads nothing - rule 8 by the world's own predicate.
  core::WorldState ploughing = FieldDay(core::WorkKind::kPlowing);
  core::TreadPlacedWalkers(rules, ploughing, candidates, plan, core::LivestockKindId{});
  failures += Expect(ploughing.walks.pending.empty() && ploughing.trails.rows.empty(),
                     "tread: a ploughman rides to his field (WorkRidesOut) and treads nothing - "
                     "the filter that read only rides_horse let the brigade cart's riders tread");

  // OPTION 1 (boss 24 Nov): yesterday's walk is priced by the walker's route on the new
  // morning's index, and the planner's door answers it; a pair nobody walked is not there.
  {
    core::WorldState trodden = FieldDay(core::WorkKind::kSowing);
    const core::Vec2 home = candidates[0].home;
    const core::Vec2 field = trodden.fields.rows[0].center;
    for (int day = 0; day < 30; ++day) {
      core::TreadPlacedWalkers(rules, trodden, candidates, plan, core::LivestockKindId{});
      core::DecayTrailsThisMorning(rules, trodden, {});
    }
    core::DecayTrailsThisMorning(rules, trodden, {core::WalkPair{.from = home, .to = field}});
    const float* const km = core::PlacedWalkKm(*trodden.road_index, home, field);
    const float route = core::WalkRouteKm(trodden, home, field, nullptr);
    const float* const none =
        core::PlacedWalkKm(*trodden.road_index, home, core::Vec2{.x = 0.0F, .y = 0.0F});
    failures += Expect(km != nullptr && *km == route && none == nullptr,
                       "option 1: yesterday's walk answers the planner its route's km exactly, "
                       "a pair nobody walked answers nothing");
  }
  return failures;
}
