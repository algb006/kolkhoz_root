// unit_core_logistics: the groom's tasks of carting (routing stage B, B2;
// core_logistics/logistics_tasks.h, logistics_config.h).

#include <cstdint>
#include <iostream>
#include <string>

#include "../../common/fake_tables.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_logistics/logistics_system.h"
#include "logistics_config.h"
#include "logistics_plan.h"
#include "logistics_tasks.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

core::FieldId AddHeap(core::WorldState& world, core::Grams grams, std::uint16_t resource) {
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.reaped_grams = grams;
  field.reaped_resource = core::ResourceId{resource};
  return core::AppendRow(world.fields, field);
}

const core::LogisticsTaskRow* TaskOfField(const core::WorldState& world, core::FieldId field) {
  for (const core::LogisticsTaskRow& task : world.logistics_tasks.rows) {
    if (task.load_kind == core::LogisticsLoadKind::kFieldHeap && task.field.value == field.value) {
      return &task;
    }
  }
  return nullptr;
}

/// ONE TASK A LOAD, MADE AS IT APPEARS AND ENDED WITH IT (boss [9], default
/// 2): a heap, a stand's logs and a dig give three tasks at their kinds'
/// levels; a second sync makes none; the heap carted away ends its task.
int TestOneTaskALoad() {
  int failures = 0;
  const core::LogisticsConfig config;
  core::WorldState world;
  const core::FieldId heap = AddHeap(world, 1'000'000, 0);
  core::TimberStandRow stand;
  stand.load_grams = 2'000'000;
  core::AppendRow(world.stands, stand);
  core::ExtractionSiteRow site;
  site.load_grams = 500'000;
  core::AppendRow(world.extraction_sites, site);
  core::TaskDayCount count;
  core::SyncTasks(config, world, 0, count);
  failures += Expect(world.logistics_tasks.rows.size() == 3 && count.made == 3,
                     "tasks: a heap, logs and a dig make three tasks");
  const core::LogisticsTaskRow* const heap_task = TaskOfField(world, heap);
  failures += Expect(heap_task != nullptr && heap_task->level == core::LogisticsLevel::kTerm,
                     "tasks: a field's heap is due by a date (level 1)");
  core::TaskDayCount again;
  core::SyncTasks(config, world, 1, again);
  failures += Expect(again.made == 0 && world.logistics_tasks.rows.size() == 3,
                     "tasks: a load with a task gets no second one");
  world.fields.rows[core::FindRow(world.fields, heap)].reaped_grams = 0;
  core::TaskDayCount carted;
  core::SyncTasks(config, world, 2, carted);
  failures += Expect(carted.ended == 1 && world.logistics_tasks.rows.size() == 2 &&
                         TaskOfField(world, heap) == nullptr,
                     "tasks: the heap carted away ends its task");
  return failures;
}

/// AGEING FROM THE LAST TRIP (econ [5]): background -> ordinary after 6
/// days, ordinary -> term after 4 more; a served task starts again from its
/// base level; a paused one does not age.
int TestAgeing() {
  int failures = 0;
  const core::LogisticsConfig config;
  core::WorldState world;
  core::UnitRow store;
  store.emptying = 1;
  const core::UnitId church = core::AppendRow(world.units, store);
  core::TaskDayCount count;
  core::SyncTasks(config, world, 0, count);
  core::LogisticsTaskRow& task = world.logistics_tasks.rows[0];
  failures += Expect(task.base_level == core::LogisticsLevel::kBackground,
                     "ageing: the church's transfer is background (level 3)");
  const auto level_on = [&](core::SimDay day) {
    core::TaskDayCount day_count;
    core::AgeAndRaise(config, world, day, static_cast<core::Tick>(day) * 24U, day_count);
    return std::pair{world.logistics_tasks.rows[0].level, day_count.aged_up};
  };
  failures += Expect(level_on(5).first == core::LogisticsLevel::kBackground,
                     "ageing: five days unserved — still background");
  const auto [on_six, raised_six] = level_on(6);
  failures += Expect(on_six == core::LogisticsLevel::kOrdinary && raised_six == 1,
                     "ageing: six days — ordinary, counted raised that day");
  failures +=
      Expect(level_on(10).first == core::LogisticsLevel::kTerm, "ageing: ten days — due by a date");
  failures += Expect(level_on(30).first == core::LogisticsLevel::kTerm,
                     "ageing: a term never ages into urgent");
  // A carter goes to the church: served, back to background.
  core::ResidentRow carter;
  carter.work.kind = core::WorkKind::kHauling;
  carter.work.unit = church;
  core::AppendRow(world.residents, carter);
  core::TaskDayCount served;
  core::MarkServed(world, 30, served);
  failures += Expect(served.served == 1 && level_on(30).first == core::LogisticsLevel::kBackground,
                     "ageing: a trip returns the task to its base level");
  // Paused from day 30: twenty days later it has not aged.
  world.residents.rows.clear();
  world.residents.row_ids.clear();
  core::RebuildLookup(world.residents);
  world.logistics_tasks.rows[0].paused = true;
  for (core::SimDay day = 31; day <= 50; ++day) {
    level_on(day);
  }
  failures += Expect(world.logistics_tasks.rows[0].level == core::LogisticsLevel::kBackground,
                     "ageing: a paused task does not age");
  return failures;
}

/// SELF TO LEVEL 0 (econ [5]): a heap that spoils fast is urgent, the same
/// heap of a keeping resource is not; a feed heap is urgent while a herd is
/// hungry, the same heap with the herd fed is not.
int TestThreats() {
  int failures = 0;
  core::LogisticsConfig config;
  config.spoil_days = {2.0F, 0.0F, 0.0F};
  config.feed = {0, 0, 1};
  core::WorldState world;
  const core::FieldId rotting = AddHeap(world, 5'000'000, 0);
  const core::FieldId keeping = AddHeap(world, 5'000'000, 1);
  const core::FieldId hay = AddHeap(world, 5'000'000, 2);
  core::UnitRow barn;
  core::HerdRow herd;
  herd.unit = core::AppendRow(world.units, barn);
  herd.adult_count = 10;
  herd.fed_share = 1.0F;
  core::AppendRow(world.herds, herd);
  core::TaskDayCount count;
  core::SyncTasks(config, world, 0, count);
  core::AgeAndRaise(config, world, 0, 5, count);
  failures += Expect(TaskOfField(world, rotting)->level == core::LogisticsLevel::kUrgent &&
                         TaskOfField(world, rotting)->urgent_since == 5,
                     "threats: a heap to rot by tomorrow is urgent, the tick recorded");
  failures += Expect(TaskOfField(world, keeping)->level == core::LogisticsLevel::kTerm,
                     "threats: the same heap of a keeping resource stays due by a date");
  failures += Expect(TaskOfField(world, hay)->level == core::LogisticsLevel::kTerm,
                     "threats: a feed heap with the herd fed stays due by a date");
  world.herds.rows[0].fed_share = 0.5F;
  core::TaskDayCount hungry;
  core::AgeAndRaise(config, world, 1, 30, hungry);
  failures += Expect(TaskOfField(world, hay)->level == core::LogisticsLevel::kUrgent &&
                         hungry.urgent_by_hunger == 1,
                     "threats: a feed heap is urgent while a herd went underfed");
  return failures;
}

/// THE TABLE: logistics.csv's levels and knobs are read; a level beyond 3
/// refuses it.
int TestConfig() {
  int failures = 0;
  const test::FakeTable good{{"key", "value"},
                             {{"level_stand_logs", "3"}, {"age_ordinary_days", "5"}}};
  const test::FakeTableSet good_tables{{{"logistics", &good}}};
  core::LogisticsConfig config;
  std::string error;
  failures += Expect(core::ParseLogisticsConfig(good_tables, config, error) &&
                         config.default_level[1] == core::LogisticsLevel::kBackground &&
                         config.age_ordinary_days == 5.0F,
                     "config: the table's level and ageing are read");
  const test::FakeTable bad{{"key", "value"}, {{"level_site_dig", "5"}}};
  const test::FakeTableSet bad_tables{{{"logistics", &bad}}};
  core::LogisticsConfig refused;
  failures += Expect(!core::ParseLogisticsConfig(bad_tables, refused, error),
                     "config: a level of 5 refuses the table");
  return failures;
}

/// THE PLAN'S CHAINS AND ITS CLOCK (B3, B4b; logistics_plan.h): two carts on
/// two of three heaps of level 1, a stand's logs of level 2, a paused heap
/// and a heap 10 km out, a carrier on foot, a people's cart to the fellers.
/// The second cart's ring is turned one, so done with its own heap it takes
/// the third before the first; a heap another cart has carted is skipped;
/// the paused heap and the one beyond the road limit are in no chain; every
/// day lies in the light, its legs in order.
int TestThePlansChains() {
  int failures = 0;
  core::LogisticsConfig config;  // 12 km/h: 1 game hour a km; the limit 6 hours
  core::WorldState world;
  const auto heap_at = [&world](float east_m) {
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.center = core::Vec2{.x = east_m, .y = 0.0F};
    field.reaped_grams = 1'000'000;
    field.reaped_resource = core::ResourceId{0};
    field.haul_days_remaining = 2.0F;
    return core::AppendRow(world.fields, field);
  };
  // The seams in cart-days (a cart-day is 10 cart-hours): the first heap a
  // whole day, the second an hour, the third half a day.
  const auto heap_days = [&world, &heap_at](float east_m, float days) {
    const core::FieldId id = heap_at(east_m);
    world.fields.rows[core::FindRow(world.fields, id)].haul_days_remaining = days;
    return id;
  };
  const core::FieldId first = heap_days(100.0F, 1.0F);
  const core::FieldId second = heap_days(200.0F, 0.1F);
  const core::FieldId third = heap_days(250.0F, 0.5F);
  const core::FieldId paused = heap_at(300.0F);
  const core::FieldId far = heap_at(10'000.0F);
  core::TimberStandRow stand;
  stand.position = core::Vec2{.x = 400.0F, .y = 0.0F};
  stand.load_grams = 1'000'000;
  stand.haul_days_remaining = 0.1F;
  core::AppendRow(world.stands, stand);
  core::TimberStandRow felled;  // the people's cart's work: fellers 2 km out
  felled.position = core::Vec2{.x = 0.0F, .y = 2000.0F};
  const core::TimberStandId wood = core::AppendRow(world.stands, felled);
  core::TaskDayCount count;
  core::SyncTasks(config, world, 0, count);
  for (core::LogisticsTaskRow& task : world.logistics_tasks.rows) {
    task.paused = task.field.value == paused.value;
  }
  // One house at the origin; twelve hours of light, 6 to 18; three horses,
  // not stabled — no horse yard, so every day starts at the house, and a
  // carrier on foot drains a cart-seam at his share of a cart-day (haul.h).
  world.weather.daylight_hours = 12.0F;
  config.horse_kind = core::LivestockKindId{0};
  core::HerdRow horses;
  horses.kind = core::LivestockKindId{0};
  horses.adult_count = 3;
  core::AppendRow(world.herds, horses);
  core::UnitRow house;
  house.position = core::Vec2{.x = 0.0F, .y = 0.0F};
  const core::UnitId house_id = core::AppendRow(world.units, house);
  core::FamilyRow household;
  household.house = house_id;
  const core::FamilyId family = core::AppendRow(world.families, household);
  world.units.rows[0].household = family;
  const auto person = [&world, family](core::WorkAssignment work) {
    core::ResidentRow row;
    row.family = family;
    row.work = work;
    return core::AppendRow(world.residents, row);
  };
  const auto carter = [&person](core::FieldId field, std::uint8_t horse) {
    core::WorkAssignment work;
    work.kind = core::WorkKind::kHauling;
    work.field = field;
    work.rides_horse = horse;
    return person(work);
  };
  const core::ResidentId cart_one = carter(first, 1);
  const core::ResidentId cart_two = carter(second, 1);
  const core::ResidentId walker = carter(first, 0);
  core::WorkAssignment felling;
  felling.kind = core::WorkKind::kFelling;
  felling.stand = wood;
  felling.rides_horse = 1;
  const core::ResidentId people_driver = person(felling);
  felling.rides_horse = 0;
  felling.rides_cart_of = people_driver;
  const core::ResidentId feller = person(felling);
  core::WorkAssignment reaping;  // a passenger of the first goods cart
  reaping.kind = core::WorkKind::kHarvest;
  reaping.field = far;
  reaping.rides_cart_of = cart_one;
  const core::ResidentId passenger = person(reaping);

  core::LogisticsTally tally;
  const core::GroomPlan plan = core::BuildGroomPlan(config, world, tally);
  const auto cart_of = [&plan](core::ResidentId driver) -> const core::CartPlan* {
    for (const core::CartPlan& cart : plan.carts) {
      if (cart.driver.value == driver.value) {
        return &cart;
      }
    }
    return nullptr;
  };
  // The loads of a mover's legs, in order: a field's id, -1 the logs. With
  // `carted_only`, those the clock sent it to — a load leg after an empty leg
  // that rode there; without, the whole chain, the loads kept in their place
  // as legs of no length too (carted by others first, or past the evening).
  const auto loads_of = [&world](const core::CartPlan& cart, bool carted_only) {
    std::vector<int> loads;
    for (std::size_t index = 0; index < cart.legs.size(); ++index) {
      const std::uint32_t row = core::FindRow(world.logistics_tasks, cart.legs[index].task);
      if (row == core::kNoRow) {
        continue;  // an empty leg
      }
      const bool rode_there =
          index > 0 && cart.legs[index - 1].task.value == core::kInvalidEntityIdValue;
      if (carted_only && !rode_there) {
        continue;
      }
      const core::LogisticsTaskRow& task = world.logistics_tasks.rows[row];
      loads.push_back(task.load_kind == core::LogisticsLoadKind::kStandLogs
                          ? -1
                          : static_cast<int>(task.field.value));
    }
    return loads;
  };
  const core::CartPlan* const one = cart_of(cart_one);
  const core::CartPlan* const two = cart_of(cart_two);
  const core::CartPlan* const foot = cart_of(walker);
  const core::CartPlan* const people = cart_of(people_driver);
  failures += Expect(plan.carts.size() == 4 && tally.carts == 3 && one != nullptr &&
                         two != nullptr && foot != nullptr && people != nullptr,
                     "plan: two goods carts, the people's cart and the carrier on foot");
  if (one == nullptr || two == nullptr || foot == nullptr || people == nullptr) {
    return failures;
  }
  const std::vector<int> one_loads = loads_of(*one, true);
  const std::vector<int> two_loads = loads_of(*two, true);
  const std::vector<int> foot_loads = loads_of(*foot, true);
  const auto print =
      [](const char* name, const core::CartPlan& cart, const std::vector<int>& loads) {
        std::cout << "  plan: " << name << " - loads";
        for (const int load : loads) {
          std::cout << ' ' << load;
        }
        std::cout << "; legs";
        for (const core::CartLeg& leg : cart.legs) {
          std::cout << ' ' << leg.depart << '-' << leg.arrive;
        }
        std::cout << '\n';
      };
  print("cart 1", *one, one_loads);
  print("cart 2", *two, two_loads);
  print("on foot", *foot, foot_loads);
  print("people's cart", *people, {});
  const auto id = [](core::FieldId field) { return static_cast<int>(field.value); };
  // Cart 2 carts its hour's heap by 7; the ring turned one sends it to the
  // third heap, not to the first that cart 1 is still on; at noon it comes to
  // help cart 1, and both end on the logs.
  failures += Expect(two_loads == std::vector<int>{id(second), id(third), id(first), -1},
                     "plan: the second cart — its own heap, then the ring turned one (the third "
                     "heap before the first), the first heap with the other cart, the logs");
  failures += Expect(one_loads == std::vector<int>{id(first), -1},
                     "plan: the first cart — its day's heap with help at noon, then the logs: "
                     "the heaps carted by others are not ridden to");
  failures +=
      Expect(loads_of(*one, false) == std::vector<int>{id(first), id(second), id(third), -1},
             "plan: and they stay in its chain in their place, as legs of no length — "
             "the labour hour follows the seams, not the estimate");
  failures += Expect(foot->on_foot && !foot_loads.empty() && foot_loads.front() == id(first),
                     "plan: the carrier on foot is in the plan, on his own heap first");
  bool paused_or_far = false;
  bool in_order = true;
  for (const core::CartPlan& cart : plan.carts) {
    core::Tick last = 0;
    for (const core::CartLeg& leg : cart.legs) {
      in_order = in_order && leg.depart <= leg.arrive && leg.depart >= last;
      last = leg.arrive;
    }
    for (const int load : loads_of(cart, false)) {
      paused_or_far = paused_or_far || load == id(paused) || load == id(far);
    }
  }
  failures += Expect(!paused_or_far,
                     "plan: the paused heap and the heap past the road limit are in no chain");
  failures += Expect(in_order,
                     "plan: every leg leaves no earlier than the one before it arrived, and "
                     "arrives no earlier than it leaves");
  failures += Expect(one->legs.front().riders.size() == 1 &&
                         one->legs.front().riders[0].value == passenger.value &&
                         one->legs.front().depart >= 6 && one->legs.back().arrive <= 18,
                     "plan: the passenger rides the first cart's first leg; its day is in the "
                     "light, 6 to 18");
  failures += Expect(
      people->people_cart && people->legs.size() == 2 && people->legs[0].riders.size() == 1 &&
          people->legs[0].riders[0].value == feller.value &&
          people->legs[0].task.value == core::kInvalidEntityIdValue && people->legs[1].arrive == 18,
      "plan: the people's cart — out to the fellers with its rider, home at "
      "sunset");
  return failures;
}

/// THE RE-PLAN BY EVENT (B5; logistics_system.h, Replan): two carts on two
/// heaps a long way apart, planned at hour 1 of a twelve-hour day. At hour 5
/// the second carter has lost his horse: the plan is stale — the system plans
/// the rest of the day again, the first cart keeps every leg it had begun by
/// then exactly, no new leg leaves before hour 6, and the second carter has no
/// cart. The pair: nothing changed, no re-plan.
int TestTheReplan() {
  int failures = 0;
  const test::FakeTableSet no_tables{{}};
  const auto system = core::CreateLogisticsSystem(no_tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "re-plan: the system assembles on its defaults") != 0) {
    return 1;
  }
  const auto day_of = [&system](bool horse_lost, bool& replanned, core::WorldState& world) {
    world.weather.daylight_hours = 12.0F;
    world.calendar.day = 3;
    core::LogisticsConfig config;
    const auto heap_at = [&world](float east_m) {
      core::FieldRow field;
      field.kind = core::LandKind::kArable;
      field.center = core::Vec2{.x = east_m, .y = 0.0F};
      field.reaped_grams = 1'000'000;
      field.reaped_resource = core::ResourceId{0};
      field.haul_days_remaining = 1.0F;
      return core::AppendRow(world.fields, field);
    };
    const core::FieldId one = heap_at(500.0F);
    const core::FieldId two = heap_at(1'500.0F);
    core::TaskDayCount count;
    core::SyncTasks(config, world, 3, count);
    core::UnitRow house;
    const core::UnitId house_id = core::AppendRow(world.units, house);
    core::FamilyRow household;
    household.house = house_id;
    const core::FamilyId family = core::AppendRow(world.families, household);
    world.units.rows[0].household = family;
    for (const core::FieldId field : {one, two}) {
      core::ResidentRow carter;
      carter.family = family;
      carter.work.kind = core::WorkKind::kHauling;
      carter.work.field = field;
      carter.work.rides_horse = 1;
      core::AppendRow(world.residents, carter);
    }
    world.calendar.tick = (3 * core::kTicksPerDay) + 1;
    system->BuildPlan(world);
    if (horse_lost) {
      world.residents.rows[1].work.rides_horse = 0;
    }
    world.calendar.tick = (3 * core::kTicksPerDay) + 5;
    replanned = system->Replan(world);
  };
  bool replanned = false;
  core::WorldState lost;
  day_of(true, replanned, lost);
  core::WorldState before = lost;  // the morning's plan, rebuilt to compare the kept legs
  {
    core::WorldState morning;
    bool ignored = false;
    day_of(false, ignored, morning);
    before.groom_plan = morning.groom_plan;
  }
  bool kept = !lost.groom_plan.carts.empty() && !before.groom_plan.carts.empty();
  bool nothing_early = true;
  bool second_has_cart = false;
  const core::Tick hour_five = (3 * core::kTicksPerDay) + 5;
  for (const core::CartPlan& cart : lost.groom_plan.carts) {
    if (cart.driver.value == lost.residents.row_ids[1].value && !cart.on_foot) {
      second_has_cart = true;
    }
    if (cart.driver.value != lost.residents.row_ids[0].value) {
      continue;
    }
    const core::CartPlan& morning_cart = before.groom_plan.carts.front();
    std::size_t index = 0;
    for (; index < morning_cart.legs.size() && morning_cart.legs[index].depart <= hour_five;
         ++index) {
      kept = kept && index < cart.legs.size() &&
             cart.legs[index].depart == morning_cart.legs[index].depart &&
             cart.legs[index].arrive == morning_cart.legs[index].arrive &&
             cart.legs[index].task.value == morning_cart.legs[index].task.value;
    }
    for (; index < cart.legs.size(); ++index) {
      nothing_early = nothing_early && cart.legs[index].depart > hour_five;
    }
  }
  failures += Expect(replanned && !second_has_cart,
                     "re-plan: a carter who lost his horse makes the plan stale — re-planned, and "
                     "he has no cart in it");
  failures += Expect(kept && nothing_early,
                     "re-plan: the other cart keeps every leg begun by then, and nothing new "
                     "leaves before the next hour");
  core::WorldState calm;
  bool calm_replanned = true;
  day_of(false, calm_replanned, calm);
  failures += Expect(!calm_replanned, "re-plan: nothing changed, nothing re-planned");
  return failures;
}

/// THE WAY HOME FITS THE LIGHT (0.37.181; 0.37.180's canon: a goods cart's
/// leg home after sunset in all 27 village-years, core-legsprobe2 late_h): a
/// winter's day of four hours, 10 to 14. A cart carts its own heap 100 m out
/// for an hour; the next heap of its level lies 2.5 km away — 2.5 hours
/// there, 2.5 back — and does not fit what is left of the light. The cart
/// goes home instead, home by 14, the far heap kept in its chain as a leg of
/// no length. The pair: in a day of twelve hours it rides to the far heap.
int TestTheWayHomeFitsTheLight() {
  int failures = 0;
  const auto day_of = [](float light_hours, bool& rode_far, core::Tick& home_by) {
    core::LogisticsConfig config;  // 12 km/h: 1 game hour a km
    core::WorldState world;
    world.weather.daylight_hours = light_hours;
    const auto heap_at = [&world](float east_m, float days) {
      core::FieldRow field;
      field.kind = core::LandKind::kArable;
      field.center = core::Vec2{.x = east_m, .y = 0.0F};
      field.reaped_grams = 1'000'000;
      field.reaped_resource = core::ResourceId{0};
      field.haul_days_remaining = days;
      return core::AppendRow(world.fields, field);
    };
    const core::FieldId near = heap_at(100.0F, 0.1F);
    const core::FieldId far = heap_at(2'500.0F, 0.1F);
    core::TaskDayCount count;
    core::SyncTasks(config, world, 0, count);
    core::UnitRow house;
    const core::UnitId house_id = core::AppendRow(world.units, house);
    core::FamilyRow household;
    household.house = house_id;
    const core::FamilyId family = core::AppendRow(world.families, household);
    world.units.rows[0].household = family;
    core::ResidentRow carter;
    carter.family = family;
    carter.work.kind = core::WorkKind::kHauling;
    carter.work.field = near;
    carter.work.rides_horse = 1;
    core::AppendRow(world.residents, carter);
    core::LogisticsTally tally;
    const core::GroomPlan plan = core::BuildGroomPlan(config, world, tally);
    rode_far = false;
    home_by = 0;
    if (plan.carts.size() != 1) {
      return;
    }
    const std::vector<core::CartLeg>& legs = plan.carts[0].legs;
    for (std::size_t index = 1; index < legs.size(); ++index) {
      const std::uint32_t row = core::FindRow(world.logistics_tasks, legs[index].task);
      rode_far = rode_far ||
                 (row != core::kNoRow && world.logistics_tasks.rows[row].field.value == far.value &&
                  legs[index - 1].task.value == core::kInvalidEntityIdValue);
    }
    home_by = legs.empty() ? 0 : legs.back().arrive;
  };
  bool winter_far = true;
  bool summer_far = false;
  core::Tick winter_home = 0;
  core::Tick summer_home = 0;
  day_of(4.0F, winter_far, winter_home);
  day_of(12.0F, summer_far, summer_home);
  std::cout << "  plan, the way home: a 4-hour day - rides to the far heap " << winter_far
            << ", home by " << winter_home << "; a 12-hour day - " << summer_far << ", home by "
            << summer_home << '\n';
  failures += Expect(!winter_far && winter_home <= 14,
                     "plan: in a day of four hours the far heap does not fit the light — the cart "
                     "goes home, home by sunset");
  failures += Expect(summer_far && summer_home <= 18,
                     "plan: in a day of twelve hours it rides to the far heap and is home by "
                     "sunset too");
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  failures += TestOneTaskALoad();
  failures += TestAgeing();
  failures += TestThreats();
  failures += TestConfig();
  failures += TestThePlansChains();
  failures += TestTheWayHomeFitsTheLight();
  failures += TestTheReplan();
  if (failures == 0) {
    std::cout << "unit_core_logistics: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
