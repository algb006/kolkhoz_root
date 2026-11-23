/// @file
/// @brief #2072 trodden paths: the rules of core_common/trail_state.h and the one door (every
///        walking query reads the trailed index), each assertion naming its breakage.

#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "core_common/road_route.h"
#include "core_common/road_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/trail_state.h"
#include "core_common/world_state.h"

namespace {

int Expect(bool condition, std::string_view what) {
  if (!condition) {
    std::cout << "FAIL: " << what << '\n';
    return 1;
  }
  return 0;
}

bool Near(float value, float expected) {
  return std::abs(value - expected) < 0.002F;
}

core::RoadTable OneRoad(core::RoadKind kind, core::Vec2 from, core::Vec2 to) {
  core::RoadTable roads;
  core::RoadRow row;
  row.kind = kind;
  row.axis.push_back(core::RoadPoint{.position = from});
  row.axis.push_back(core::RoadPoint{.position = to});
  core::AppendRow(roads, row);
  return roads;
}

/// Treads every lattice edge the straight line from `from` to `to` crosses, sampled as the
/// query door samples it (every half cell) - a trail laid along the line.
void TreadLine(core::TrailTable& trails, core::Vec2 from, core::Vec2 to, bool snow, float gain) {
  const float length = std::hypot(to.x - from.x, to.y - from.y);
  const auto pieces = static_cast<int>(std::ceil(length / (core::kTrailSnapMetres / 2.0F)));
  core::TrailEnd here = core::SnapTrailEnd(from);
  for (int piece = 1; piece <= pieces; ++piece) {
    const float share = static_cast<float>(piece) / static_cast<float>(pieces);
    const core::TrailEnd next = core::SnapTrailEnd(
        {.x = from.x + ((to.x - from.x) * share), .y = from.y + ((to.y - from.y) * share)});
    if (next.x != here.x || next.y != here.y) {
      core::TreadTrailEdge(trails, here, next, snow, gain);
      here = next;
    }
  }
}

}  // namespace

namespace core_test {

int TestTrails() {
  int failures = 0;
  const core::TrailRules rules;
  const core::Vec2 west{.x = 0.0F, .y = 500.0F};
  const core::Vec2 east{.x = 1000.0F, .y = 500.0F};

  // Rule 1, and the key: a leg walked either way is one row; the gain caps at 1.
  core::TrailTable trails;
  core::TreadTrail(trails, west, east, false, rules.gain_per_pass);
  core::TreadTrail(trails, east, west, false, rules.gain_per_pass);
  const core::TrailRow* const row = core::FindTrail(trails, east, west);
  failures += Expect(trails.rows.size() == 1 && row != nullptr && Near(row->earth, 0.04F),
                     "trails: a leg walked both ways is ONE row with two passes (a key that "
                     "kept the direction would make two)");
  for (int pass = 0; pass < 60; ++pass) {
    core::TreadTrail(trails, west, east, false, rules.gain_per_pass);
  }
  failures += Expect(trails.rows[0].earth == 1.0F, "trails: the strength caps at 1");
  core::TreadTrail(trails, west, {.x = 3.0F, .y = 502.0F}, false, rules.gain_per_pass);
  failures += Expect(trails.rows.size() == 1,
                     "trails: a leg whose two ends snap to one cell treads nothing");

  // Rules 2-3: decay, and gone completely at 0.
  core::TrailTable fading;
  core::TreadTrail(fading, west, east, false, 0.015F);
  core::DecayTrails(fading, rules, false, false);
  const bool one_left = fading.rows.size() == 1 && Near(fading.rows[0].earth, 0.005F);
  core::DecayTrails(fading, rules, false, false);
  failures += Expect(one_left && fading.rows.empty(),
                     "trails: earth decays by its day's rate and the row is DELETED at 0 - no "
                     "memory of a former trail");

  // Rule 4: snow lives under snow, and a snowfall or the thaw erases it.
  core::TrailTable winter;
  core::TreadTrail(winter, west, east, true, 0.5F);
  core::TreadTrail(winter, west, east, false, 0.5F);
  core::DecayTrails(winter, rules, true, false);
  const bool kept = Near(winter.rows[0].snow, 0.48F) && Near(winter.rows[0].earth, 0.49F);
  core::DecayTrails(winter, rules, true, true);
  const bool snowed_over = winter.rows.size() == 1 && winter.rows[0].snow == 0.0F;
  core::TreadTrail(winter, west, east, true, 0.5F);
  core::DecayTrails(winter, rules, false, false);
  failures += Expect(kept && snowed_over && winter.rows[0].snow == 0.0F,
                     "trails: snow strength decays under lying snow, and a snowfall and the "
                     "thaw each erase it while the earth part stays");

  // The weight, inside the index: open ground 1.2, a full trail 1.10, a half one 1.15.
  const core::RoadTable far_road =
      OneRoad(core::RoadKind::kRoad, {.x = 0.0F, .y = 0.0F}, {.x = 1000.0F, .y = 0.0F});
  const auto bare = core::BuildRoadIndex(far_road);
  core::TrailTable full;
  for (int pass = 0; pass < 50; ++pass) {
    TreadLine(full, west, east, false, rules.gain_per_pass);
  }
  core::TrailTable half;
  TreadLine(half, west, east, false, 0.5F);
  const core::RoadBeds dry{};  // every bed dry: the ground neutral
  const auto trailed = core::WithTrails(bare, full, dry);
  const float open = bare->EffectiveKm(core::TravelMode::kWalk, west, east);
  const float on_full = trailed->EffectiveKm(core::TravelMode::kWalk, west, east);
  const float on_half =
      core::WithTrails(bare, half, dry)->EffectiveKm(core::TravelMode::kWalk, west, east);
  failures += Expect(Near(open, 1.2F) && Near(on_full, 1.10F) && Near(on_half, 1.15F),
                     "trails: a walker's open-ground km weighs 1.2 bare, 1.10 on a full trail, "
                     "1.15 on a half one");
  // Rule 8: a log cart over the same ground is not helped.
  failures += Expect(Near(trailed->EffectiveKm(core::TravelMode::kLogCart, west, east),
                          bare->EffectiveKm(core::TravelMode::kLogCart, west, east)),
                     "trails: a log cart over a trodden leg weighs what it did - people only");

  // Rule 6: a laid path along the same line beats the full trail.
  core::RoadTable with_path = far_road;
  core::RoadRow path;
  path.kind = core::RoadKind::kPath;
  path.axis.push_back(core::RoadPoint{.position = west});
  path.axis.push_back(core::RoadPoint{.position = east});
  core::AppendRow(with_path, path);
  const float laid =
      core::BuildRoadIndex(with_path, full, dry)->EffectiveKm(core::TravelMode::kWalk, west, east);
  failures += Expect(laid < on_full - 0.05F,
                     "trails: a laid path on the same line beats a full trail (1.0 against "
                     "1.10) - the laying decision is not given away");

  // THE ONE DOOR (econ's review): RoadKm, the found-place query and Measure all read the
  // index the world holds; re-trailing an already trailed index starts from the bare one.
  core::WorldState world;
  world.roads = far_road;
  world.trails = full;
  world.road_index = core::BuildRoadIndex(world.roads, world.trails, dry);
  const auto index = core::RoadIndexOf(world);
  const core::NetworkPlace from = index->Locate(core::TravelMode::kWalk, west);
  const core::NetworkPlace to = index->Locate(core::TravelMode::kWalk, east);
  failures += Expect(
      Near(core::RoadKm(world, core::TravelMode::kWalk, west, east), 1.10F) &&
          Near(index->EffectiveKm(from, to), 1.10F) &&
          Near(core::RoadMeasure(world, core::TravelMode::kWalk, west, east).effective_km, 1.10F) &&
          Near(index->Way(core::TravelMode::kWalk, west, east).effective_km, 1.10F),
      "trails: RoadKm, the found-place query, Measure and Way all read the trails - one door "
      "(a reader of the bare index would answer 1.2)");
  const auto emptied = core::WithTrails(world.road_index, core::TrailTable{}, dry);
  failures += Expect(Near(emptied->EffectiveKm(core::TravelMode::kWalk, west, east), 1.2F),
                     "trails: re-trailing a trailed index with no trail gives the bare network "
                     "back, not the old trails twice");

  // THE GROUND (roads.md §3), in the same index: a leg's cost is its weight ÷ the factor.
  core::RoadBeds mud{};
  mud.condition[static_cast<std::size_t>(core::RoadBed::kDirt)] = core::RoadCondition::kMud;
  core::RoadBeds snow{};
  snow.condition.fill(core::RoadCondition::kSnow);
  const core::Vec2 on_road_a{.x = 100.0F, .y = 0.0F};
  const core::Vec2 on_road_b{.x = 900.0F, .y = 0.0F};
  const auto in_mud = core::WithTrails(bare, core::TrailTable{}, mud);
  const auto in_snow_bare = core::WithTrails(bare, core::TrailTable{}, snow);
  const auto in_snow_trail = core::WithTrails(bare, full, snow);  // the trail is earth...
  core::TrailTable snow_full;
  for (int pass = 0; pass < 50; ++pass) {
    TreadLine(snow_full, west, east, true, rules.gain_per_pass);
  }
  const auto on_snow_trail = core::WithTrails(bare, snow_full, snow);
  failures += Expect(
      Near(in_mud->EffectiveKm(core::TravelMode::kWalk, west, east), 1.6F) &&
          Near(in_mud->EffectiveKm(core::TravelMode::kWalk, on_road_a, on_road_b), 0.8F / 0.75F) &&
          Near(in_snow_bare->EffectiveKm(core::TravelMode::kWalk, west, east), 2.0F) &&
          Near(in_snow_bare->EffectiveKm(core::TravelMode::kWalk, on_road_a, on_road_b),
               0.8F / 0.9F) &&
          Near(on_snow_trail->EffectiveKm(core::TravelMode::kWalk, west, east), 1.10F / 0.9F) &&
          Near(in_snow_trail->EffectiveKm(core::TravelMode::kWalk, west, east), 1.10F / 0.6F),
      "ground: open ground in mud 1.2 ÷ 0.75, a dirt road in mud 0.8 ÷ 0.75; open snow 1.2 ÷ 0.6, "
      "a road in snow as wet; a snow trail as wet ground, an earth trail under snow as snow");
  failures += Expect(Near(in_mud->EffectiveKm(core::TravelMode::kCart, on_road_a, on_road_b),
                          bare->EffectiveKm(core::TravelMode::kCart, on_road_a, on_road_b)),
                     "ground: a cart is not slowed here - the carts' own road rules stand");
  core::WorldState wet_world;
  wet_world.roads = far_road;
  wet_world.weather.road_beds = mud;
  wet_world.road_index = core::BuildRoadIndex(wet_world.roads, wet_world.trails, mud);
  // REACH IS A DISTANCE (boss 23 Nov; leisure.md §4): in the mud the walk is 1.6 km, the reach
  // still 1.2; and a trail still shortens the reach (1.10), as the walk's door does.
  core::WorldState trodden_wet = wet_world;
  trodden_wet.trails = full;
  trodden_wet.road_index = core::BuildRoadIndex(trodden_wet.roads, trodden_wet.trails, mud);
  failures += Expect(
      Near(core::RoadKm(wet_world, core::TravelMode::kWalk, west, east), 1.6F) &&
          Near(core::ReachKm(wet_world, core::TravelMode::kWalk, west, east), 1.2F) &&
          Near(core::ReachKm(trodden_wet, core::TravelMode::kWalk, west, east), 1.10F) &&
          Near(core::ReachMeasure(trodden_wet, core::TravelMode::kWalk, west, east).effective_km,
               1.10F),
      "reach: the walk in mud is slowed (1.6) but the reach is not (1.2), and a trail still "
      "shortens the reach (1.10) - the rain does not move the beach farther");
  failures += Expect(
      core::GroundStateAt(wet_world, {.x = 500.0F, .y = 1.0F}) == core::GroundState::kMud &&
          core::GroundStateAt(wet_world, {.x = 500.0F, .y = 300.0F}) == core::GroundState::kMud &&
          core::GroundStateAt(core::WorldState{}, {}) != core::GroundState::kIce,
      "ground: the state at a point on a dirt road and off it reads the dirt bed; "
      "never ice");

  // The view: one lattice edge, its ends in metres; the visible threshold the table carries.
  core::TrailTable edge;
  core::TreadTrailEdge(edge, {.x = 4, .y = 20}, {.x = 5, .y = 20}, false, 0.5F);
  const std::vector<core::TrailView> all = core::TrailViews(edge, false);
  edge.visible_strength = 0.6F;
  const std::vector<core::TrailView> shown = core::TrailViews(edge, true);
  failures +=
      Expect(all.size() == 1 && all[0].visible && Near(all[0].from.x, 100.0F) &&
                 Near(all[0].to.x, 125.0F) && Near(all[0].from.y, 500.0F) && shown.empty() &&
                 core::CountTrailRows(edge).all == 1 && core::CountTrailRows(edge).visible == 0,
             "trails: the view gives an edge's ends in metres (25 m cells) and the table's own "
             "visible threshold decides visible");

  // THE LATTICE (#2072 (A)): the walker ROUTES. A full trail one cell north of his line
  // (y = 525, x 25..975) carries him: a diagonal step onto it, 950 m at 1.10, a step off -
  // about 1.13 km against the straight 1.2. He takes the trail, and treads its edges.
  // (A SLANTED trail is the lattice's weak case: an 8-neighbour path runs up to 8.2 % over
  // the line it follows, about the full trail's whole discount, 1.2 -> 1.10 - named in the
  // delivery, not hidden in this test.)
  core::WorldState lattice_world;  // the far road only: open ground weighs 1.2
  lattice_world.roads = far_road;
  for (int pass = 0; pass < 50; ++pass) {
    TreadLine(lattice_world.trails,
              {.x = 25.0F, .y = 525.0F},
              {.x = 975.0F, .y = 525.0F},
              false,
              rules.gain_per_pass);
  }
  lattice_world.road_index = core::BuildRoadIndex(lattice_world.roads, lattice_world.trails, dry);
  std::vector<std::pair<core::TrailEnd, core::TrailEnd>> steps;
  const float routed = core::WalkRouteKm(lattice_world, west, east, &steps);
  std::size_t on_trail = 0;
  for (const auto& [a, b] : steps) {
    on_trail += core::FindTrailEdge(lattice_world.trails, a, b) != nullptr ? 1U : 0U;
  }
  const float straight = core::RoadKm(lattice_world, core::TravelMode::kWalk, west, east);
  failures += Expect(routed < 1.14F && routed > 1.12F && !steps.empty() &&
                         on_trail * 10 >= steps.size() * 8 && Near(straight, 1.2F),
                     "lattice: the walker steps onto a trail a cell off his line (1.13 km, most "
                     "steps on trail edges) where the query's straight door still answers 1.2 - "
                     "the two doors, named");
  // Untrodden ground: the walker's route costs EXACTLY the straight line (the octile excess
  // normalised out), so the lattice moves nobody's time before a trail exists.
  core::WorldState bare_world;
  bare_world.roads = far_road;
  bare_world.road_index = core::BuildRoadIndex(bare_world.roads, bare_world.trails, dry);
  std::vector<std::pair<core::TrailEnd, core::TrailEnd>> first_steps;
  const float untrodden =
      core::WalkRouteKm(bare_world, west, {.x = 700.0F, .y = 900.0F}, &first_steps);
  failures += Expect(
      Near(untrodden,
           core::RoadKm(bare_world, core::TravelMode::kWalk, west, {.x = 700.0F, .y = 900.0F})) &&
          !first_steps.empty(),
      "lattice: with no trail the walker's route is the straight line's km exactly, and he "
      "still gets lattice steps to tread (the first trail can begin)");
  return failures;
}

}  // namespace core_test
