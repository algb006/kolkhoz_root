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
    core::TreadTrail(full, west, east, false, rules.gain_per_pass);
  }
  core::TrailTable half;
  core::TreadTrail(half, west, east, false, 0.5F);
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
    core::TreadTrail(snow_full, west, east, true, rules.gain_per_pass);
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

  // The view: ends in metres, the visible threshold the table carries.
  const std::vector<core::TrailView> all = core::TrailViews(half, false);
  half.visible_strength = 0.6F;
  const std::vector<core::TrailView> shown = core::TrailViews(half, true);
  failures +=
      Expect(all.size() == 1 && all[0].visible && Near(all[0].from.x, 0.0F) &&
                 Near(all[0].to.x, 1000.0F) && shown.empty() &&
                 core::CountTrailRows(half).all == 1 && core::CountTrailRows(half).visible == 0,
             "trails: the view gives the ends in metres and the table's own visible "
             "threshold decides visible");
  return failures;
}

}  // namespace core_test
