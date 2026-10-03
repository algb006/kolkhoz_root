// The checks of the labour hour against the groom's plan (groom_plan_checks.h).

#include "groom_plan_checks.h"

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
