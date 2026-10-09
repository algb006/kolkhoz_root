// The meadow's mark against its contract (core_common/meadow_mark.h; contract 0.37.210, body
// 0.37.211), counted by hand on a 1000 m map with a 10 m raster. Called from main.cpp
// (TestMeadowMarkContract).
//
// THE GROUND: the floodplain is the western strip x < 400; a forest stands in the north-east corner
// (800..1000 both ways); a pit lies at 600..650 by 100..150; the start's one field is a LONG shape,
// 500 m by 40 m (x 450..950, y 480..520, two hectares) — its row says centre (700, 500) and 2 ha, a
// circle of 79.8 m. A mark of one hectare is a circle of 56.4 m.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "core_common/event_state.h"
#include "core_common/land_state.h"
#include "core_common/map_obstacles.h"
#include "core_common/meadow_mark.h"
#include "core_common/obstacle_raster.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core_test {
namespace {

int ExpectMark(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

core::MapAreaDef Box(core::MapAreaKind kind, float x0, float y0, float x1, float y1) {
  return core::MapAreaDef{
      .key = "box",
      .kind = kind,
      .outline = {{.x = x0, .y = y0}, {.x = x1, .y = y0}, {.x = x1, .y = y1}, {.x = x0, .y = y1}}};
}

struct Fixture {
  core::MapObstacles obstacles;
  std::vector<core::StartLandShape> start_land;
  std::vector<std::vector<core::Vec2>> pits;
  std::vector<float> plot_radii = {30.0F};
  std::vector<float> stand_radii;
  core::WorldState world;
  core::FieldId long_field;
  core::FieldId fallow;
  core::FieldId sown;
};

Fixture MakeFixture() {
  Fixture f;
  f.obstacles.areas.push_back(Box(core::MapAreaKind::kFloodplain, 0.0F, 0.0F, 400.0F, 1000.0F));
  f.obstacles.areas.push_back(Box(core::MapAreaKind::kForest, 800.0F, 800.0F, 1000.0F, 1000.0F));
  f.pits.push_back(Box(core::MapAreaKind::kClayPit, 600.0F, 100.0F, 650.0F, 150.0F).outline);
  f.start_land.push_back(core::StartLandShape{
      .key = "field_long",
      .outline = Box(core::MapAreaKind::kStartField, 450.0F, 480.0F, 950.0F, 520.0F).outline});
  core::FieldRow long_field;
  long_field.center = {.x = 700.0F, .y = 500.0F};
  long_field.area_ga = 2.0F;
  long_field.rotation_assigned = 1;
  long_field.start_shape = 1;
  f.long_field = core::AppendRow(f.world.fields, long_field);
  // Two small fields far in the south-east, with no polygon: one fallow and idle, one with a chain.
  core::FieldRow fallow;
  fallow.center = {.x = 900.0F, .y = 100.0F};
  fallow.area_ga = 0.5F;
  f.fallow = core::AppendRow(f.world.fields, fallow);
  core::FieldRow sown = fallow;
  sown.center = {.x = 900.0F, .y = 300.0F};
  sown.rotation_assigned = 1;
  f.sown = core::AppendRow(f.world.fields, sown);
  return f;
}

core::MeadowMarkGround GroundOf(const Fixture& f, const core::ObstacleRaster& raster) {
  core::MeadowMarkGround ground;
  ground.raster = &raster;
  ground.start_land = &f.start_land;
  ground.pits = &f.pits;
  ground.plot_radius_m = f.plot_radii;
  ground.stand_radius_m = &f.stand_radii;
  ground.map_side_m = 1000.0F;
  ground.mow_days_per_ha = 8.0F;
  ground.dry_yield_kg_per_ha = 1500.0F;
  ground.floodplain_yield_kg_per_ha = 2500.0F;
  return ground;
}

}  // namespace

int TestMeadowMarkContract() {
  int failures = 0;
  Fixture f = MakeFixture();
  const core::ObstacleRaster raster(f.obstacles, 1000.0F, 10.0F);
  const core::MeadowMarkGround ground = GroundOf(f, raster);
  const core::FieldId none{};
  const auto ask = [&](float x, float y, float hectares) {
    return core::PreviewMeadowMark(ground, f.world, core::Vec2{.x = x, .y = y}, hectares, none);
  };
  using Why = core::MeadowMarkRefusal;

  // -- the circle against a polygon, the test itself
  // ---------------------------------------------------
  const std::vector<core::Vec2>& outline = f.start_land[0].outline;
  failures += ExpectMark(core::CircleTouchesPolygon({.x = 700.0F, .y = 500.0F}, 1.0F, outline),
                         "meadow mark: a circle inside the polygon touches it");
  failures +=
      ExpectMark(core::CircleTouchesPolygon({.x = 700.0F, .y = 560.0F}, 41.0F, outline) &&
                     !core::CircleTouchesPolygon({.x = 700.0F, .y = 560.0F}, 40.0F, outline),
                 "meadow mark: 40 m from the polygon's edge a radius of 41 touches and a "
                 "radius of 40 does not — shapes that only touch do not overlap");

  // -- what is not a place
  // ------------------------------------------------------------------------------
  failures += ExpectMark(ask(600.0F, 800.0F, 0.0F).refusal == Why::kBadArea,
                         "meadow mark: no hectares is refused as a bad area");
  failures += ExpectMark(ask(10.0F, 10.0F, 1.0F).refusal == Why::kOutsideMap,
                         "meadow mark: a circle over the map's edge is refused as outside the map");

  // -- the kind is the floodplain's share
  // ---------------------------------------------------------------
  const core::MeadowMarkAnswer dry = ask(600.0F, 800.0F, 1.0F);
  std::cout << "  meadow mark, one hectare on dry ground: refusal " << static_cast<int>(dry.refusal)
            << ", kind " << static_cast<int>(dry.kind) << ", flood share " << dry.flood_share
            << ", hay " << dry.hay_kg_a_cut << " kg, mowing " << dry.mow_man_days << " man-days\n";
  failures += ExpectMark(dry.refusal == Why::kNone && dry.kind == core::LandKind::kMeadow &&
                             dry.flood_share == 0.0F && dry.hay_kg_a_cut == 1500.0F &&
                             dry.mow_man_days == 8.0F,
                         "meadow mark: a hectare on dry ground is a dry meadow — 1 500 kg of hay a "
                         "cut, 8 man-days of mowing");
  const core::MeadowMarkAnswer wet = ask(200.0F, 300.0F, 1.0F);
  failures +=
      ExpectMark(wet.refusal == Why::kNone && wet.kind == core::LandKind::kFloodplainMeadow &&
                     wet.flood_share == 1.0F && wet.hay_kg_a_cut == 2500.0F,
                 "meadow mark: a hectare wholly on the floodplain is a floodplain meadow — "
                 "2 500 kg of hay a cut");
  const core::MeadowMarkAnswer mixed = ask(400.0F, 300.0F, 1.0F);
  std::cout << "  meadow mark, a hectare astride the floodplain's edge: flood share "
            << mixed.flood_share << '\n';
  failures += ExpectMark(mixed.refusal == Why::kMixedFloodplain && mixed.flood_share > 0.4F &&
                             mixed.flood_share < 0.6F,
                         "meadow mark: a circle half on the floodplain is neither kind, refused by "
                         "its own word with the share beside it");
  failures += ExpectMark(
      core::OrderRefusalOf(Why::kMixedFloodplain) == core::OrderRefusal::kWrongLand &&
          core::OrderRefusalOf(Why::kLand) == core::OrderRefusal::kTooClose &&
          core::OrderRefusalOf(Why::kBadArea) == core::OrderRefusal::kRuleForbids &&
          core::OrderRefusalOf(Why::kNoSuchField) == core::OrderRefusal::kNoSuchSubject &&
          core::OrderRefusalOf(Why::kAlreadyMown) == core::OrderRefusal::kConflictsWithActive,
      "meadow mark: the order's word for each of the preview's reasons");

  // -- the map's grounds
  // --------------------------------------------------------------------------------
  failures += ExpectMark(ask(780.0F, 900.0F, 1.0F).refusal == Why::kForest,
                         "meadow mark: a circle reaching into the forest is refused by the forest");
  failures += ExpectMark(ask(625.0F, 200.0F, 1.0F).refusal == Why::kPit,
                         "meadow mark: a circle reaching a pit is refused by the pit");

  // -- THE SEAM: the start's land by its polygon, not by its circle
  // ------------------------------------- 40 m north of the long field's far end: across it by the
  // map, and 209 m from the row's centre — clear by 73 m of the two circles (56.4 + 79.8).
  failures += ExpectMark(ask(900.0F, 560.0F, 1.0F).refusal == Why::kLand,
                         "meadow mark: a circle across the far end of a long start field is "
                         "refused — the polygon is asked, not the row's circle");
  // 100 m north of the field's middle: clear of the polygon, and inside the two circles' reach
  // (120 m against 136.2).
  failures += ExpectMark(ask(700.0F, 620.0F, 1.0F).refusal == Why::kNone,
                         "meadow mark: a circle clear of the polygon is taken though it lies "
                         "inside the row's old centre-and-area circle");

  // -- a unit's plot and a stand's contour
  // --------------------------------------------------------------
  core::UnitRow yard;
  yard.type = core::UnitTypeId{0};
  yard.position = {.x = 100.0F, .y = 800.0F};
  core::AppendRow(f.world.units, yard);
  failures += ExpectMark(ask(150.0F, 800.0F, 1.0F).refusal == Why::kUnit,
                         "meadow mark: a circle over a unit's plot is refused by the unit");
  core::TimberStandRow grove;
  grove.position = {.x = 600.0F, .y = 300.0F};
  core::AppendRow(f.world.stands, grove);
  f.stand_radii = {50.0F};
  failures += ExpectMark(ask(680.0F, 300.0F, 1.0F).refusal == Why::kStand,
                         "meadow mark: a circle within a stand's contour is refused by the stand");

  // -- the order, form 1
  // --------------------------------------------------------------------------------
  core::OrderRow mark;
  mark.kind = core::OrderKind::kMarkMeadow;
  mark.position = {.x = 600.0F, .y = 800.0F};
  mark.area_ha = 1.0F;
  const std::size_t rows_before = f.world.fields.rows.size();
  const core::OrderRefusal marked = core::MarkMeadow(ground, f.world, mark);
  const core::FieldRow& meadow = f.world.fields.rows.back();
  failures += ExpectMark(
      marked == core::OrderRefusal::kNone && f.world.fields.rows.size() == rows_before + 1 &&
          meadow.kind == core::LandKind::kMeadow && meadow.area_ga == 1.0F &&
          meadow.center.x == 600.0F && meadow.phase == core::FieldPhase::kGrowing &&
          meadow.start_shape == 0 && meadow.mown_fallow == 0,
      "meadow mark, the order: a dry meadow row of the hectare, growing, with "
      "no polygon");
  failures +=
      ExpectMark(!f.world.step_events.empty() &&
                     f.world.step_events.back().kind == core::EventKind::kMeadowMarked &&
                     f.world.step_events.back().amount == 100 &&
                     f.world.step_events.back().field.value == f.world.fields.row_ids.back().value,
                 "meadow mark, the order: kMeadowMarked names the row and its hectares × 100");
  // A meadow the player marked is asked as its circle: 50 m beside it, two circles of 56.4 m.
  failures += ExpectMark(ask(650.0F, 800.0F, 1.0F).refusal == Why::kLand,
                         "meadow mark: a second mark across the first is refused — a marked meadow "
                         "is asked as its circle");
  mark.position = {.x = 780.0F, .y = 900.0F};
  const std::size_t events_before = f.world.step_events.size();
  failures +=
      ExpectMark(core::MarkMeadow(ground, f.world, mark) == core::OrderRefusal::kWrongLand &&
                     f.world.fields.rows.size() == rows_before + 1 &&
                     f.world.step_events.size() == events_before,
                 "meadow mark, the order: refused in the forest as wrong land, nothing "
                 "appended and nothing said");

  // -- form 2: former arable mown as it lies
  // ------------------------------------------------------------
  const auto ask_field = [&](core::FieldId field) {
    return core::PreviewMeadowMark(ground, f.world, core::Vec2{}, 0.0F, field);
  };
  failures += ExpectMark(ask_field(core::FieldId{9999}).refusal == Why::kNoSuchField,
                         "meadow mark, form 2: a field that is not there");
  failures += ExpectMark(ask_field(f.sown).refusal == Why::kNotFallow,
                         "meadow mark, form 2: a field with a rotation is not fallow");
  const core::MeadowMarkAnswer lies = ask_field(f.fallow);
  failures += ExpectMark(lies.refusal == Why::kNone && lies.kind == core::LandKind::kMeadow &&
                             lies.hay_kg_a_cut == 750.0F && lies.mow_man_days == 4.0F,
                         "meadow mark, form 2: half a hectare of fallow is mown as dry meadow — "
                         "750 kg, 4 man-days");
  core::OrderRow mow;
  mow.kind = core::OrderKind::kMarkMeadow;
  mow.field = f.fallow;
  failures += ExpectMark(core::MarkMeadow(ground, f.world, mow) == core::OrderRefusal::kNone,
                         "meadow mark, form 2, the order: taken");
  const core::FieldRow& mown = f.world.fields.rows[core::FindRow(f.world.fields, f.fallow)];
  failures += ExpectMark(mown.kind == core::LandKind::kMeadow && mown.mown_fallow == 1 &&
                             mown.phase == core::FieldPhase::kGrowing &&
                             f.world.step_events.back().kind == core::EventKind::kMeadowMarked &&
                             f.world.step_events.back().amount == 50,
                         "meadow mark, form 2, the order: the row is a dry meadow that remembers "
                         "its field, growing, and the event says half a hectare");
  failures += ExpectMark(
      ask_field(f.fallow).refusal == Why::kAlreadyMown &&
          core::MarkMeadow(ground, f.world, mow) == core::OrderRefusal::kConflictsWithActive,
      "meadow mark, form 2: a field mown as it lies already is not marked twice");
  return failures;
}

}  // namespace core_test
