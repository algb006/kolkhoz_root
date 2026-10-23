/// @file
/// @brief Unit cases of the graves (burial_checks.h).

#include "burial_checks.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <utility>

#include "burial.h"
#include "core_common/cemetery_rules.h"
#include "core_common/event_state.h"
#include "core_common/plot.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

// Type 0 is the cemetery (plot 25 m), type 1 a house (plot 6 m).
constexpr std::array<float, 2> kRadii = {25.0F, 6.0F};
constexpr std::uint16_t kFull = 130;  // floor(pi * 25^2 / 15)

core::BurialConfig Config() {
  core::BurialConfig config;
  config.cemetery_type = core::UnitTypeId{0};
  config.m2_per_grave = 15.0F;
  return config;
}

core::UnitId Place(core::WorldState& world,
                   std::uint16_t type,
                   float x,
                   std::uint8_t level,
                   std::uint16_t graves) {
  core::UnitRow unit;
  unit.type = core::UnitTypeId{type};
  unit.position = core::Vec2{.x = x, .y = 0.0F};
  unit.level = level;
  unit.graves = graves;
  return core::AppendRow(world.units, unit);
}

std::uint16_t GravesOf(const core::WorldState& world, core::UnitId unit) {
  return world.units.rows[core::FindRow(world.units, unit)].graves;
}

int Count(const core::WorldState& world, core::EventKind kind) {
  return static_cast<int>(std::ranges::count_if(
      world.step_events, [kind](const core::SimEvent& event) { return event.kind == kind; }));
}

int CheckTheRules() {
  int failures = 0;
  failures += Expect(core::CemeteryCapacityGraves(25.0F, 15.0F) == kFull,
                     "cemetery: a 25 m plot at 15 m2 a grave holds 130 graves");
  failures += Expect(core::CemeteryCapacityGraves(25.0F, 0.0F) == 0 &&
                         core::CemeteryCapacityGraves(0.0F, 15.0F) == 0,
                     "cemetery: no ground or no grave size holds nobody");
  failures += Expect(core::CemeteryFill(65, kFull) == 0.5F,
                     "cemetery: the old one's 65 of 130 reads half full");
  failures += Expect(core::CemeteryFill(0, 0) == 1.0F,
                     "cemetery: a capacity of nought reads full, not empty");
  return failures;
}

// THE OLDEST WITH ROOM FIRST (boss [33], (α)): the old one fills to its end
// though the new one stands nearer to every house; the new one takes the dead
// only from then on. Until 0.37.317 the nearest took them, and in the run the
// old one stopped at 37 of 41 the day the new one stood.
int CheckTheOldestWithRoom() {
  int failures = 0;
  const core::BurialConfig config = Config();
  core::WorldState world;
  const core::UnitId old_one = Place(world, 0, 1000.0F, 1, kFull - 1);  // marked first
  const core::UnitId new_one = Place(world, 0, 0.0F, 1, 0);             // by the houses
  const core::UnitId house = Place(world, 1, 10.0F, 1, 0);
  failures += Expect(core::GraveCapacityOf(config, kRadii, world.units.rows[2]) == 0,
                     "burial: a house holds no graves");
  core::LayGraveOf(config, kRadii, world, core::ResidentId{1});
  failures += Expect(GravesOf(world, old_one) == kFull && GravesOf(world, new_one) == 0,
                     "burial: the dead is laid in the oldest cemetery with room, not the nearest");
  core::LayGraveOf(config, kRadii, world, core::ResidentId{2});
  failures += Expect(GravesOf(world, old_one) == kFull && GravesOf(world, new_one) == 1,
                     "burial: the old one full, the next dead goes to the new one");
  failures += Expect(GravesOf(world, house) == 0 && world.burials_waiting == 0 &&
                         Count(world, core::EventKind::kBurialHeld) == 0,
                     "burial: with room somewhere, nobody waits and the game does not stop");
  // THE ID, NOT THE ROW: a removal moves rows (state_table_ops.h), so the old
  // one may come after the new one by row and must still be first.
  core::WorldState swapped;
  const core::UnitId first = Place(swapped, 0, 0.0F, 1, 0);
  const core::UnitId second = Place(swapped, 0, 500.0F, 1, 0);
  std::swap(swapped.units.rows[0], swapped.units.rows[1]);
  std::swap(swapped.units.row_ids[0], swapped.units.row_ids[1]);
  swapped.units.row_by_id[first.value] = 1;  // the lookup follows the rows
  swapped.units.row_by_id[second.value] = 0;
  core::LayGraveOf(config, kRadii, swapped, core::ResidentId{3});
  failures += Expect(GravesOf(swapped, first) == 1 && GravesOf(swapped, second) == 0,
                     "burial: the oldest is found by its id when the rows have moved");
  return failures;
}

int CheckTheStop() {
  int failures = 0;
  const core::BurialConfig config = Config();
  core::WorldState world;
  const core::UnitId full = Place(world, 0, 0.0F, 1, kFull);
  const core::UnitId marked = Place(world, 0, 500.0F, 0, 0);  // pegs and string
  core::LayGraveOf(config, kRadii, world, core::ResidentId{7});
  failures += Expect(world.burials_waiting == 1 && core::BurialHeld(world),
                     "burial: full everywhere, the grave waits and the game stops");
  failures += Expect(GravesOf(world, full) == kFull && GravesOf(world, marked) == 0,
                     "burial: no grave goes past a full cemetery or into a marked plot");
  const auto held = std::ranges::find_if(world.step_events, [](const core::SimEvent& event) {
    return event.kind == core::EventKind::kBurialHeld;
  });
  failures += Expect(held != world.step_events.end() && held->resident.value == 7 &&
                         held->amount == 1 && held->severity == core::EventSeverity::kInterrupting,
                     "burial: kBurialHeld names the dead and the waiting, and interrupts");
  core::LayGraveOf(config, kRadii, world, core::ResidentId{8});
  core::LayWaitingGraves(config, kRadii, world);
  failures += Expect(world.burials_waiting == 2 && Count(world, core::EventKind::kBurialsLaid) == 0,
                     "burial: a marked plot takes no waiting grave");
  world.units.rows[core::FindRow(world.units, marked)].level = 1;  // the new one stands
  core::LayWaitingGraves(config, kRadii, world);
  const auto laid = std::ranges::find_if(world.step_events, [](const core::SimEvent& event) {
    return event.kind == core::EventKind::kBurialsLaid;
  });
  failures +=
      Expect(world.burials_waiting == 0 && !core::BurialHeld(world) && GravesOf(world, marked) == 2,
             "burial: the new cemetery takes both waiting graves and the game runs");
  failures += Expect(
      laid != world.step_events.end() && laid->unit.value == marked.value && laid->amount == 2,
      "burial: kBurialsLaid names the cemetery and the graves laid");
  return failures;
}

/// THE OLD CEMETERY's OWN 14 m (boss [23], [25]): what it holds and what it
/// blocks, both through PlotRadiusOf; a new one keeps the type's 25 m.
int CheckTheOwnPlot() {
  int failures = 0;
  const core::BurialConfig config = Config();
  core::WorldState world;
  const core::UnitId old_one = Place(world, 0, 0.0F, 1, 0);
  world.units.rows[core::FindRow(world.units, old_one)].plot_radius_m = 14.0F;
  const core::UnitId new_one = Place(world, 0, 1000.0F, 1, 0);
  const core::UnitRow& old_row = world.units.rows[core::FindRow(world.units, old_one)];
  const core::UnitRow& new_row = world.units.rows[core::FindRow(world.units, new_one)];
  failures += Expect(core::GraveCapacityOf(config, kRadii, old_row) == 41 &&
                         core::GraveCapacityOf(config, kRadii, new_row) == kFull,
                     "own plot: the old one's 14 m holds 41 graves, the new one's 25 m 130");
  failures += Expect(
      core::PlotRadiusOf(old_row, kRadii) == 14.0F && core::PlotRadiusOf(new_row, kRadii) == 25.0F,
      "own plot: a unit's own radius answers before its type's");
  // 60 m centre to centre (boss's suggest_cemetery_new_01): 14 + 25 = 39 does
  // not overlap, the type's 25 + 25 = 50 would not either; 45 m tells them apart.
  const core::PlotRules rules{.radius_by_type = kRadii, .map_side_m = 0.0F, .yields_by_type = {}};
  failures +=
      Expect(!core::PlotOverlaps(
                 world.units, rules, core::Vec2{.x = 45.0F, .y = 0.0F}, 25.0F, core::UnitId{}),
             "own plot: a 25 m plot 45 m from the old one's 14 m does not overlap");
  world.units.rows[core::FindRow(world.units, old_one)].plot_radius_m = 0.0F;
  failures +=
      Expect(core::PlotOverlaps(
                 world.units, rules, core::Vec2{.x = 45.0F, .y = 0.0F}, 25.0F, core::UnitId{}),
             "own plot: with the type's 25 m again, the same plot overlaps");
  return failures;
}

}  // namespace

int CheckBurial() {
  return CheckTheRules() + CheckTheOldestWithRoom() + CheckTheStop() + CheckTheOwnPlot();
}
