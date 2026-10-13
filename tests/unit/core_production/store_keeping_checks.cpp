// The checks of a store's own keeping (store_keeping_checks.h).
//
// THE KNOWN ANSWERS, by hand from resources design §4 and boss's export #769: the same ten tonnes
// of potatoes rot in a day under a vegetable store a third of what they rot in the church or a
// clamp, under a food store a half; a vegetable store still being built (level 0) keeps at one;
// a type the table gives no factor keeps at one.
#include "store_keeping_checks.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "field_haul.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: store keeping: " << label << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int CheckTheStoresKeepByTheirRoof() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ITable* const types = tables->FindTable("unit_types");
  const core::ITable* const resources = tables->FindTable("resources");
  const auto type = [types](const char* key) {
    return core::UnitTypeId{static_cast<std::uint16_t>(types->FindRowByKey(key))};
  };
  const core::UnitTypeId vegetable_store = type("vegetable_store");
  const core::UnitTypeId food_store = type("food_store");
  const core::UnitTypeId church = type("church_store");
  const core::UnitTypeId clamp = type("clamp");
  const core::UnitTypeId granary = type("granary");
  const auto factor = [&config](core::UnitTypeId unit, std::uint8_t level) {
    return config.unit_types[unit.value].KeepingFactorAt(level);
  };
  failures += Expect(factor(vegetable_store, 1) == 3.0F && factor(vegetable_store, 2) == 3.0F,
                     "the vegetable store keeps three times as long on both its levels");
  failures += Expect(factor(food_store, 1) == 2.0F, "the food store keeps twice as long");
  failures += Expect(factor(church, 1) == 1.0F && factor(clamp, 1) == 1.0F,
                     "the church store and the clamp keep at one");
  failures += Expect(factor(granary, 1) == 1.0F && factor(vegetable_store, 0) == 1.0F &&
                         factor(vegetable_store, 9) == 1.0F,
                     "a blank cell, a site and a level past the ladder keep at one");

  // THE DAY'S ROT: ten tonnes of potatoes under each roof, one day.
  const core::ResourceId potato{static_cast<std::uint16_t>(resources->FindRowByKey("potato"))};
  constexpr core::Grams kTen = 10'000'000;
  const auto rot_under = [&config, potato](core::UnitTypeId unit_type, std::uint8_t level) {
    core::WorldState world;
    core::UnitRow store;
    store.type = unit_type;
    store.level = level;
    store.stock.assign(static_cast<std::size_t>(potato.value) + 1U, 0);
    store.stock[potato.value] = kTen;
    core::AppendRow(world.units, store);
    core::SpoilStores(config, world);
    return kTen - world.units.rows[0].stock[potato.value];
  };
  const core::Grams in_church = rot_under(church, 1);
  const core::Grams in_clamp = rot_under(clamp, 1);
  const core::Grams in_vegetable_store = rot_under(vegetable_store, 1);
  const core::Grams in_food_store = rot_under(food_store, 1);
  const core::Grams on_a_site = rot_under(vegetable_store, 0);
  std::cout << "  store keeping, ten tonnes of potatoes, one day's rot, kg: church "
            << in_church / 1000 << ", clamp " << in_clamp / 1000 << ", vegetable store "
            << in_vegetable_store / 1000 << ", food store " << in_food_store / 1000
            << ", a vegetable store's site " << on_a_site / 1000 << '\n';
  failures += Expect(in_church > 0, "potatoes rot in the church store at all");
  failures += Expect(in_clamp == in_church, "the clamp rots them as the church does");
  failures +=
      Expect(in_vegetable_store * 3 > in_church - 3000 && in_vegetable_store * 3 < in_church + 3000,
             "the vegetable store rots a third of the church's day");
  failures += Expect(in_food_store * 2 > in_church - 2000 && in_food_store * 2 < in_church + 2000,
                     "the food store rots a half of it");
  failures += Expect(on_a_site == in_church,
                     "a vegetable store not yet built keeps nothing better than the open church");
  return failures;
}
