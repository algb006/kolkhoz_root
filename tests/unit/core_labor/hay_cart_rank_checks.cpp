// The checks of the rank of the meadow's hay cart (hay_cart_rank_checks.h).

#include "hay_cart_rank_checks.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
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
  /// With short_of_hay: the days the stores DO hold - a question about no
  /// more days than these is answered «not short». 0: short at any horizon.
  std::uint32_t days_held = 0;
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
                                   return manger->short_of_hay && days > manger->days_held;
                                 });
}

/// What the one man with the one horse does at noon.
enum class Sent : std::uint8_t { kNowhere, kToTheHeap, kToTheFallow, kToTheLogs };

/// What the heap is set against.
enum class Rival : std::uint8_t { kTheFallowForRye, kTheStandsLogs };

/// The day's word that a stack is urgent by the stores' hay
/// (EventKind::kStackCartUrgent), as a day of the run below said it.
struct UrgentWord {
  int times = 0;
  std::uint32_t days_asked = 0;
  bool named_the_heap = false;
  std::uint32_t at_hour = 0;
};

/// One man, one horse, a day of August; a heap lying on land of `heap_land`
/// against the rival, equal work. THE HEAP HAS EVERY LATER KEY OF THE QUEUE:
/// its field is the first row of its table and it lies the nearer, so only
/// the rank can send the horse elsewhere.
/// `mowing_left`: another meadow stands in its cut with mowing left - an open
/// cut beside the heap.
Sent TheHorseOnADayOfAugust(core::ILaborSystem& labor,
                            core::LandKind heap_land,
                            Rival rival,
                            bool mowing_left = false,
                            std::uint32_t day = kAugustDay,
                            UrgentWord* said = nullptr) {
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
  if (mowing_left) {
    core::FieldRow uncut;
    uncut.kind = core::LandKind::kMeadow;
    uncut.center = core::Vec2{.x = 0.0F, .y = 60.0F};
    uncut.area_ga = 10.0F;
    uncut.phase = core::FieldPhase::kHarvest;
    uncut.harvest_work_days = 10.0F;
    uncut.work_days_remaining = 5.0F;
    core::AppendRow(world.fields, uncut);
  }
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
    world.calendar.tick = (static_cast<core::Tick>(day) * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(world.calendar);
    const core::WorldState previous = world;
    labor.RunAssignmentDecisions(previous, world);
  }
  if (said != nullptr) {
    // The step's events are never cleared by this loop: the day's words.
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kStackCartUrgent) {
        ++said->times;
        said->days_asked = static_cast<std::uint32_t>(event.amount & 0xFFFF);
        said->named_the_heap = event.field.value == heap_id.value;
        said->at_hour = core::HourFromTick(event.tick);
      }
    }
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
  failures += Expect(manger.asked > 0 && manger.asked_days == 12,
                     "hay cart's rank: production is asked about the default's twelve days ahead");
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
  // BUT NOT AHEAD OF AN OPEN CUT'S MOWING (0.37.252): the same short stores -
  // five days of hay held, short of the twelve - with another meadow still
  // to mow: the stack waits, the grass does not. The horse does not go to
  // the heap; where it goes instead (the scythes take no horse, the logs or
  // the fallow do) is the queue's.
  manger.days_held = 5;
  const Sent with_mowing_logs =
      TheHorseOnADayOfAugust(*labor, core::LandKind::kMeadow, Rival::kTheStandsLogs, true);
  const Sent with_mowing_fallow =
      TheHorseOnADayOfAugust(*labor, core::LandKind::kMeadow, Rival::kTheFallowForRye, true);
  std::cout << "  the hay cart's rank, the stores short AND a meadow still to mow: against the "
            << "logs - " << Name(with_mowing_logs) << "; against the fallow - "
            << Name(with_mowing_fallow) << '\n';
  failures += Expect(with_mowing_logs != Sent::kToTheHeap && with_mowing_fallow != Sent::kToTheHeap,
                     "hay cart's rank: while a meadow still has mowing left the stack's carting "
                     "has no window though the stores are short of the twelve days - it does not "
                     "take the horse");
  // AND NOT BESIDE A HUNGRY HERD (labor.csv hay_cart_in_cut_days, 2): one day
  // of hay held - short within the two - and the stack is carted ahead of
  // the mowing and of the fallow after all.
  manger.days_held = 1;
  const Sent nearly_out =
      TheHorseOnADayOfAugust(*labor, core::LandKind::kMeadow, Rival::kTheFallowForRye, true);
  failures += Expect(nearly_out == Sent::kToTheHeap && manger.asked_days == 2,
                     "hay cart's rank: with the stores short within two days the stack is carted "
                     "ahead of an open cut's mowing - production's last question is the urgent "
                     "rule's, about this morning's feeding and the rule's one day");
  return failures;
}

/// URGENT UNDER A DAY OF HAY (0.37.252, form (c); labor.csv
/// `hay_cart_urgent_days`, 1) - A PAIR. The in-cut rule switched off
/// (hay_cart_in_cut_days 0), a meadow still to mow, the stores holding no hay
/// at all: by every other rule the stack waits, last of the windowless, and
/// the horse ploughs the fallow. With the urgent rule's one day it is carted
/// ahead of everything; with the rule at nought it waits as before.
int CheckTheStackIsUrgentUnderADayOfHay() {
  int failures = 0;
  Manger empty;
  empty.short_of_hay = true;  // days_held 0: short at any horizon
  const auto urgent = Labor("unit_core_labor_hay_cart_urgent",
                            &empty,
                            "key,value\nhay_cart_in_cut_days,0\nhay_cart_urgent_days,1\n");
  Manger also_empty;
  also_empty.short_of_hay = true;
  const auto never = Labor("unit_core_labor_hay_cart_not_urgent",
                           &also_empty,
                           "key,value\nhay_cart_in_cut_days,0\nhay_cart_urgent_days,0\n");
  if (Expect(urgent != nullptr && never != nullptr,
             "hay cart's rank, urgent: the tables build a labor system") != 0) {
    return 1;
  }
  UrgentWord said;
  UrgentWord not_said;
  const Sent carted = TheHorseOnADayOfAugust(
      *urgent, core::LandKind::kMeadow, Rival::kTheFallowForRye, true, kAugustDay, &said);
  const Sent waited = TheHorseOnADayOfAugust(
      *never, core::LandKind::kMeadow, Rival::kTheFallowForRye, true, kAugustDay, &not_said);
  // THE DAY's ONE WORD (EventKind::kStackCartUrgent, 0.37.253): thirteen hours
  // of the day run, the rule firing at hour 0 and at the top-up - said once,
  // at hour 1, naming the meadow and the two days asked; with the rule at
  // nought, never.
  failures +=
      Expect(said.times == 1 && said.at_hour == 1 && said.days_asked == 2 && said.named_the_heap,
             "hay cart's rank: the urgent stack is said once that day, at the top-up's "
             "hour, naming the meadow and the two days the stores were asked about");
  failures += Expect(not_said.times == 0,
                     "hay cart's rank: with the urgent rule at nought the word is never said");
  std::cout << "  the hay cart's rank, no hay in the stores and a meadow still to mow: with the "
            << "urgent rule's one day - " << Name(carted) << "; with the rule at nought - "
            << Name(waited) << '\n';
  failures += Expect(carted == Sent::kToTheHeap && empty.asked_days == 2,
                     "hay cart's rank: with no hay in the stores the stack is carted ahead of all "
                     "work - production asked about two days: this morning's feeding and the "
                     "rule's one");
  failures += Expect(waited != Sent::kToTheHeap,
                     "hay cart's rank: with the urgent rule at nought the same stack waits behind "
                     "the open cut");
  // Today's and tomorrow's hay held: not urgent on a Wednesday, and the stack
  // waits with the rule on.
  empty.days_held = 2;
  const Sent held =
      TheHorseOnADayOfAugust(*urgent, core::LandKind::kMeadow, Rival::kTheFallowForRye, true);
  failures += Expect(held != Sent::kToTheHeap,
                     "hay cart's rank: with today's and tomorrow's hay held the urgent rule does "
                     "not fire in the middle of the week");
  // THE SAME TWO DAYS HELD ON THE EVE OF A DAY OFF (day 33, a Saturday; day 0
  // is a Monday): nothing is carted tomorrow, so the days are counted to
  // Monday's feeding - three - and the stack goes today.
  const Sent on_the_eve =
      TheHorseOnADayOfAugust(*urgent, core::LandKind::kMeadow, Rival::kTheFallowForRye, true, 33);
  failures += Expect(on_the_eve == Sent::kToTheHeap && empty.asked_days == 3,
                     "hay cart's rank: on the eve of a day off the same two days held are short "
                     "of Monday's feeding - the stack is carted today, production asked about "
                     "three days");
  return failures;
}

/// THE DAYS ARE THE TABLE'S (labor.csv `hay_cart_need_days`), and nought
/// switches the need off: the hay is the last whatever production would say.
int CheckTheDaysAreTheTables() {
  int failures = 0;
  Manger five;
  five.short_of_hay = true;
  const auto asked_five =
      // The urgent rule off: its one day would be production's LAST question
      // (asked after the need's), and this case reads the last.
      Labor("unit_core_labor_hay_cart_five",
            &five,
            "key,value\nhay_cart_need_days,5\nhay_cart_urgent_days,0\n");
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

/// A RICK IS NOT CARRIED ON FOOT (row 818; AssignmentJob::rick_waits_for_a_cart)
/// - A PAIR. Two men and one horse, one heap on an arable field (its load
/// keeps a field load's window, so only the cargo's form differs): a heap of
/// GRAIN takes the rider and, in the last pass, the man left without a horse;
/// the same heap of HAY takes the rider alone - the second man stays free.
int CheckARickWaitsForItsCart() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_labor_rick_on_foot";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "crops.csv") << "key,sow_to_month,harvest_to_month,is_winter\n"
                                       "rye_winter,12,7,1\n";
  std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\n";
  std::ofstream(root / "resources.csv") << "key,kg_per_unit,cargo_form\n"
                                           "hay,1000,rick\n"
                                           "grain,1000,bulk\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  const auto labor =
      tables == nullptr ? nullptr : core::CreateLaborSystem(*tables, core::StubTables::kAllowed);
  if (Expect(labor != nullptr, "a rick on foot: the tables build a labor system") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const auto carters_on = [&labor](std::uint16_t resource) {
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
    for (int man = 0; man < 2; ++man) {
      core::ResidentRow resident;
      resident.family = family;
      resident.birth_day = -300;
      resident.health = 70.0F;
      resident.rest = 70.0F;
      resident.mood = 60.0F;
      resident.stamina = 50.0F;
      resident.education_stage = core::EducationStage::kPrimary;
      core::AppendRow(world.residents, resident);
    }
    core::HerdRow team;
    team.kind = core::LivestockKindId{0};
    team.adult_count = 1;
    core::AppendRow(world.herds, team);
    core::FieldRow heap;
    heap.kind = core::LandKind::kArable;
    heap.center = core::Vec2{.x = 0.0F, .y = 20.0F};
    heap.area_ga = 10.0F;
    heap.phase = core::FieldPhase::kIdle;
    heap.reaped_grams = 10'000'000;
    heap.reaped_resource = core::ResourceId{resource};
    heap.haul_days_remaining = 5.0F;
    const core::FieldId heap_id = core::AppendRow(world.fields, heap);
    for (std::uint32_t hour = 0; hour <= 12; ++hour) {
      world.calendar.tick = (static_cast<core::Tick>(kAugustDay) * core::kTicksPerDay) + hour;
      core::RefreshCalendarCaches(world.calendar);
      const core::WorldState previous = world;
      labor->RunAssignmentDecisions(previous, world);
    }
    int riders = 0;
    int walkers = 0;
    for (const core::ResidentRow& resident : world.residents.rows) {
      if (resident.work.kind == core::WorkKind::kHauling &&
          resident.work.field.value == heap_id.value) {
        (resident.work.rides_horse != 0 ? riders : walkers) += 1;
      }
    }
    return riders * 10 + walkers;
  };
  const int grain = carters_on(1);
  const int hay = carters_on(0);
  std::cout << "  a rick on foot: two men, one horse, one heap - of grain: " << grain / 10
            << " rider(s), " << grain % 10 << " on foot; of hay: " << hay / 10 << " rider(s), "
            << hay % 10 << " on foot\n";
  failures += Expect(grain == 11,
                     "a rick on foot: a heap of grain takes the rider and the man left without "
                     "a horse, on foot");
  failures += Expect(hay == 10,
                     "a rick on foot: the same heap of hay takes the rider alone - a rick waits "
                     "for its cart and the second man stays free");
  return failures;
}

}  // namespace

int CheckTheHayCartsRank() {
  int failures = 0;
  failures += CheckARickWaitsForItsCart();
  failures += CheckTheHayWaitsWhileTheStoresHold();
  failures += CheckTheHayGoesFirstWhenTheStoresAreShort();
  failures += CheckTheDaysAreTheTables();
  failures += CheckTheStackIsUrgentUnderADayOfHay();
  return failures;
}
