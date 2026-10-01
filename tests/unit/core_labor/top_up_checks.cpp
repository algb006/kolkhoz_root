// The checks of the top-up against windowless work (top_up_checks.h).

#include "top_up_checks.h"

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

/// One household of `adults` at the map's origin, twelve hours of light.
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
  return world;
}

void RunHour(core::ILaborSystem& labor, core::WorldState& world, std::uint32_t hour) {
  world.calendar.tick = (static_cast<core::Tick>(kWorkingDay) * core::kTicksPerDay) + hour;
  core::RefreshCalendarCaches(world.calendar);
  const core::WorldState previous = world;
  labor.RunAssignmentDecisions(previous, world);
}

std::unique_ptr<core::ILaborSystem> Labor(const char* directory) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / directory;
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

/// What the day looks like after hour 1.
struct Crews {
  std::uint32_t on_the_field = 0;  ///< Of the late field's own work.
  std::uint32_t log_riders = 0;    ///< Carters of the stand's logs on a horse.
  std::uint32_t planters = 0;
  std::uint32_t morning_riders = 0;  ///< Log riders after hour 0.
  std::uint32_t morning_planters = 0;
};

/// THE HORSE (district_lot's red of 0.37.99, seed 1931, day 30): the one
/// horse rides for a stand's logs in the morning — windowless, the lowest
/// horse work of the day — and production then opens a ploughing. The pair:
/// with no ploughing opened the carter keeps his horse and his logs.
Crews LateWorkAgainstTheLogCart(core::ILaborSystem& labor, bool a_ploughing_opens) {
  core::WorldState world = Village(3);
  core::HerdRow team;
  team.kind = core::LivestockKindId{0};  // row 0 is the horse of this livestock.csv
  team.adult_count = 1;
  core::AppendRow(world.herds, team);
  core::TimberStandRow stand;
  stand.position = core::Vec2{.x = 3000.0F, .y = 0.0F};  // 7.2 h on foot: nobody walks it
  stand.load_grams = 10'000'000;
  stand.haul_days_remaining = 5.0F;
  stand.haul_days_written = 5.0F;
  const core::TimberStandId stand_id = core::AppendRow(world.stands, stand);
  core::FieldRow late;
  late.center = core::Vec2{.x = 0.0F, .y = 20.0F};
  late.area_ga = 10.0F;
  late.phase = core::FieldPhase::kIdle;
  const core::FieldId late_id = core::AppendRow(world.fields, late);
  const auto log_riders = [&world, stand_id]() {
    std::uint32_t riders = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      riders += person.work.stand.value == stand_id.value && person.work.rides_horse != 0 ? 1U : 0U;
    }
    return riders;
  };
  Crews crews;
  RunHour(labor, world, 0);
  crews.morning_riders = log_riders();
  if (a_ploughing_opens) {
    // Production opens the field's ploughing after the placement, in hour 0.
    core::FieldRow& opened = world.fields.rows[core::FindRow(world.fields, late_id)];
    opened.phase = core::FieldPhase::kPlowing;
    opened.work_days_remaining = 5.0F;
  }
  RunHour(labor, world, 1);
  crews.log_riders = log_riders();
  for (const core::ResidentRow& person : world.residents.rows) {
    crews.on_the_field +=
        person.work.field.value == late_id.value && person.work.kind == core::WorkKind::kPlowing
            ? 1U
            : 0U;
  }
  return crews;
}

/// THE HANDS, the horse's kin: every hand plants a zone in the morning —
/// windowless, the last work but one of the queue — and production then
/// opens a reaping. The pair: with no reaping opened the planters plant on.
Crews LateWorkAgainstThePlanting(core::ILaborSystem& labor, bool a_reaping_opens) {
  core::WorldState world = Village(3);
  core::TimberStandRow zone;
  zone.kind = core::TimberStandKind::kPlanted;
  zone.position = core::Vec2{.x = 300.0F, .y = 0.0F};
  zone.planted_area_ha = 5.0F;
  zone.work_days_remaining = 20.0F;  // more than three hands plant in a day
  const core::TimberStandId zone_id = core::AppendRow(world.stands, zone);
  core::FieldRow late;
  late.center = core::Vec2{.x = 0.0F, .y = 20.0F};
  late.area_ga = 10.0F;
  late.phase = core::FieldPhase::kGrowing;
  const core::FieldId late_id = core::AppendRow(world.fields, late);
  const auto planters = [&world, zone_id]() {
    std::uint32_t hands = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      hands +=
          person.work.kind == core::WorkKind::kPlanting && person.work.stand.value == zone_id.value
              ? 1U
              : 0U;
    }
    return hands;
  };
  Crews crews;
  RunHour(labor, world, 0);
  crews.morning_planters = planters();
  if (a_reaping_opens) {
    core::FieldRow& opened = world.fields.rows[core::FindRow(world.fields, late_id)];
    opened.phase = core::FieldPhase::kHarvest;
    opened.work_days_remaining = 20.0F;
  }
  RunHour(labor, world, 1);
  crews.planters = planters();
  for (const core::ResidentRow& person : world.residents.rows) {
    crews.on_the_field +=
        person.work.field.value == late_id.value && person.work.kind == core::WorkKind::kHarvest
            ? 1U
            : 0U;
  }
  return crews;
}

}  // namespace

int CheckTopUpAgainstWindowlessWork() {
  int failures = 0;
  const auto labor = Labor("unit_core_labor_top_up_windowless");
  if (Expect(labor != nullptr, "top-up against windowless work: the tables build a labor system") !=
      0) {
    return 1;
  }
  const Crews ploughing = LateWorkAgainstTheLogCart(*labor, true);
  const Crews no_ploughing = LateWorkAgainstTheLogCart(*labor, false);
  std::cout << "  top-up, the horse: morning log riders " << ploughing.morning_riders
            << "; a ploughing opened after the morning - ploughmen " << ploughing.on_the_field
            << ", log riders " << ploughing.log_riders << "; none opened - log riders "
            << no_ploughing.log_riders << '\n';
  failures += Expect(ploughing.morning_riders == 1 && no_ploughing.morning_riders == 1,
                     "top-up, the horse: the morning sends the one horse for the stand's logs");
  failures += Expect(ploughing.on_the_field == 1,
                     "top-up, the horse: a ploughing opened after the morning takes the horse from "
                     "the log cart at hour 1");
  failures += Expect(ploughing.log_riders == 0,
                     "top-up, the horse: and the carter rides for no logs on a horse that ploughs");
  failures += Expect(no_ploughing.log_riders == 1,
                     "top-up, the horse: with no ploughing opened the carter keeps his horse");

  const Crews reaping = LateWorkAgainstThePlanting(*labor, true);
  const Crews no_reaping = LateWorkAgainstThePlanting(*labor, false);
  std::cout << "  top-up, the hands: morning planters " << reaping.morning_planters
            << "; a reaping opened after the morning - reapers " << reaping.on_the_field
            << ", planters " << reaping.planters << "; none opened - planters "
            << no_reaping.planters << '\n';
  failures += Expect(reaping.morning_planters == 3 && no_reaping.morning_planters == 3,
                     "top-up, the hands: the morning sends all three hands to plant the zone");
  failures += Expect(reaping.on_the_field == 3 && reaping.planters == 0,
                     "top-up, the hands: a reaping opened after the morning takes the planters at "
                     "hour 1");
  failures += Expect(no_reaping.planters == 3,
                     "top-up, the hands: with no reaping opened the planters plant on");
  return failures;
}
