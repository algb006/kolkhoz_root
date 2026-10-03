// The checks of the day with a horse (horse_yard_road_checks.h).

#include "horse_yard_road_checks.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
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
      std::filesystem::temp_directory_path() / "unit_core_labor_horse_yard_road";
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

enum class Job : std::uint8_t { kCartAHeap, kPlough };

/// Where the team stands: at households (not stabled), or stabled at a yard
/// `yard_x` metres along the axis the work lies on (negative: behind the
/// house).
struct Team {
  bool stabled = false;
  float yard_x = 0.0F;
};

/// One man at the map's origin, `light` hours of light (twelve unless
/// said), one kolkhoz horse; the job `metres` out with fifty days of work on
/// it. The norm-days his whole day takes off its seam.
float DoneInADay(core::ILaborSystem& labor, Job job, float metres, Team team, float light = 12.0F) {
  core::WorldState world;
  world.calendar.day_zero_weekday = core::Weekday::kMonday;
  world.weather.daylight_hours = light;
  world.chairman.harvest_without_days_off = 0;
  core::UnitRow house;
  house.level = 1;
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
  core::HerdRow horses;
  horses.kind = core::LivestockKindId{0};  // row 0 is the horse of this livestock.csv
  horses.adult_count = 1;
  if (team.stabled) {
    core::UnitRow yard;
    yard.level = 1;
    yard.position = core::Vec2{.x = team.yard_x, .y = 0.0F};
    horses.unit = core::AppendRow(world.units, yard);
    world.chairman.horses_stabled = 1;
  }
  core::AppendRow(world.herds, horses);
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
  for (std::uint32_t hour = 0; hour + 1 < core::kTicksPerDay; ++hour) {
    world.calendar.tick = (static_cast<core::Tick>(kWorkingDay) * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(world.calendar);
    const core::WorldState previous = world;
    labor.RunAssignmentDecisions(previous, world);
  }
  const core::FieldRow& done = world.fields.rows[0];
  return kSeamDays - (job == Job::kPlough ? done.work_days_remaining : done.haul_days_remaining);
}

bool Same(float left, float right) {
  return std::fabs(left - right) < 0.01F * std::fabs(right);
}

}  // namespace

int CheckTheHorseYardRoad() {
  int failures = 0;
  const auto labor = Labor();
  if (Expect(labor != nullptr, "horse yard: the tables build a labor system") != 0) {
    return 1;
  }
  const Team at_home{};
  const Team yard_behind{.stabled = true, .yard_x = -1500.0F};
  const Team yard_at_house{.stabled = true, .yard_x = 0.0F};
  // A field 300 m out and a heap 2 km out; the yard 1.5 km behind the house,
  // or at the house itself.
  const float plough_home = DoneInADay(*labor, Job::kPlough, 300.0F, at_home);
  const float plough_behind = DoneInADay(*labor, Job::kPlough, 300.0F, yard_behind);
  const float plough_at_house = DoneInADay(*labor, Job::kPlough, 300.0F, yard_at_house);
  const float cart_home = DoneInADay(*labor, Job::kCartAHeap, 2000.0F, at_home);
  // The carter's yard nearer, 500 m behind: walk and ride within the road
  // rule's six hours, so he is sent and his day is measured, not refused.
  const Team yard_near_behind{.stabled = true, .yard_x = -500.0F};
  const float cart_behind = DoneInADay(*labor, Job::kCartAHeap, 2000.0F, yard_near_behind);
  const float cart_at_house = DoneInADay(*labor, Job::kCartAHeap, 2000.0F, yard_at_house);
  std::cout << "  horse yard, norm-days off the seam in twelve hours of light: a ploughman 300 m "
            << "out " << plough_home << " (horses at home), " << plough_behind
            << " (yard 1.5 km behind), " << plough_at_house << " (yard at the house); a carter "
            << "at a heap 2 km out " << cart_home << ", " << cart_behind << " (yard 500 m behind), "
            << cart_at_house << '\n';
  // THE WALK TO THE HORSE IS ROAD (livestock design §5; the human's word of
  // 2 October 2026, «Возчик должен идти на конный двор»).
  failures += Expect(plough_home > 0.0F && plough_behind < 0.8F * plough_home,
                     "horse yard: a ploughman whose team stands 1.5 km behind his house walks "
                     "there and rides back past it - his day loses the road");
  // 500 m each way on foot at 2.4 game hours a kilometre: 2.4 hours of the
  // twelve, 0.8 of the day — and not the ride on to the load, which would
  // leave him under 0.4.
  failures +=
      Expect(cart_home > 0.0F && cart_behind > 0.75F * cart_home && cart_behind < 0.85F * cart_home,
             "horse yard: a carter whose team stands 500 m behind his house loses the walk "
             "to the horse and back from his day - 0.8 of it is left, the ride on to the "
             "load still his first trip");
  // THE PAIR: a yard at the house is the old day — the ploughman's road is
  // the same way, and the carter's ride to the load is still his first
  // trip's empty half, not road.
  failures += Expect(Same(plough_at_house, plough_home),
                     "horse yard: a ploughman whose yard stands at his house works the day he "
                     "worked with the horse at home");
  failures += Expect(cart_home > 0.0F && Same(cart_at_house, cart_home),
                     "horse yard: a carter whose yard stands at his house loses nothing of his "
                     "day to the ride two kilometres to the load - it is his first trip");
  // THE PLACEMENT JUDGES HIS DAY BY THE SAME ROAD (0.37.160): eight hours of
  // winter light, the yard 500 m behind, a heap 2.5 km out — 1.2 hours' walk
  // and 3 hours' ride, inside the six-hour limit. By walk and ride twice
  // over nothing of the day was left and he was refused (0.37.158); by the
  // walk alone 5.6 of the 8 hours are his, as the labour hour counts them.
  const float winter_home = DoneInADay(*labor, Job::kCartAHeap, 2500.0F, at_home, 8.0F);
  const float winter_behind = DoneInADay(*labor, Job::kCartAHeap, 2500.0F, yard_near_behind, 8.0F);
  std::cout << "  horse yard, a carter at a heap 2.5 km out in eight hours of light: "
            << winter_home << " (horses at home), " << winter_behind << " (yard 500 m behind)\n";
  failures += Expect(winter_home > 0.0F && winter_behind > 0.6F * winter_home &&
                         winter_behind < 0.8F * winter_home,
                     "horse yard: in eight hours of winter light a carter whose yard stands 500 m "
                     "behind is sent to a heap 2.5 km out and works 0.7 of the day - the placement "
                     "takes only his walk off it, as the labour hour does");
  return failures;
}
