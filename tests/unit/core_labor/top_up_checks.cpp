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

/// THE UNPLACED TAKE THE OPENED JOB, AND ONLY THEY (#633, 0.37.233; time
/// design «Один работник — одна работа в день»). Three hands; a zone to plant
/// of `zone_days` norm-days in the morning; a small reaping — one hand's —
/// opens after it. A zone of twenty days takes all three in the morning, and
/// the reaping waits: nobody is re-placed. A zone of one day leaves hands the
/// morning did not place, and one of them reaps while the planters plant on.
/// Until 0.37.233 the top-up let the planters go and placed them again (one
/// reaped, the others planted - 0.37.206's case, timber_years seed 1929).
Crews TheUnplacedTakeTheOpenedJob(core::ILaborSystem& labor, float zone_days) {
  core::WorldState world = Village(3);
  core::TimberStandRow zone;
  zone.kind = core::TimberStandKind::kPlanted;
  zone.position = core::Vec2{.x = 300.0F, .y = 0.0F};
  zone.planted_area_ha = 5.0F;
  zone.work_days_remaining = zone_days;
  const core::TimberStandId zone_id = core::AppendRow(world.stands, zone);
  core::FieldRow late;
  late.center = core::Vec2{.x = 0.0F, .y = 20.0F};
  late.area_ga = 10.0F;
  late.phase = core::FieldPhase::kGrowing;
  const core::FieldId late_id = core::AppendRow(world.fields, late);
  Crews crews;
  RunHour(labor, world, 0);
  for (const core::ResidentRow& person : world.residents.rows) {
    crews.morning_planters += person.work.kind == core::WorkKind::kPlanting ? 1U : 0U;
  }
  core::FieldRow& opened = world.fields.rows[core::FindRow(world.fields, late_id)];
  opened.phase = core::FieldPhase::kHarvest;
  opened.work_days_remaining = 0.2F;  // one hand's morning
  RunHour(labor, world, 1);
  for (const core::ResidentRow& person : world.residents.rows) {
    crews.planters +=
        person.work.kind == core::WorkKind::kPlanting && person.work.stand.value == zone_id.value
            ? 1U
            : 0U;
    crews.on_the_field +=
        person.work.field.value == late_id.value && person.work.kind == core::WorkKind::kHarvest
            ? 1U
            : 0U;
  }
  return crews;
}

/// The labour system of the people's cart: six seats for a walk past two
/// hours (transport.csv), and a cow beside the horse (livestock.csv row 1).
std::unique_ptr<core::ILaborSystem> LaborWithThePeoplesCart() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_labor_top_up_peoples_cart";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  // Ten cows at 67.2 real care-days a head a year — a real day is a seventh
  // of a game one — are two norm-days of barn work a day.
  std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\ncow,67.2\n";
  std::ofstream(root / "transport.csv") << "key,speed_kmh,seats,min_walk_hours\n"
                                           "pedestrian,5,,\n"
                                           "horse_trot,12,,\n"
                                           "people_cart,,6,2\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << error << '\n';
    return nullptr;
  }
  return core::CreateLaborSystem(*tables, core::StubTables::kAllowed);
}

/// What the top-up did to the horses of a people's cart.
struct LentHorses {
  std::uint32_t morning_log_riders = 0;
  std::uint32_t morning_peoples_carts = 0;
  std::uint32_t log_riders = 0;
  std::uint32_t peoples_carts = 0;
  std::uint32_t morning_carers = 0;  ///< On the barn's care after hour 0.
};

/// THE PEOPLE'S CART KEEPS ITS HORSE (#633, 0.37.233). Two horses: in the
/// morning one carts a stand's logs 3 km out and the other, left over,
/// carries the three hands of a barn a kilometre out (2.4 hours on foot). At
/// hour 1 a second stand's logs lie: no horse is free, the barn's cart keeps
/// its horse and its crew their ride, the first carter his load - and the
/// second load waits for tomorrow's morning plan. From 0.37.168 to 0.37.232
/// the top-up borrowed the cart's horse (the carter let go and placed again
/// with both loads; the barn's hands walked).
LentHorses TheTopUpBorrowsThePeoplesCart(core::ILaborSystem& labor) {
  core::WorldState world = Village(6);
  core::HerdRow team;
  team.kind = core::LivestockKindId{0};
  team.adult_count = 2;
  core::AppendRow(world.herds, team);
  core::UnitRow barn;
  barn.position = core::Vec2{.x = 1000.0F, .y = 0.0F};
  core::HerdRow cows;
  cows.kind = core::LivestockKindId{1};
  cows.unit = core::AppendRow(world.units, barn);
  cows.adult_count = 10;
  cows.care_days_remaining = 2.0F;
  core::AppendRow(world.herds, cows);
  const auto stand_at = [&world](float metres, float haul_days) {
    core::TimberStandRow stand;
    stand.position = core::Vec2{.x = metres, .y = 0.0F};
    stand.load_grams = haul_days > 0.0F ? 1'000'000 : 0;
    stand.haul_days_remaining = haul_days;
    stand.haul_days_written = haul_days;
    return core::AppendRow(world.stands, stand);
  };
  stand_at(3000.0F, 0.5F);
  const core::TimberStandId second = stand_at(-3000.0F, 0.0F);
  const auto count = [&world](LentHorses& into, bool morning) {
    std::uint32_t log_riders = 0;
    std::uint32_t carts = 0;
    std::uint32_t carers = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      carers += person.work.kind == core::WorkKind::kHerdCare ? 1U : 0U;
      log_riders +=
          person.work.kind == core::WorkKind::kHauling && person.work.rides_horse != 0 ? 1U : 0U;
      carts +=
          person.work.kind == core::WorkKind::kHerdCare && person.work.rides_horse != 0 ? 1U : 0U;
    }
    (morning ? into.morning_log_riders : into.log_riders) = log_riders;
    (morning ? into.morning_peoples_carts : into.peoples_carts) = carts;
    if (morning) {
      into.morning_carers = carers;
    }
  };
  LentHorses horses;
  RunHour(labor, world, 0);
  count(horses, true);
  core::TimberStandRow& lying = world.stands.rows[core::FindRow(world.stands, second)];
  lying.load_grams = 1'000'000;
  lying.haul_days_remaining = 0.5F;
  lying.haul_days_written = 0.5F;
  RunHour(labor, world, 1);
  count(horses, false);
  return horses;
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
  // ONE JOB A DAY (#633): until 0.37.233 the ploughing opened after the
  // morning took the horse from the log cart at hour 1 (district_lot's case
  // of 0.37.101, seed 1931 day 30); the morning's placement stands now, and
  // the ploughing waits for the next morning plan.
  failures += Expect(ploughing.morning_riders == 1 && no_ploughing.morning_riders == 1,
                     "top-up, the horse: the morning sends the one horse for the stand's logs");
  failures += Expect(ploughing.on_the_field == 0 && ploughing.log_riders == 1,
                     "top-up, the horse: a ploughing opened after the morning does not take the "
                     "horse from the log cart - the carter keeps his day's one job");
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
  failures += Expect(reaping.on_the_field == 0 && reaping.planters == 3,
                     "top-up, the hands: a reaping opened after the morning does not take the "
                     "planters at hour 1 - their day's one job stands");
  failures += Expect(no_reaping.planters == 3,
                     "top-up, the hands: with no reaping opened the planters plant on");

  const Crews all_placed = TheUnplacedTakeTheOpenedJob(*labor, 20.0F);
  const Crews hands_left = TheUnplacedTakeTheOpenedJob(*labor, 1.0F);
  std::cout << "  top-up, the unplaced: a zone of 20 days - morning planters "
            << all_placed.morning_planters << ", then reapers " << all_placed.on_the_field
            << ", planters " << all_placed.planters << "; a zone of 1 day - morning planters "
            << hands_left.morning_planters << ", then reapers " << hands_left.on_the_field
            << ", planters " << hands_left.planters << '\n';
  failures += Expect(
      all_placed.morning_planters == 3 && all_placed.on_the_field == 0 && all_placed.planters == 3,
      "top-up, the unplaced: with every hand placed in the morning the one-hand "
      "reaping waits - nobody is re-placed");
  failures += Expect(hands_left.morning_planters < 3 && hands_left.on_the_field == 1 &&
                         hands_left.planters == hands_left.morning_planters,
                     "top-up, the unplaced: a hand the morning left reaps the small field, and the "
                     "planters plant on as placed");

  const auto carting = LaborWithThePeoplesCart();
  if (Expect(carting != nullptr, "top-up, the people's cart: the tables build a labor system") !=
      0) {
    return failures + 1;
  }
  const LentHorses lent = TheTopUpBorrowsThePeoplesCart(*carting);
  std::cout << "  top-up, the people's cart: morning log riders " << lent.morning_log_riders
            << ", barn carts " << lent.morning_peoples_carts << " of " << lent.morning_carers
            << " carers; after hour 1 log riders " << lent.log_riders << ", barn carts "
            << lent.peoples_carts << '\n';
  failures += Expect(lent.morning_log_riders == 1 && lent.morning_peoples_carts == 1,
                     "top-up, the people's cart: the morning carts one load and gives the other "
                     "horse, left over, to the barn's far hands");
  failures += Expect(lent.log_riders == 1 && lent.peoples_carts == 1,
                     "top-up, the people's cart: a second load at hour 1 takes no horse - the "
                     "barn's cart keeps it and its crew their ride, the second load waits");
  return failures;
}
