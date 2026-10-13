// The carriers on foot in the groom's plan (#671, the second delivery; logistics_plan.cpp,
// leg_loads.h). Called from main.cpp (TestThePortersPlan).
//
// THE KNOWN ANSWERS, by hand from transport design §2: a walker's day on a heap with a receiver
// is CUT INTO TRIPS - loaded to the receiver, empty back - where until 0.37.240 he stood at the
// load all day; his hay trip is a 10-kg bundle; three hands on a manure heap are a pair on a
// stretcher, each naming the other and each with 20 kg of the pair's 40, and an odd hand alone
// with his 20 kg as bulk; the porters' door reports the walker on his loaded leg with a whole
// trip's share.
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../../common/fake_tables.h"
#include "core_common/cargo_form.h"
#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/porter_leg.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "leg_loads.h"
#include "logistics_config.h"
#include "logistics_plan.h"
#include "logistics_tasks.h"

namespace {

int ExpectPorters(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: porters' plan: " << label << '\n';
  return 1;
}

constexpr core::Grams kKg = 1000;

}  // namespace

int TestThePortersPlan() {
  int failures = 0;
  const test::FakeTable resources{{"key", "kg_per_unit", "cargo_form", "is_fuel"},
                                  {{"hay", "1000", "rick", "0"},
                                   {"manure", "1000", "manure", "0"},
                                   {"log", "200", "long", "0"}}};
  const test::FakeTable labor{
      {"key", "value"},
      {{"carry_kg_adult", "20"}, {"carry_kg_rick", "10"}, {"carry_kg_stretcher_pair", "40"}}};
  const test::FakeTableSet tables{{{"resources", &resources}, {"labor", &labor}}};
  core::LogisticsConfig config;
  std::string error;
  if (ExpectPorters(core::ParseLogisticsConfig(tables, config, error),
                    "the config reads the tables") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  failures += ExpectPorters(config.porter.carry.rick == 10 * kKg &&
                                config.porter.carry.stretcher_pair == 40 * kKg &&
                                config.porter.cargo_form == config.cargo_form,
                            "the porter's rules are read with the config, and their forms are "
                            "the logistics' own column");
  core::WorldState world;
  world.weather.daylight_hours = 12.0F;
  world.calendar.day = 3;
  core::UnitRow house;
  const core::UnitId house_id = core::AppendRow(world.units, house);
  core::FamilyRow household;
  household.house = house_id;
  const core::FamilyId family = core::AppendRow(world.families, household);
  world.units.rows[0].household = family;
  const auto heap = [&world](core::Vec2 at, core::Vec2 receiver, std::uint16_t resource) {
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.center = at;
    field.reaped_grams = 5'000'000;
    field.reaped_resource = core::ResourceId{resource};
    field.haul_days_remaining = 50.0F;
    field.haul_to = receiver;
    return core::AppendRow(world.fields, field);
  };
  const core::FieldId hay = heap({.x = 300.0F, .y = 0.0F}, {.x = 100.0F, .y = 0.0F}, 0);
  const core::FieldId manure = heap({.x = 0.0F, .y = 300.0F}, {.x = 0.0F, .y = 100.0F}, 1);
  const auto walker = [&world, family](core::FieldId field) {
    core::ResidentRow row;
    row.family = family;
    row.work.kind = core::WorkKind::kHauling;
    row.work.field = field;
    return core::AppendRow(world.residents, row);
  };
  const core::ResidentId hay_hand = walker(hay);
  const core::ResidentId first = walker(manure);
  const core::ResidentId second = walker(manure);
  const core::ResidentId odd = walker(manure);
  core::TaskDayCount count;
  core::SyncTasks(config, world, world.calendar.day, count);
  core::LogisticsTally tally;
  world.groom_plan = core::BuildGroomPlan(config, world, tally);
  const auto cart_of = [&world](core::ResidentId driver) -> const core::CartPlan* {
    for (const core::CartPlan& cart : world.groom_plan.carts) {
      if (cart.driver.value == driver.value) {
        return &cart;
      }
    }
    return nullptr;
  };
  const core::CartPlan* const hay_plan = cart_of(hay_hand);
  const core::CartPlan* const first_plan = cart_of(first);
  const core::CartPlan* const second_plan = cart_of(second);
  const core::CartPlan* const odd_plan = cart_of(odd);
  if (ExpectPorters(hay_plan != nullptr && first_plan != nullptr && second_plan != nullptr &&
                        odd_plan != nullptr,
                    "every carrier on foot is in the plan") != 0) {
    return failures + 1;
  }
  // THE TRIPS: loaded to the receiver, empty back, again.
  std::uint32_t loaded = 0;
  std::uint32_t empty_back = 0;
  bool alternate = true;
  bool to_the_receiver = true;
  bool bundles = true;
  const core::CartLeg* first_loaded = nullptr;
  int last = -1;
  for (const core::CartLeg& leg : hay_plan->legs) {
    if (leg.task.value == core::kInvalidEntityIdValue) {
      continue;
    }
    alternate = alternate && static_cast<int>(leg.loaded) != last;
    last = leg.loaded;
    if (leg.loaded != 0) {
      ++loaded;
      first_loaded = first_loaded == nullptr ? &leg : first_loaded;
      to_the_receiver = to_the_receiver && leg.from.x == 300.0F && leg.to.x == 100.0F;
      bundles = bundles && leg.resource.value == 0 && leg.carried == 10 * kKg;
    } else {
      ++empty_back;
    }
  }
  std::cout << "  porters' plan: the hay hand's day - " << loaded << " loaded legs, " << empty_back
            << " empty back\n";
  failures += ExpectPorters(hay_plan->on_foot && loaded >= 2 && empty_back >= 1 && alternate,
                            "a walker's day on a heap with a receiver is cut into trips - "
                            "loaded and empty by turns");
  // With the count beside each: «every loaded leg» of none is true of nothing.
  failures += ExpectPorters(loaded > 0 && to_the_receiver,
                            "each loaded leg goes from the heap to its receiver");
  failures += ExpectPorters(loaded > 0 && bundles, "and carries hay as a 10-kg bundle");
  // THE STRETCHER: two name each other, the odd hand is alone.
  failures += ExpectPorters(
      first_plan->partner.value == second.value && second_plan->partner.value == first.value,
      "two hands on the manure heap are a pair, each naming the other");
  failures += ExpectPorters(odd_plan->partner.value == core::kInvalidEntityIdValue &&
                                hay_plan->partner.value == core::kInvalidEntityIdValue,
                            "the odd hand on manure is alone, and nobody pairs under hay");
  const auto first_carry = [](const core::CartPlan& cart) {
    for (const core::CartLeg& leg : cart.legs) {
      if (leg.loaded != 0) {
        return leg.carried;
      }
    }
    return core::Grams{-1};
  };
  failures +=
      ExpectPorters(first_carry(*first_plan) == 20 * kKg && first_carry(*second_plan) == 20 * kKg,
                    "a stretcher's head holds 20 kg of the pair's 40");
  failures +=
      ExpectPorters(first_carry(*odd_plan) == 20 * kKg, "the odd hand carries his 20 kg as bulk");
  // THE DOOR: the hay hand on his first loaded leg. No loaded leg at all is a
  // failure of this check too, not a reason to skip it.
  const core::PorterLeg* mine = nullptr;
  std::vector<core::PorterLeg> porters;
  if (first_loaded != nullptr) {
    world.calendar.tick = first_loaded->depart;
    core::CollectPorterLegs(config, world, porters);
    for (const core::PorterLeg& porter : porters) {
      mine = porter.porter.value == hay_hand.value ? &porter : mine;
    }
  }
  failures +=
      ExpectPorters(mine != nullptr && mine->loaded == 1 && mine->form == core::CargoForm::kRick &&
                        mine->grams == 10 * kKg && mine->load_share == 1.0F,
                    "the porters' door reports him loaded with a whole bundle");
  return failures;
}
