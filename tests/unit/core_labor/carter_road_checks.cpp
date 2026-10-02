// The checks of the carter's road (carter_road_checks.h).

#include "carter_road_checks.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
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
constexpr float kSeamDays = 50.0F;  // more than one man does in a day

std::unique_ptr<core::ILaborSystem> Labor() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_labor_carter_road";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << error << '\n';
    return nullptr;
  }
  return core::CreateLaborSystem(*tables, core::StubTables::kAllowed);
}

/// The work one man is given, `metres` from his house.
enum class Job : std::uint8_t { kCartAHeap, kCartLogs, kCarryAHeap, kPlough };

/// One man at the map's origin, twelve hours of light, a horse unless he
/// carries on foot; the job `metres` out with fifty days of work on it. The
/// norm-days his whole day takes off its seam.
float DoneInADay(core::ILaborSystem& labor, Job job, float metres) {
  core::WorldState world;
  world.calendar.day_zero_weekday = core::Weekday::kMonday;
  world.weather.daylight_hours = 12.0F;
  world.chairman.harvest_without_days_off = 0;
  core::UnitRow house;
  const core::UnitId house_id = core::AppendRow(world.units, house);
  core::FamilyRow household;
  household.house = house_id;
  const core::FamilyId family = core::AppendRow(world.families, household);
  world.units.rows[0].household = family;
  core::ResidentRow resident;
  resident.family = family;
  resident.birth_day = -300;  // ~25 biological years at day 0
  resident.health = 70.0F;
  resident.rest = 70.0F;
  resident.mood = 60.0F;
  resident.stamina = 50.0F;
  resident.education_stage = core::EducationStage::kPrimary;
  core::AppendRow(world.residents, resident);
  if (job != Job::kCarryAHeap) {
    core::HerdRow team;
    team.kind = core::LivestockKindId{0};  // row 0 is the horse of this livestock.csv
    team.adult_count = 1;
    core::AppendRow(world.herds, team);
  }
  if (job == Job::kCartLogs) {
    core::TimberStandRow stand;
    stand.position = core::Vec2{.x = metres, .y = 0.0F};
    stand.load_grams = 10'000'000;
    stand.haul_days_remaining = kSeamDays;
    stand.haul_days_written = kSeamDays;
    core::AppendRow(world.stands, stand);
  } else {
    core::FieldRow field;
    field.center = core::Vec2{.x = metres, .y = 0.0F};
    field.area_ga = 10.0F;
    if (job == Job::kPlough) {
      field.phase = core::FieldPhase::kPlowing;
      field.work_days_remaining = kSeamDays;
    } else {
      field.phase = core::FieldPhase::kIdle;
      field.reaped_grams = 10'000'000;
      field.reaped_resource = core::ResourceId{0};
      field.haul_days_remaining = kSeamDays;
      field.haul_days_written = kSeamDays;
    }
    core::AppendRow(world.fields, field);
  }
  for (std::uint32_t hour = 0; hour + 1 < core::kTicksPerDay; ++hour) {
    world.calendar.tick = (static_cast<core::Tick>(kWorkingDay) * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(world.calendar);
    const core::WorldState previous = world;
    labor.RunAssignmentDecisions(previous, world);
  }
  if (job == Job::kCartLogs) {
    return kSeamDays - world.stands.rows[0].haul_days_remaining;
  }
  const core::FieldRow& field = world.fields.rows[0];
  return kSeamDays - (job == Job::kPlough ? field.work_days_remaining : field.haul_days_remaining);
}

bool Same(float left, float right) {
  return std::fabs(left - right) < 0.01F * std::fabs(right);
}

}  // namespace

int CheckTheCartersRoad() {
  int failures = 0;
  const auto labor = Labor();
  if (Expect(labor != nullptr, "carter's road: the tables build a labor system") != 0) {
    return 1;
  }
  // THE ROAD TO THE LOAD IS THE FIRST TRIP'S EMPTY HALF (0.37.139). The
  // load's seam is priced in round trips load-store-load; the carter's day
  // had the road home-load-home taken from it besides — a whole trip a day
  // counted twice. A heap two kilometres out and one beside the village:
  // the same norm-days of carting in the same light.
  const float near_heap = DoneInADay(*labor, Job::kCartAHeap, 100.0F);
  const float far_heap = DoneInADay(*labor, Job::kCartAHeap, 2000.0F);
  const float near_logs = DoneInADay(*labor, Job::kCartLogs, 100.0F);
  const float far_logs = DoneInADay(*labor, Job::kCartLogs, 2000.0F);
  const float near_carry = DoneInADay(*labor, Job::kCarryAHeap, 100.0F);
  const float far_carry = DoneInADay(*labor, Job::kCarryAHeap, 600.0F);
  // THE PAIR OF THE REFUSAL: a ploughman's field has no round trip in its
  // seam, and his road is his road.
  const float near_plough = DoneInADay(*labor, Job::kPlough, 100.0F);
  const float far_plough = DoneInADay(*labor, Job::kPlough, 2000.0F);
  std::cout
      << "  carter's road, norm-days off the seam in twelve hours of light, 100 m out and far: "
      << "a rider at a heap " << near_heap << " and " << far_heap << " (2 km); at a stand's "
      << "logs " << near_logs << " and " << far_logs << " (2 km); a walker at a heap " << near_carry
      << " and " << far_carry << " (600 m); a ploughman " << near_plough << " and " << far_plough
      << " (2 km)\n";
  failures += Expect(near_heap > 0.0F && Same(far_heap, near_heap),
                     "carter's road: a rider's day at a heap two kilometres out takes as much off "
                     "its seam as at one beside the village - the road there is his first trip");
  failures += Expect(near_logs > 0.0F && Same(far_logs, near_logs),
                     "carter's road: and the same at a stand's logs");
  failures += Expect(near_carry > 0.0F && Same(far_carry, near_carry),
                     "carter's road: and for the carrier on foot, whose walk to the heap is his "
                     "first trip's too");
  failures += Expect(near_plough > 0.0F && far_plough < 0.8F * near_plough,
                     "carter's road: a ploughman two kilometres out loses his road from the day, "
                     "as before - a field has no round trip in its seam");
  return failures;
}
