// The checks of the settlement step (settlement_checks.h).
//
// THE WORLD, by hand: unit type 0 is a house (class housing), type 1 the fire gong, type 2 a barn;
// no plot radii, so a gong stands where it is put. The start village: three houses around (1000,
// 1000) and its gong. Distances are along the x axis, so they read off the positions.
#include "settlement_checks.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/plot.h"
#include "core_common/settlement_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "settlement_rules.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: settlements: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr std::uint16_t kHouse = 0;
constexpr std::uint16_t kGong = 1;
constexpr std::uint16_t kBarn = 2;

core::SettlementConfig Config() {
  core::SettlementConfig config;
  config.housing = {1, 0, 0};
  config.fire_gong = core::UnitTypeId{kGong};
  config.radius_by_type = {0.0F, 0.0F, 0.0F};
  return config;
}

core::UnitId Add(core::WorldState& world, std::uint16_t type, float x, float y) {
  core::UnitRow unit;
  unit.type = core::UnitTypeId{type};
  unit.level = 1;
  unit.position = core::Vec2{.x = x, .y = y};
  return core::AppendRow(world.units, unit);
}

/// A house with a family living in it.
core::UnitId AddHome(core::WorldState& world, float x, float y) {
  const core::UnitId house = Add(world, kHouse, x, y);
  core::FamilyRow family;
  family.house = house;
  core::AppendRow(world.families, family);
  return house;
}

core::SettlementId SettlementOf(const core::WorldState& world, core::UnitId unit) {
  return world.units.rows[core::FindRow(world.units, unit)].settlement;
}

/// Runs the step at the last tick of `day`.
void Day(const core::SettlementConfig& config, core::WorldState& world, core::SimDay day) {
  world.calendar.tick =
      (static_cast<core::Tick>(day) * core::kTicksPerDay) + core::kTicksPerDay - 1U;
  world.calendar.day = day;
  core::RunSettlementDay(config, world);
}

int Count(const core::WorldState& world, core::EventKind kind) {
  int n = 0;
  for (const core::SimEvent& event : world.step_events) {
    n += event.kind == kind ? 1 : 0;
  }
  return n;
}

core::WorldState StartVillage(const core::SettlementConfig& config) {
  core::WorldState world;
  AddHome(world, 1000.0F, 1000.0F);
  AddHome(world, 1100.0F, 1000.0F);
  AddHome(world, 1200.0F, 1000.0F);
  Add(world, kGong, 1100.0F, 1050.0F);
  core::SeedStartSettlement(config, world);
  return world;
}

}  // namespace

int CheckTheSettlements() {
  int failures = 0;
  const core::SettlementConfig config = Config();

  // THE SEED: one settlement, id 1, its gong the layout's own, every unit its member.
  core::WorldState world = StartVillage(config);
  failures +=
      Expect(world.settlements.rows.size() == 1 && world.settlements.row_ids[0].value == 1 &&
                 world.settlements.rows[0].gong == world.units.row_ids[3] &&
                 SettlementOf(world, world.units.row_ids[0]).value == 1,
             "the start village is id 1, its gong the layout's, the units its members");

  // JOIN, DAILY: a house 500 m from the village joins it; one 750 m away joins
  // as an outlying yard; one 2 km away stays unassigned.
  const core::UnitId near = AddHome(world, 1700.0F, 1000.0F);
  const core::UnitId yard = AddHome(world, 2450.0F, 1000.0F);
  const core::UnitId far = AddHome(world, 4500.0F, 1000.0F);
  Day(config, world, 1);
  const auto unit = [&world](core::UnitId id) -> const core::UnitRow& {
    return world.units.rows[core::FindRow(world.units, id)];
  };
  failures += Expect(unit(near).settlement.value == 1 && unit(near).outlying == 0,
                     "a house within 600 m joins the village the same day");
  failures += Expect(unit(yard).settlement.value == 1 && unit(yard).outlying == 1,
                     "a house within 800 m joins as an outlying yard");
  failures += Expect(unit(far).settlement.value == core::kInvalidEntityIdValue,
                     "a house 2 km away belongs to no settlement yet");

  // V4: two far houses never form a settlement, however many turns go by.
  AddHome(world, 4800.0F, 1000.0F);
  for (core::SimDay day = 2; day <= 15; ++day) {
    Day(config, world, day);
  }
  failures += Expect(world.settlements.rows.size() == 1,
                     "two far houses make no settlement and no gong (V4)");

  // FORM: a third far house; the next month's turn marks the chain, the one
  // after forms it - a new id, a gong at its centre, the event said.
  AddHome(world, 5100.0F, 1000.0F);
  Day(config, world, 19);  // a month's turn (day % 4 == 3)
  const bool not_yet = world.settlements.rows.size() == 1;
  world.step_events.clear();
  Day(config, world, 23);
  const std::uint32_t formed_row = world.settlements.rows.size() == 2 ? 1U : 0U;
  failures += Expect(not_yet && world.settlements.rows.size() == 2 &&
                         world.settlements.row_ids[formed_row].value == 2 &&
                         Count(world, core::EventKind::kSettlementFormed) == 1,
                     "three far houses form settlement 2 at the second month's turn");
  if (world.settlements.rows.size() == 2) {
    const core::SettlementRow& formed = world.settlements.rows[formed_row];
    const std::uint32_t gong_row = core::FindRow(world.units, formed.gong);
    failures += Expect(gong_row != core::kNoRow && world.units.rows[gong_row].type.value == kGong &&
                           world.units.rows[gong_row].position.x == 4800.0F,
                       "the new settlement's gong stands at its centre");
  }

  // MERGE (V5): houses built between the two until they link within 600 m;
  // two month's turns on, one settlement - id 1, the elder - and the younger's
  // gong gone.
  for (float x = 2950.0F; x <= 4400.0F; x += 500.0F) {
    AddHome(world, x, 1000.0F);
  }
  // THE GONG (V3, V6), measured over the same turns: the merged village's
  // centre moves east, and the gong follows at the turn unless frozen.
  core::SettlementConfig frozen = config;
  frozen.gong_frozen = true;
  core::WorldState held = world;
  const core::Vec2 before =
      world.units.rows[core::FindRow(world.units, world.settlements.rows[0].gong)].position;
  world.step_events.clear();
  Day(config, world, 27);
  Day(config, world, 31);
  Day(frozen, held, 27);
  Day(frozen, held, 31);
  failures += Expect(
      world.settlements.rows.size() == 1 && world.settlements.row_ids[0].value == 1 &&
          Count(world, core::EventKind::kSettlementMerged) == 1 && unit(far).settlement.value == 1,
      "linked two turns, the two merge into id 1, the elder's (V5)");

  const core::Vec2 after_frozen =
      held.units.rows[core::FindRow(held.units, held.settlements.rows[0].gong)].position;
  const core::Vec2 after =
      world.units.rows[core::FindRow(world.units, world.settlements.rows[0].gong)].position;
  // Two turns, two moves at most: the houses joined before the first moved
  // the centre, the merge at the second moved it again.
  const int moves = Count(world, core::EventKind::kFireGongMoved);
  std::cout << "  settlements: the gong " << before.x << " -> " << after.x << " over two turns, "
            << moves << " moves\n";
  failures += Expect(after.x > before.x + 100.0F && moves >= 1 && moves <= 2,
                     "the gong moves to the grown village's centre, at most once a turn (V3)");
  failures += Expect(after_frozen.x == before.x, "a frozen gong stands where it was (V6's run)");

  // THE GONG YIELDS ITS PLACE (yields_place): a house's plot that a FreePlot
  // with the gong yielding lands on the gong is not bent round it; the same
  // day the gong steps to the nearest free spot, off every body.
  {
    core::SettlementConfig yielding = config;
    yielding.radius_by_type = {10.0F, 0.6F, 0.0F};
    core::WorldState small = StartVillage(yielding);
    const core::UnitId gong = small.settlements.rows[0].gong;
    const core::Vec2 gong_at = small.units.rows[core::FindRow(small.units, gong)].position;
    const std::vector<std::uint8_t> yields = {0, 1, 0};
    const core::PlotRules house_rules{
        .radius_by_type = yielding.radius_by_type, .map_side_m = 0.0F, .yields_by_type = yields};
    const core::Vec2 house_at = core::FreePlot(small.units, house_rules, gong_at, 10.0F);
    failures += Expect(house_at.x == gong_at.x && house_at.y == gong_at.y,
                       "a house's FreePlot does not bend round a yielding gong");
    AddHome(small, house_at.x, house_at.y);
    Day(yielding, small, 1);
    const core::Vec2 moved = small.units.rows[core::FindRow(small.units, gong)].position;
    const core::PlotRules all{
        .radius_by_type = yielding.radius_by_type, .map_side_m = 0.0F, .yields_by_type = {}};
    failures += Expect(!(moved.x == gong_at.x && moved.y == gong_at.y) &&
                           !core::PlotOverlaps(small.units, all, moved, 0.6F, gong),
                       "the gong steps off the house the same day, onto a free spot");
  }

  // A barn takes the settlement of the nearest dwelling, daily.
  const core::UnitId barn = Add(world, kBarn, 5000.0F, 1100.0F);
  Day(config, world, 36);
  failures += Expect(unit(barn).settlement.value == 1,
                     "a barn belongs to its nearest dwellings' settlement");
  return failures;
}
