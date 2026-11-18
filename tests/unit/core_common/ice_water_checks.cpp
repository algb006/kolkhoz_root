// What water stands at a point, as the ice reads it (#1859; core_common/ice_water.h).
// Called from main.cpp (TestTheIceWater).
//
// THE KNOWN ANSWERS, on a hand-drawn map: a 200 m square lake, a backwater square beside it,
// and a river line 10 m wide (half width 5) running into the lake's west shore. Inside the
// channel - river; in the lake far from the mouth - still; in the lake within 30 m of the
// channel's edge - inflow; in the backwater - backwater; outside everything - none.
#include <iostream>

#include "core_common/ice_water.h"
#include "core_common/map_obstacles.h"

namespace core_test {
namespace {

int ExpectIce(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: ice water: " << label << '\n';
  return 1;
}

core::MapAreaDef Square(core::MapAreaKind kind, float x0, float y0, float side) {
  core::MapAreaDef area;
  area.key = "square";
  area.kind = kind;
  area.outline = {core::Vec2{.x = x0, .y = y0},
                  core::Vec2{.x = x0 + side, .y = y0},
                  core::Vec2{.x = x0 + side, .y = y0 + side},
                  core::Vec2{.x = x0, .y = y0 + side}};
  return area;
}

}  // namespace

int TestTheIceWater() {
  core::MapObstacles map;
  map.areas.push_back(Square(core::MapAreaKind::kLake, 0.0F, 0.0F, 200.0F));
  map.areas.push_back(Square(core::MapAreaKind::kBackwater, 300.0F, 0.0F, 100.0F));
  core::MapLineDef river;
  river.key = "river";
  river.kind = core::MapLineKind::kRiver;
  river.points = {
      core::MapLinePoint{.position = core::Vec2{.x = -500.0F, .y = 100.0F}, .half_width_m = 5.0F},
      core::MapLinePoint{.position = core::Vec2{.x = 0.0F, .y = 100.0F}, .half_width_m = 5.0F}};
  map.lines.push_back(river);
  constexpr float kDisc = 30.0F;
  int failures = 0;
  failures += ExpectIce(
      core::IceWaterAt(map, core::Vec2{.x = -200.0F, .y = 103.0F}, kDisc) == core::IceWater::kRiver,
      "3 m off the axis of a 10 m channel is the river");
  failures += ExpectIce(
      core::IceWaterAt(map, core::Vec2{.x = -200.0F, .y = 120.0F}, kDisc) == core::IceWater::kNone,
      "20 m off the axis, on land, is no water");
  failures += ExpectIce(
      core::IceWaterAt(map, core::Vec2{.x = 20.0F, .y = 100.0F}, kDisc) == core::IceWater::kInflow,
      "in the lake 15 m past the channel's edge is the inflow");
  failures += ExpectIce(
      core::IceWaterAt(map, core::Vec2{.x = 150.0F, .y = 150.0F}, kDisc) == core::IceWater::kStill,
      "the lake's far side is still water");
  failures += ExpectIce(core::IceWaterAt(map, core::Vec2{.x = 350.0F, .y = 50.0F}, kDisc) ==
                            core::IceWater::kBackwater,
                        "inside the backwater is the backwater");
  failures += ExpectIce(
      core::IceWaterAt(map, core::Vec2{.x = 250.0F, .y = 50.0F}, kDisc) == core::IceWater::kNone,
      "between the two bodies is dry land");
  failures += ExpectIce(
      core::IceWaterAt(core::MapObstacles{}, core::Vec2{}, kDisc) == core::IceWater::kNone,
      "a map with nothing on it is dry land everywhere");
  if (failures == 0) {
    std::cout << "ice water: 7 points read as drawn\n";
  }
  return failures;
}

}  // namespace core_test
