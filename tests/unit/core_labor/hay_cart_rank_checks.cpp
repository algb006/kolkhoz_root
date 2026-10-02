// The checks of the rank of the meadow's hay cart (hay_cart_rank_checks.h).

#include "hay_cart_rank_checks.h"

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

// Day 30 is a Wednesday in August (four days a month, day 0 a Monday): the
// month the hay is cut and the fallow for the rye is ploughed.
constexpr std::uint32_t kAugustDay = 30;

/// What production is made to answer, and what it was asked.
struct Manger {
  bool short_of_hay = false;
  std::uint32_t asked = 0;       ///< Times the question was put.
  std::uint32_t asked_days = 0;  ///< The days of the last question.
};

/// A labor system on a table set of one winter crop and the horse;
/// `labor_csv` empty leaves labor.csv out — the documented defaults.
std::unique_ptr<core::ILaborSystem> Labor(const char* directory,
                                          Manger* manger,
                                          const std::string& labor_csv = {}) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / directory;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "crops.csv") << "key,sow_to_month,harvest_to_month,is_winter\n"
                                       "rye_winter,12,7,1\n";
  std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\n";
  if (!labor_csv.empty()) {
    std::ofstream(root / "labor.csv") << labor_csv;
  }
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << error << '\n';
    return nullptr;
  }
  if (manger == nullptr) {
    return core::CreateLaborSystem(*tables, core::StubTables::kAllowed);
  }
  return core::CreateLaborSystem(*tables,
                                 core::StubTables::kAllowed,
                                 core::kDaysPerYear - 1U,
                                 {},
                                 core::RainDayShares{},
                                 [manger](const core::WorldState& /*world*/, std::uint32_t days) {
                                   ++manger->asked;
                                   manger->asked_days = days;
                                   return manger->short_of_hay;
                                 });
}

/// What the one man with the one horse does at noon.
enum class Sent : std::uint8_t { kNowhere, kToTheHeap, kToTheFallow, kToTheLogs };

/// What the heap is set against.
enum class Rival : std::uint8_t { kTheFallowForRye, kTheStandsLogs };

/// One man, one horse, a day of August; a heap lying on land of `heap_land`
/// against the rival, equal work. THE HEAP HAS EVERY LATER KEY OF THE QUEUE:
/// its field is the first row of its table and it lies the nearer, so only
/// the rank can send the horse elsewhere.
Sent TheHorseOnADayOfAugust(core::ILaborSystem& labor, core::LandKind heap_land, Rival rival) {
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
  core::HerdRow team;
  team.kind = core::LivestockKindId{0};  // row 0 is the horse of this livestock.csv
  team.adult_count = 1;
  core::AppendRow(world.herds, team);

  core::FieldRow heap;
  heap.kind = heap_land;
  heap.center = core::Vec2{.x = 0.0F, .y = 20.0F};
  heap.area_ga = 10.0F;
  heap.phase = core::FieldPhase::kIdle;
  heap.reaped_grams = 10'000'000;
  heap.haul_days_remaining = 5.0F;
  const core::FieldId heap_id = core::AppendRow(world.fields, heap);
  core::FieldId fallow_id;
  core::TimberStandId stand_id;
  if (rival == Rival::kTheFallowForRye) {
    core::FieldRow fallow;
    fallow.center = core::Vec2{.x = 0.0F, .y = -20.0F};
    fallow.area_ga = 10.0F;
    fallow.phase = core::FieldPhase::kPlowing;
    fallow.work_days_remaining = 5.0F;
    fallow.rotation_assigned = 1;
    fallow.rotation_year1 = core::CropId{0};
    fallow_id = core::AppendRow(world.fields, fallow);
  } else {
    core::TimberStandRow stand;
    stand.position = core::Vec2{.x = 0.0F, .y = -40.0F};
    stand.load_grams = 10'000'000;
    stand.haul_days_remaining = 5.0F;
    stand.haul_days_written = 5.0F;
    stand_id = core::AppendRow(world.stands, stand);
  }

  for (std::uint32_t hour = 0; hour <= 12; ++hour) {
    world.calendar.tick = (static_cast<core::Tick>(kAugustDay) * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(world.calendar);
    const core::WorldState previous = world;
    labor.RunAssignmentDecisions(previous, world);
  }
  const core::WorkAssignment& work = world.residents.rows[0].work;
  if (work.kind == core::WorkKind::kHauling && work.field.value == heap_id.value) {
    return Sent::kToTheHeap;
  }
  if (rival == Rival::kTheFallowForRye && work.kind == core::WorkKind::kPlowing &&
      work.field.value == fallow_id.value) {
    return Sent::kToTheFallow;
  }
  if (rival == Rival::kTheStandsLogs && work.kind == core::WorkKind::kHauling &&
      work.stand.value == stand_id.value) {
    return Sent::kToTheLogs;
  }
  return Sent::kNowhere;
}

const char* Name(Sent sent) {
  switch (sent) {
    case Sent::kToTheHeap:
      return "the heap";
    case Sent::kToTheFallow:
      return "the fallow";
    case Sent::kToTheLogs:
      return "the logs";
    case Sent::kNowhere:
      break;
  }
  return "nowhere";
}

/// THE STORES HOLD THE DAYS AHEAD: the hay waits — behind the fallow for the
/// rye (0.37.128) and behind the stand's logs (0.37.132).
int CheckTheHayWaitsWhileTheStoresHold() {
  int failures = 0;
  Manger manger;
  const auto labor = Labor("unit_core_labor_hay_cart_holds", &manger);
  if (Expect(labor != nullptr, "hay cart's rank: the tables build a labor system") != 0) {
    return 1;
  }
  const Sent fallow =
      TheHorseOnADayOfAugust(*labor, core::LandKind::kMeadow, Rival::kTheFallowForRye);
  const Sent logs = TheHorseOnADayOfAugust(*labor, core::LandKind::kMeadow, Rival::kTheStandsLogs);
  std::cout << "  the hay cart's rank, the stores holding the days ahead: a meadow's heap against "
            << "the fallow for rye - the horse goes to " << Name(fallow)
            << "; against a stand's logs - to " << Name(logs) << "; the days asked of production "
            << manger.asked_days << '\n';
  failures += Expect(fallow == Sent::kToTheFallow,
                     "hay cart's rank: with the days of hay in store the hay at a meadow waits "
                     "and the horse ploughs the fallow for this autumn's rye");
  failures += Expect(logs == Sent::kToTheLogs,
                     "hay cart's rank: with the days of hay in store the stand's logs are carted "
                     "before the hay, though the meadow is the nearer");
  failures += Expect(manger.asked > 0 && manger.asked_days == 8,
                     "hay cart's rank: production is asked about the default's eight days ahead");
  // AN ARABLE FIELD'S LOAD is not the hay's matter: it keeps its window and
  // production is not asked.
  manger = Manger{};
  const Sent grain_fallow =
      TheHorseOnADayOfAugust(*labor, core::LandKind::kArable, Rival::kTheFallowForRye);
  const Sent grain_logs =
      TheHorseOnADayOfAugust(*labor, core::LandKind::kArable, Rival::kTheStandsLogs);
  failures += Expect(grain_fallow == Sent::kToTheHeap && grain_logs == Sent::kToTheHeap,
                     "hay cart's rank: the grain lying on a field is carted ahead of the fallow "
                     "and of the logs, as before");
  failures += Expect(manger.asked == 0,
                     "hay cart's rank: and with no meadow's heap lying production is not asked");
  return failures;
}

/// THE STORES ARE SHORT: the hay has a field load's window — before the logs
/// and before the fallow for the rye.
int CheckTheHayGoesFirstWhenTheStoresAreShort() {
  int failures = 0;
  Manger manger;
  manger.short_of_hay = true;
  const auto labor = Labor("unit_core_labor_hay_cart_short", &manger);
  if (Expect(labor != nullptr, "hay cart's rank, short: the tables build a labor system") != 0) {
    return 1;
  }
  const Sent fallow =
      TheHorseOnADayOfAugust(*labor, core::LandKind::kMeadow, Rival::kTheFallowForRye);
  const Sent logs = TheHorseOnADayOfAugust(*labor, core::LandKind::kMeadow, Rival::kTheStandsLogs);
  std::cout << "  the hay cart's rank, the stores short: a meadow's heap against the fallow for "
            << "rye - the horse goes to " << Name(fallow) << "; against a stand's logs - to "
            << Name(logs) << '\n';
  failures += Expect(fallow == Sent::kToTheHeap,
                     "hay cart's rank: with the stores short of the days ahead the hay is carted "
                     "ahead of the fallow for rye - a field load's window");
  failures += Expect(logs == Sent::kToTheHeap,
                     "hay cart's rank: with the stores short the hay is carted ahead of the "
                     "stand's logs");
  return failures;
}

/// THE DAYS ARE THE TABLE'S (labor.csv `hay_cart_need_days`), and nought
/// switches the need off: the hay is the last whatever production would say.
int CheckTheDaysAreTheTables() {
  int failures = 0;
  Manger five;
  five.short_of_hay = true;
  const auto asked_five =
      Labor("unit_core_labor_hay_cart_five", &five, "key,value\nhay_cart_need_days,5\n");
  Manger nought;
  nought.short_of_hay = true;
  const auto never =
      Labor("unit_core_labor_hay_cart_nought", &nought, "key,value\nhay_cart_need_days,0\n");
  if (Expect(asked_five != nullptr && never != nullptr,
             "hay cart's rank, the table's days: the tables build a labor system") != 0) {
    return 1;
  }
  TheHorseOnADayOfAugust(*asked_five, core::LandKind::kMeadow, Rival::kTheStandsLogs);
  failures += Expect(five.asked > 0 && five.asked_days == 5,
                     "hay cart's rank: the days asked of production are labor.csv's five");
  const Sent logs = TheHorseOnADayOfAugust(*never, core::LandKind::kMeadow, Rival::kTheStandsLogs);
  failures += Expect(logs == Sent::kToTheLogs && nought.asked == 0,
                     "hay cart's rank: nought days switch the need off - the hay is the last and "
                     "production is not asked");
  // A LABOR BUILT ALONE, with nobody to ask: the hay is the last.
  const auto alone = Labor("unit_core_labor_hay_cart_alone", nullptr);
  failures +=
      Expect(alone != nullptr &&
                 TheHorseOnADayOfAugust(*alone, core::LandKind::kMeadow, Rival::kTheStandsLogs) ==
                     Sent::kToTheLogs,
             "hay cart's rank: a labor built with no production to ask carts the logs "
             "first");
  return failures;
}

}  // namespace

int CheckTheHayCartsRank() {
  int failures = 0;
  failures += CheckTheHayWaitsWhileTheStoresHold();
  failures += CheckTheHayGoesFirstWhenTheStoresAreShort();
  failures += CheckTheDaysAreTheTables();
  return failures;
}
