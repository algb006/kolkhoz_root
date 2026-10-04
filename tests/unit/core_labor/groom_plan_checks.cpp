// The checks of the labour hour against the groom's plan (groom_plan_checks.h).

#include "groom_plan_checks.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "assignment.h"
#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "core_common/world_state.h"
#include "core_labor/labor_system.h"
#include "core_tables/tables.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

// Day 30 is a Wednesday (four days a month, day 0 a Monday).
constexpr std::uint32_t kWorkingDay = 30;

/// One household of `adults` at the map's origin, twelve hours of light, and
/// one horse.
core::WorldState Village(std::uint32_t adults) {
  core::WorldState world;
  world.calendar.day_zero_weekday = core::Weekday::kMonday;
  world.weather.daylight_hours = 12.0F;
  world.chairman.harvest_without_days_off = 0;
  core::UnitRow house;
  house.position = core::Vec2{.x = 0.0F, .y = 0.0F};
  const core::UnitId house_id = core::AppendRow(world.units, house);
  core::FamilyRow household;
  household.house = house_id;
  const core::FamilyId family = core::AppendRow(world.families, household);
  world.units.rows[0].household = family;
  for (std::uint32_t index = 0; index < adults; ++index) {
    core::ResidentRow resident;
    resident.family = family;
    resident.birth_day = -300;  // ~25 biological years at day 0
    resident.health = 70.0F;
    resident.rest = 70.0F;
    resident.mood = 60.0F;
    resident.stamina = 50.0F;
    resident.education_stage = core::EducationStage::kPrimary;
    core::AppendRow(world.residents, resident);
  }
  core::HerdRow team;
  team.kind = core::LivestockKindId{0};  // row 0 is the horse of this livestock.csv
  team.adult_count = 1;
  core::AppendRow(world.herds, team);
  return world;
}

void RunHour(core::ILaborSystem& labor, core::WorldState& world, std::uint32_t hour) {
  world.calendar.tick = (static_cast<core::Tick>(kWorkingDay) * core::kTicksPerDay) + hour;
  core::RefreshCalendarCaches(world.calendar);
  const core::WorldState previous = world;
  labor.RunAssignmentDecisions(previous, world);
}

std::unique_ptr<core::ILaborSystem> Labor() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_labor_groom_plan";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "crops.csv") << "key,sow_to_month,harvest_to_month,is_winter\n"
                                       "oat,5,9,0\n";
  std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << error << '\n';
    return nullptr;
  }
  return core::CreateLaborSystem(*tables, core::StubTables::kAllowed);
}

core::TimberStandId StandAt(core::WorldState& world, float east_m, float haul_days) {
  core::TimberStandRow stand;
  stand.position = core::Vec2{.x = east_m, .y = 0.0F};
  stand.load_grams = haul_days > 0.0F ? 1'000'000 : 0;
  stand.haul_days_remaining = haul_days;
  stand.haul_days_written = haul_days;
  return core::AppendRow(world.stands, stand);
}

/// The groom's task of a stand's logs, at a level.
core::LogisticsTaskId LogsTask(core::WorldState& world,
                               core::TimberStandId stand,
                               core::LogisticsLevel level) {
  core::LogisticsTaskRow task;
  task.load_kind = core::LogisticsLoadKind::kStandLogs;
  task.stand = stand;
  task.level = level;
  task.base_level = level;
  return core::AppendRow(world.logistics_tasks, task);
}

/// THE CART FOLLOWS ITS CHAIN (B4): the carter on a horse carts stand A,
/// whose logs are all carted; today's plan chains him A → B. At hour 3 —
/// past the morning and the top-up, so nothing but the chain moves him — he
/// goes on to B with his horse. The pair: a plan of yesterday moves nobody.
/// Returns whether after hour 3 the carter rides for B, and whether he still
/// stands on A.
std::pair<bool, bool> TheCartAfterItsLoad(core::ILaborSystem& labor, bool plan_of_today) {
  core::WorldState world = Village(1);
  const core::TimberStandId carted = StandAt(world, 1000.0F, 0.0F);
  const core::TimberStandId next = StandAt(world, 1500.0F, 1.0F);
  const core::LogisticsTaskId carted_task =
      LogsTask(world, carted, core::LogisticsLevel::kOrdinary);
  const core::LogisticsTaskId next_task = LogsTask(world, next, core::LogisticsLevel::kOrdinary);
  core::WorkAssignment& work = world.residents.rows[0].work;
  work.kind = core::WorkKind::kHauling;
  work.stand = carted;
  work.rides_horse = 1;
  world.groom_plan.day = plan_of_today ? kWorkingDay : kWorkingDay - 1;
  core::CartPlan cart;
  cart.driver = world.residents.row_ids[0];
  for (const core::LogisticsTaskId task : {carted_task, next_task}) {
    cart.legs.push_back(core::CartLeg{.from = core::Vec2{},
                                      .to = core::Vec2{},
                                      .task = task,
                                      .depart = 0,
                                      .arrive = 0,
                                      .riders = {}});
  }
  world.groom_plan.carts.push_back(cart);
  RunHour(labor, world, 3);
  const core::WorkAssignment& after = world.residents.rows[0].work;
  return {after.kind == core::WorkKind::kHauling && after.rides_horse != 0 &&
              after.stand.value == next.value,
          after.stand.value == carted.value};
}

/// The task of a field's heap, at a level.
core::FieldId HeapAt(core::WorldState& world, float east_m, float haul_days) {
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.center = core::Vec2{.x = east_m, .y = 0.0F};
  field.reaped_grams = haul_days > 0.0F ? 1'000'000 : 0;
  field.reaped_resource = core::ResourceId{0};
  field.haul_days_remaining = haul_days;
  return core::AppendRow(world.fields, field);
}

core::LogisticsTaskId HeapTask(core::WorldState& world, core::FieldId field) {
  core::LogisticsTaskRow task;
  task.load_kind = core::LogisticsLoadKind::kFieldHeap;
  task.field = field;
  task.level = core::LogisticsLevel::kTerm;
  task.base_level = core::LogisticsLevel::kTerm;
  return core::AppendRow(world.logistics_tasks, task);
}

/// THE CARRIER ON FOOT FOLLOWS HIS CHAIN TOO (B4b; boss [11], default 1): he
/// carries heap A, carted out; today's plan gives him the near heap B. At
/// hour 3 he goes on to B, on foot. Returns whether he carries B on foot.
bool TheWalkerAfterHisHeap(core::ILaborSystem& labor) {
  core::WorldState world = Village(1);
  const core::FieldId carted = HeapAt(world, 200.0F, 0.0F);
  const core::FieldId next = HeapAt(world, 300.0F, 1.0F);
  core::WorkAssignment& work = world.residents.rows[0].work;
  work.kind = core::WorkKind::kHauling;
  work.field = carted;
  world.groom_plan.day = kWorkingDay;
  core::CartPlan cart;
  cart.driver = world.residents.row_ids[0];
  cart.on_foot = true;
  for (const core::LogisticsTaskId task : {HeapTask(world, carted), HeapTask(world, next)}) {
    cart.legs.push_back(core::CartLeg{.from = core::Vec2{},
                                      .to = core::Vec2{},
                                      .task = task,
                                      .depart = 0,
                                      .arrive = 0,
                                      .riders = {}});
  }
  world.groom_plan.carts.push_back(cart);
  RunHour(labor, world, 3);
  const core::WorkAssignment& after = world.residents.rows[0].work;
  return after.kind == core::WorkKind::kHauling && after.rides_horse == 0 &&
         after.field.value == next.value;
}

/// LEVEL 0 GOES AHEAD OF ALL WORK (B3; boss, the logistics thread [9]): one
/// horse, a ploughing open and a stand's logs lying. The ploughing's window
/// takes the horse in the morning (top_up_checks.cpp, the horse); with the
/// logs' task at level 0 the logs take it. Returns: the logs ride, the
/// ploughing ploughs.
std::pair<bool, bool> TheHorseAgainstAnUrgentLoad(core::ILaborSystem& labor, bool urgent) {
  core::WorldState world = Village(3);
  const core::TimberStandId logs = StandAt(world, 1000.0F, 5.0F);
  LogsTask(world, logs, urgent ? core::LogisticsLevel::kUrgent : core::LogisticsLevel::kOrdinary);
  core::FieldRow field;
  field.center = core::Vec2{.x = 0.0F, .y = 20.0F};
  field.area_ga = 10.0F;
  field.phase = core::FieldPhase::kPlowing;
  field.work_days_remaining = 5.0F;
  const core::FieldId ploughed = core::AppendRow(world.fields, field);
  RunHour(labor, world, 0);
  bool logs_ride = false;
  bool ploughs = false;
  for (const core::ResidentRow& person : world.residents.rows) {
    logs_ride =
        logs_ride || (person.work.stand.value == logs.value && person.work.rides_horse != 0);
    ploughs = ploughs || (person.work.field.value == ploughed.value &&
                          person.work.kind == core::WorkKind::kPlowing);
  }
  return {logs_ride, ploughs};
}

/// A PAUSED TASK'S LOAD IS NOT OFFERED (B7; transport §12): a stand's logs
/// lying and their task paused by the chairman — the morning puts nobody on
/// them. The pair: not paused, somebody carts them. Returns whether anybody
/// stands on the logs after hour 0.
bool TheLogsOfATask(core::ILaborSystem& labor, bool paused) {
  core::WorldState world = Village(3);
  const core::TimberStandId logs = StandAt(world, 1000.0F, 5.0F);
  const core::LogisticsTaskId task = LogsTask(world, logs, core::LogisticsLevel::kOrdinary);
  world.logistics_tasks.rows[core::FindRow(world.logistics_tasks, task)].paused = paused;
  RunHour(labor, world, 0);
  return std::ranges::any_of(world.residents.rows, [&logs](const core::ResidentRow& person) {
    return person.work.kind == core::WorkKind::kHauling && person.work.stand.value == logs.value;
  });
}

/// TODAY'S DAY OFF DECLARED WORKING (B8's door, 0.37.190; kCancelDayOff with
/// `enable` 1; boss, the logistics thread [103]): a Sunday, a stand's logs
/// lying and their task at level 0. The hour-0 placement puts nobody on them
/// — a day off. With the order read at hour 4, at hour 5 somebody carts them
/// the same day; without it, nobody till the morning. Returns: anybody on the
/// logs at hour 5, and whether a second order the same hour was refused.
std::pair<bool, bool> TheDeclaredDay(core::ILaborSystem& labor, bool declare) {
  constexpr std::uint32_t kSunday = kWorkingDay + 4;  // day 0 a Monday: 34 is a Sunday
  core::WorldState world = Village(3);
  const core::TimberStandId logs = StandAt(world, 1000.0F, 5.0F);
  LogsTask(world, logs, core::LogisticsLevel::kUrgent);
  const auto run = [&labor, &world](std::uint32_t hour) {
    world.calendar.tick = (static_cast<core::Tick>(kSunday) * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(world.calendar);
    const core::WorldState previous = world;
    labor.RunAssignmentDecisions(previous, world);
  };
  for (std::uint32_t hour = 0; hour < 4; ++hour) {
    run(hour);
  }
  bool second_refused = false;
  if (declare) {
    core::OrderRow order;
    order.kind = core::OrderKind::kCancelDayOff;
    order.enable = 1;
    core::AppendRow(world.orders, order);
    core::AppendRow(world.orders, order);
  }
  run(4);
  if (declare) {
    second_refused = world.orders.rows.size() == 2 &&
                     world.orders.rows[0].status == core::OrderStatus::kDone &&
                     world.orders.rows[1].refusal == core::OrderRefusal::kRuleForbids;
  }
  run(5);
  const bool carted =
      std::ranges::any_of(world.residents.rows, [&logs](const core::ResidentRow& p) {
        return p.work.kind == core::WorkKind::kHauling && p.work.stand.value == logs.value;
      });
  return {carted, second_refused};
}

/// LEVEL 0 IN THE TOP-UP (B5): the morning gives the one horse to a
/// ploughing — the logs' task is ordinary then — and after the placement the
/// task is raised to level 0 (as the hour-0 ageing does). At hour 1 the top-up
/// lets the ploughman's horse go and the urgent logs take it. The pair: the
/// task left ordinary, the plough keeps its horse. Returns: the logs ride, the
/// ploughing ploughs, after hour 1.
///
/// THE PLOUGHING HAS A WINDOW — its field's crop (oat, crops.csv row 0) — so
/// the old top-up does not let it go as windowless work: it did in this
/// check's first form (0.37.184), which passed with the new let-go switched
/// off — 0.37.184's fault «the top-up keeps the windows' horses» reddened
/// nothing.
std::pair<bool, bool> TheHorseAfterARaise(core::ILaborSystem& labor, bool raised) {
  core::WorldState world = Village(3);
  const core::TimberStandId logs = StandAt(world, 1000.0F, 5.0F);
  const core::LogisticsTaskId task = LogsTask(world, logs, core::LogisticsLevel::kOrdinary);
  core::FieldRow field;
  field.center = core::Vec2{.x = 0.0F, .y = 20.0F};
  field.area_ga = 10.0F;
  field.phase = core::FieldPhase::kPlowing;
  field.work_days_remaining = 5.0F;
  field.crop = core::CropId{0};
  const core::FieldId ploughed = core::AppendRow(world.fields, field);
  RunHour(labor, world, 0);
  if (raised) {
    core::LogisticsTaskRow& row =
        world.logistics_tasks.rows[core::FindRow(world.logistics_tasks, task)];
    row.level = core::LogisticsLevel::kUrgent;
  }
  RunHour(labor, world, 1);
  bool logs_ride = false;
  bool ploughs = false;
  for (const core::ResidentRow& person : world.residents.rows) {
    logs_ride =
        logs_ride || (person.work.stand.value == logs.value && person.work.rides_horse != 0);
    ploughs = ploughs || (person.work.field.value == ploughed.value &&
                          person.work.kind == core::WorkKind::kPlowing);
  }
  return {logs_ride, ploughs};
}

/// A passenger's wait as the fixture below makes it.
struct PassengerWait {
  bool work_ended = false;
  std::uint32_t term = 1;
  /// The hour the labour hour runs.
  std::uint32_t hour = 3;
  /// Hours after `since` the cart is due at his point; 0 leaves the record's
  /// own (the term's end).
  std::uint32_t due_after = 0;
  /// Whether today's plan carries him on the driver's cart.
  bool planned = true;
  /// Whether today's plan has the driver's cart at all.
  bool cart_planned = true;
};

/// A PASSENGER'S WAIT GOES WITH ITS WORK (0.37.183; 0.37.182's canon: 29
/// firings of the dog, all «work gone», the record outliving the work the
/// labour hour ended), AND WITH THE CART THAT HAS COME (0.37.186): a passenger
/// waiting from hour 10 for his driver's cart keeps his wait while he still
/// has the work and the cart and the cart is not yet due — or is due and not
/// come, which is the dog's. Returns whether the wait stands after the hour.
bool TheWaitAfterTheWork(core::ILaborSystem& labor, const PassengerWait& asked) {
  const bool work_ended = asked.work_ended;
  core::WorldState world = Village(2);
  core::WorkAssignment& driver = world.residents.rows[0].work;
  driver.kind = core::WorkKind::kHauling;
  driver.rides_horse = 1;
  core::FieldRow field;  // his work: a reaping with days left, so the hour keeps it
  field.center = core::Vec2{.x = 2000.0F, .y = 0.0F};
  field.area_ga = 10.0F;
  field.phase = core::FieldPhase::kHarvest;
  field.work_days_remaining = 5.0F;
  const core::FieldId reaped = core::AppendRow(world.fields, field);
  core::ResidentRow& rider = world.residents.rows[1];
  rider.work.kind = work_ended ? core::WorkKind::kNone : core::WorkKind::kHarvest;
  rider.work.field = work_ended ? core::FieldId{} : reaped;
  // His road measured at the seating: a road measured anew is a target
  // changed since the morning, and the hour drops the seat (labor_system.cpp).
  rider.work.travel_hours = 2.0F;
  rider.work.rides_cart_of = world.residents.row_ids[0];
  const core::Tick since = (static_cast<core::Tick>(kWorkingDay) * core::kTicksPerDay) + 10;
  rider.wait = core::WaitRecord(core::WaitKind::kPassengerAwaitsCart,
                                since,
                                asked.term,
                                core::WaitTarget{.resident = world.residents.row_ids[0],
                                                 .unit = core::UnitId{},
                                                 .field = core::FieldId{}});
  if (asked.due_after != 0) {
    rider.wait->due = since + asked.due_after;
  }
  // Today's plan: the driver's goods cart, its first leg from the yard to
  // the load, the passenger on it — or a plan without him.
  world.groom_plan.day = kWorkingDay;
  core::CartPlan cart{.driver = world.residents.row_ids[0], .legs = {core::CartLeg{}}};
  if (asked.planned) {
    cart.legs.front().riders.push_back(world.residents.row_ids[1]);
  }
  if (asked.cart_planned) {
    world.groom_plan.carts.push_back(cart);
  }
  RunHour(labor, world, asked.hour);
  return world.residents.rows[1].wait.has_value();
}

}  // namespace

int CheckTheGroomsPlan() {
  int failures = 0;
  core::AssignmentJob job;
  job.kind = core::WorkKind::kHauling;
  const int ordinary = core::PlacementTier(job);
  job.logistics_urgent = true;
  failures += Expect(core::PlacementTier(job) == -1 && ordinary >= 0,
                     "groom's plan: a load at level 0 is the tier -1, ahead of every window");
  const auto labor = Labor();
  if (Expect(labor != nullptr, "groom's plan: the tables build a labor system") != 0) {
    return failures + 1;
  }
  const auto [followed, left_behind] = TheCartAfterItsLoad(*labor, true);
  const auto [stale_followed, stale_stays] = TheCartAfterItsLoad(*labor, false);
  std::cout << "  groom's plan, the chain: today's plan - rides for the next load " << followed
            << ", still on the carted " << left_behind << "; yesterday's - rides for the next "
            << stale_followed << ", still on the carted " << stale_stays << '\n';
  failures += Expect(followed && !left_behind,
                     "groom's plan: a carter whose load is carted goes on to the next load of his "
                     "chain, on his horse");
  failures +=
      Expect(!stale_followed && stale_stays, "groom's plan: a plan of yesterday moves nobody");
  failures += Expect(TheWalkerAfterHisHeap(*labor),
                     "groom's plan: a carrier on foot whose heap is carted goes on to the near "
                     "heap of his chain, on foot");
  const auto [raised_logs, raised_ploughs] = TheHorseAfterARaise(*labor, true);
  const auto [kept_logs, kept_ploughs] = TheHorseAfterARaise(*labor, false);
  std::cout << "  groom's plan, a raise after the morning: logs ride " << raised_logs
            << ", ploughing " << raised_ploughs << "; not raised - logs " << kept_logs
            << ", ploughing " << kept_ploughs << '\n';
  failures += Expect(raised_logs && !raised_ploughs,
                     "a load raised to level 0 after the morning takes the horse in the top-up");
  failures += Expect(!kept_logs && kept_ploughs,
                     "an ordinary load leaves the plough its horse in the top-up");
  failures += Expect(!TheWaitAfterTheWork(*labor, {.work_ended = true}),
                     "a passenger's wait: his work ended, the wait goes with it");
  failures +=
      Expect(TheWaitAfterTheWork(*labor, {}),
             "a passenger's wait: still on his work and the cart, it stands until his hour");
  // STRUCK WHEN THE CART HAS COME (0.37.186; boss, the logistics thread [77]
  // p. 2): a passenger waiting from hour 10 on a term of three hours, his
  // cart due two hours on — late — still waits at hour 11 and is struck at
  // hour 12, the cart there. A cart that does not come — today's plan
  // carries him on no cart — leaves the record standing at hour 13, its term
  // out, and past it (hour 14) the dog's «term passed» (core_world's
  // watchdog checks). Until 0.37.186 the record went at the term's end by the
  // clock alone, and the dog's verdict could not fire.
  failures += Expect(TheWaitAfterTheWork(*labor, {.term = 3, .hour = 11, .due_after = 2}),
                     "a passenger's wait: his cart two hours late, the record stands an hour on");
  failures += Expect(!TheWaitAfterTheWork(*labor, {.term = 3, .hour = 12, .due_after = 2}),
                     "and is struck at the hour the cart reaches him");
  failures +=
      Expect(TheWaitAfterTheWork(*labor, {.term = 3, .hour = 13, .due_after = 2, .planned = false}),
             "a cart that does not come leaves the record standing past its due hour and its term");
  failures += Expect(TheWaitAfterTheWork(*labor, {.term = 1, .hour = 11, .planned = false}) &&
                         !TheWaitAfterTheWork(*labor, {.term = 1, .hour = 11}),
                     "a one-hour term: struck at its end with the cart there, standing without");
  // THE PLAN REFUTES, IT DOES NOT CONFIRM: a driver the plan has no cart for
  // (no task on his load; no tasks at all before the first horse yard) is
  // taken at the seating's word — struck at the due hour.
  failures += Expect(
      !TheWaitAfterTheWork(*labor, {.term = 3, .hour = 12, .due_after = 2, .cart_planned = false}),
      "a driver the plan has no cart for: the passenger's wait is struck at its due hour");
  const auto [declared_carted, second_refused] = TheDeclaredDay(*labor, true);
  const auto [sunday_carted, ignored] = TheDeclaredDay(*labor, false);
  (void)ignored;
  failures +=
      Expect(declared_carted && second_refused && !sunday_carted,
             "groom's plan: a Sunday declared working at hour 4 — the urgent logs carted at "
             "hour 5, a second order refused; not declared, nobody till the morning");
  failures += Expect(!TheLogsOfATask(*labor, true) && TheLogsOfATask(*labor, false),
                     "groom's plan: a paused task's load is offered to nobody — unpaused, it is "
                     "carted");
  const auto [urgent_logs, urgent_ploughs] = TheHorseAgainstAnUrgentLoad(*labor, true);
  const auto [ordinary_logs, ordinary_ploughs] = TheHorseAgainstAnUrgentLoad(*labor, false);
  std::cout << "  groom's plan, level 0: logs ride " << urgent_logs << ", ploughing "
            << urgent_ploughs << "; at level 2 logs ride " << ordinary_logs << ", ploughing "
            << ordinary_ploughs << '\n';
  failures += Expect(urgent_logs && !urgent_ploughs,
                     "groom's plan: a load at level 0 takes the one horse ahead of a ploughing");
  failures += Expect(!ordinary_logs && ordinary_ploughs,
                     "groom's plan: at level 2 the ploughing's window keeps the horse");
  return failures;
}
