// The checks of the carts' passengers (cart_passengers_checks.h).

#include "cart_passengers_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>

#include "cart_passengers.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "labor_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// A family in a house at `home`; its one resident at `work` on a field.
core::ResidentId AddWorker(core::WorldState& world,
                           core::Vec2 home,
                           core::Vec2 work,
                           core::WorkKind kind) {
  core::UnitRow house;
  house.level = 1;
  house.position = home;
  const core::UnitId house_id = core::AppendRow(world.units, house);
  core::FamilyRow family;
  family.house = house_id;
  const core::FamilyId family_id = core::AppendRow(world.families, family);
  world.units.rows.back().household = family_id;
  core::FieldRow field;
  field.center = work;
  field.area_ga = 5.0F;
  field.reaped_grams = kind == core::WorkKind::kHauling ? 5'000'000 : 0;
  const core::FieldId field_id = core::AppendRow(world.fields, field);
  core::ResidentRow resident;
  resident.family = family_id;
  resident.work.kind = kind;
  resident.work.field = field_id;
  resident.work.rides_horse = kind == core::WorkKind::kHauling ? 1U : 0U;
  return core::AppendRow(world.residents, resident);
}

/// A world with the team stabled at a yard at (0, 0) — or not — and a carter
/// whose house is at the yard, his load 2 km east.
core::WorldState CartWorld(bool stabled) {
  core::WorldState world;
  core::UnitRow yard;
  yard.level = 1;
  yard.position = core::Vec2{.x = 0.0F, .y = 0.0F};
  core::HerdRow horses;
  horses.kind = core::LivestockKindId{0};
  horses.adult_count = 2;
  if (stabled) {
    horses.unit = core::AppendRow(world.units, yard);
    world.chairman.horses_stabled = 1;
  }
  core::AppendRow(world.herds, horses);
  AddWorker(world,
            core::Vec2{.x = 0.0F, .y = 0.0F},
            core::Vec2{.x = 2000.0F, .y = 0.0F},
            core::WorkKind::kHauling);
  return world;
}

const core::WorkAssignment& WorkOf(const core::WorldState& world, core::ResidentId id) {
  return world.residents.rows[core::FindRow(world.residents, id)].work;
}

}  // namespace

int CheckTheCartsPassengers() {
  int failures = 0;
  core::LaborConfig config;
  config.horse_kind = core::LivestockKindId{0};
  config.cart_passenger_seats = 2;
  // THREE WALK EAST ALONG THE CART'S WAY, ONE WEST. The bench seats two: the
  // two who walk farthest ride; the west one's work lies behind the cart.
  core::WorldState world = CartWorld(true);
  const core::ResidentId driver = world.residents.row_ids[0];
  const core::ResidentId far = AddWorker(world,
                                         core::Vec2{.x = 0.0F, .y = 50.0F},
                                         core::Vec2{.x = 1900.0F, .y = 100.0F},
                                         core::WorkKind::kHarvest);
  const core::ResidentId middle = AddWorker(world,
                                            core::Vec2{.x = 0.0F, .y = 60.0F},
                                            core::Vec2{.x = 1500.0F, .y = 80.0F},
                                            core::WorkKind::kHarvest);
  const core::ResidentId near = AddWorker(world,
                                          core::Vec2{.x = 0.0F, .y = 70.0F},
                                          core::Vec2{.x = 900.0F, .y = 80.0F},
                                          core::WorkKind::kHarvest);
  const core::ResidentId west = AddWorker(world,
                                          core::Vec2{.x = 0.0F, .y = 40.0F},
                                          core::Vec2{.x = -1500.0F, .y = 0.0F},
                                          core::WorkKind::kHarvest);
  const core::PassengerTally tally = core::SeatCartPassengers(config, world);
  std::cout << "  carts' passengers: " << tally.carts << " cart(s), " << tally.seated << " seated, "
            << tally.no_seat << " without a seat; the far man's road "
            << WorkOf(world, far).travel_hours << " h, the worst wait " << tally.worst_wait_hours
            << " h\n";
  failures += Expect(tally.carts == 1 && tally.seats == 2,
                     "carts' passengers: one goods cart out from the yard, two seats on its bench");
  failures += Expect(WorkOf(world, far).rides_cart_of.value == driver.value &&
                         WorkOf(world, middle).rides_cart_of.value == driver.value,
                     "carts' passengers: the two who walk farthest along the cart's way ride it");
  // 1.9 km on foot is 4.6 game hours; by the bench — a short walk to the
  // yard, 1.9 h of ride, 0.1 km to the field — about 2.3.
  failures +=
      Expect(WorkOf(world, far).travel_hours > 2.0F && WorkOf(world, far).travel_hours < 2.6F,
             "carts' passengers: the far man's road is the walk to the cart, the ride and "
             "the walk to his field - half his walk");
  failures += Expect(
      WorkOf(world, near).rides_cart_of.value == core::kInvalidEntityIdValue && tally.no_seat == 1,
      "carts' passengers: the third, who walks the least, finds the bench full "
      "and is counted without a seat");
  failures += Expect(WorkOf(world, west).rides_cart_of.value == core::kInvalidEntityIdValue,
                     "carts' passengers: a man whose work lies behind the cart walks");
  // THE PAIR: no yard standing, or no seats in the table — nobody rides.
  core::WorldState unstabled = CartWorld(false);
  const core::ResidentId alone = AddWorker(unstabled,
                                           core::Vec2{.x = 0.0F, .y = 50.0F},
                                           core::Vec2{.x = 1900.0F, .y = 100.0F},
                                           core::WorkKind::kHarvest);
  const core::PassengerTally none = core::SeatCartPassengers(config, unstabled);
  failures += Expect(none.carts == 0 && WorkOf(unstabled, alone).rides_cart_of.value ==
                                            core::kInvalidEntityIdValue,
                     "carts' passengers: with the team at the households no cart sets out from a "
                     "yard, and nobody rides");
  core::LaborConfig seatless = config;
  seatless.cart_passenger_seats = 0;
  core::WorldState unseated = CartWorld(true);
  const core::ResidentId walker = AddWorker(unseated,
                                            core::Vec2{.x = 0.0F, .y = 50.0F},
                                            core::Vec2{.x = 1900.0F, .y = 100.0F},
                                            core::WorkKind::kHarvest);
  core::SeatCartPassengers(seatless, unseated);
  failures += Expect(WorkOf(unseated, walker).rides_cart_of.value == core::kInvalidEntityIdValue,
                     "carts' passengers: a table with no seats seats nobody");
  return failures;
}
