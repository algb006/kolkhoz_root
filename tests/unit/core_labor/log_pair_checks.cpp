// The checks of logs by two (log_pair_checks.h).
//
// THE KNOWN ANSWERS, by hand from transport design §2: a works that waits for logs lying in a
// pile gets PAIRS, and only when all three hold - it is delivering, no horse is left for it this
// morning, and the pile is within 300 m of it on foot. Four free hands are two pairs; three are
// one pair and a hand nobody sends; a pile 400 m off gets nobody and is a NEAR MISS in the year's
// book; one horse in the pool carts the logs and no pair joins it; a works whose next resource is
// clay is carried by lone backs as before. A pair's head drains the seam the pair's rate wrote -
// two heads twice one.
#include "log_pair_checks.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_labor/labor_system.h"
#include "core_tables/tables.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: log pairs: " << label << '\n';
    return 1;
  }
  return 0;
}

// Day 30 is a Wednesday (four days a month, day 0 a Monday).
constexpr std::uint32_t kWorkingDay = 30;

/// One household of `adults` at the map's origin, twelve hours of light, and
/// `horses` grown kolkhoz horses.
core::WorldState Village(std::uint32_t adults, std::uint16_t horses) {
  core::WorldState world;
  world.calendar.day_zero_weekday = core::Weekday::kMonday;
  world.weather.daylight_hours = 12.0F;
  world.chairman.harvest_without_days_off = 0;
  core::UnitRow house;
  house.level = 1;
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
  if (horses > 0) {
    core::HerdRow team;
    team.kind = core::LivestockKindId{0};
    team.adult_count = horses;
    core::AppendRow(world.herds, team);
  }
  return world;
}

/// A works 100 m from the houses that waits for its supply from a pile
/// `pile_m` further on: `clay_kg` of clay (resource 0 of the tables below)
/// and `log_kg` of logs (resource 1) on the front leg, five days of carrying
/// written.
core::UnitId AddWorks(core::WorldState& world,
                      float pile_m,
                      core::Grams clay_kg,
                      core::Grams log_kg) {
  core::UnitRow site;
  site.position = core::Vec2{.x = 100.0F, .y = 0.0F};
  site.construction.phase = core::ConstructionPhase::kDelivering;
  site.construction.target_level = 1;
  core::SupplyLeg leg;
  leg.from = core::Vec2{.x = 100.0F + pile_m, .y = 0.0F};
  leg.grams = {clay_kg * core::kGramsPerKilogram, log_kg * core::kGramsPerKilogram};
  site.construction.legs.push_back(leg);
  site.construction.awaiting = leg.grams;
  site.construction.supply_from = leg.from;
  site.construction.supply_days_remaining = 5.0F;
  site.construction.supply_days_written = 5.0F;
  site.construction.supply_days_first = 5.0F;
  site.construction.supply_days_all = 5.0F;
  return core::AppendRow(world.units, site);
}

void RunHours(core::ILaborSystem& labor, core::WorldState& world, std::uint32_t upto) {
  for (std::uint32_t hour = 0; hour <= upto; ++hour) {
    world.calendar.tick = (static_cast<core::Tick>(kWorkingDay) * core::kTicksPerDay) + hour;
    core::RefreshCalendarCaches(world.calendar);
    const core::WorldState previous = world;
    labor.RunAssignmentDecisions(previous, world);
  }
}

std::unique_ptr<core::ILaborSystem> Labor() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "unit_core_labor_log_pairs";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\n";
  std::ofstream(root / "resources.csv")
      << "key,kg_per_unit,cargo_form\nclay,1000,bulk\nlog,200,long\n";
  std::ofstream(root / "labor.csv") << "key,value\ncarry_kg_adult,20\ncarry_kg_rick,10\n"
                                       "carry_kg_stretcher_pair,40\nlog_pair_max_leg_m,300\n"
                                       "log_pair_loaded_pace,0.5\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << error << '\n';
    return nullptr;
  }
  return core::CreateLaborSystem(*tables, core::StubTables::kAllowed);
}

struct Crew {
  std::uint32_t riders = 0;
  std::uint32_t walkers = 0;
};

Crew CarriersOf(const core::WorldState& world, core::UnitId works) {
  Crew crew;
  for (const core::ResidentRow& person : world.residents.rows) {
    if (person.work.kind == core::WorkKind::kHauling && person.work.unit.value == works.value) {
      ++(person.work.rides_horse != 0 ? crew.riders : crew.walkers);
    }
  }
  return crew;
}

float SeamOf(const core::WorldState& world, core::UnitId works) {
  return world.units.rows[core::FindRow(world.units, works)].construction.supply_days_remaining;
}

}  // namespace

int CheckLogPairs() {
  int failures = 0;
  const auto labor = Labor();
  if (Expect(labor != nullptr, "the tables build a labor system") != 0) {
    return 1;
  }
  // FOUR HANDS, NO HORSE, THE PILE 200 m OFF: two pairs.
  {
    core::WorldState world = Village(4, 0);
    const core::UnitId works = AddWorks(world, 200.0F, 0, 2000);
    RunHours(*labor, world, 0);
    const Crew crew = CarriersOf(world, works);
    std::cout << "  log pairs, four hands: riders " << crew.riders << ", walkers " << crew.walkers
              << '\n';
    failures += Expect(crew.walkers == 4 && crew.riders == 0,
                       "four free hands with no horse go to the works' logs as two pairs");
    failures += Expect(world.ledger.current.log_pair_days == 1 &&
                           world.ledger.current.log_pair_worst_leg_m > 199.0F &&
                           world.ledger.current.log_pair_worst_leg_m < 201.0F &&
                           world.ledger.current.log_pair_near_miss_days == 0,
                       "the year's book counts the day and the pair's walk, 200 m, and no near "
                       "miss");
  }
  // THREE HANDS: one pair, and the third is sent nowhere near the log.
  {
    core::WorldState world = Village(3, 0);
    const core::UnitId works = AddWorks(world, 200.0F, 0, 2000);
    RunHours(*labor, world, 0);
    failures += Expect(CarriersOf(world, works).walkers == 2,
                       "three free hands are one pair - the odd hand is not sent under a log");
  }
  // TWO HEADS DRAIN TWICE ONE... and one alone drains nothing: a day of two
  // pairs against a day of one pair, the seam the pair's rate wrote.
  {
    const auto drained = [&labor](std::uint32_t adults) {
      core::WorldState world = Village(adults, 0);
      const core::UnitId works = AddWorks(world, 200.0F, 0, 200'000);
      world.units.rows[core::FindRow(world.units, works)].construction.supply_days_remaining =
          50.0F;
      world.units.rows[core::FindRow(world.units, works)].construction.supply_days_written = 50.0F;
      RunHours(*labor, world, core::kTicksPerDay - 2U);
      return 50.0F - SeamOf(world, works);
    };
    const float one_pair = drained(2);
    const float two_pairs = drained(4);
    const float odd = drained(3);
    const float alone = drained(1);
    std::cout << "  log pairs, a day's seam: one pair " << one_pair << ", two pairs " << two_pairs
              << ", three hands " << odd << ", one hand " << alone << '\n';
    failures += Expect(one_pair > 0.0F, "a pair drains the works' seam");
    failures += Expect(two_pairs > one_pair * 1.9F && two_pairs < one_pair * 2.1F,
                       "two pairs drain twice what one does");
    failures += Expect(odd > one_pair * 0.99F && odd < one_pair * 1.01F,
                       "three hands drain what one pair does - the odd hand carries no log");
    failures += Expect(alone == 0.0F, "one hand alone carries no log at all");
  }
  // THE PILE 400 m OFF: nobody, and a near miss in the book.
  {
    core::WorldState world = Village(4, 0);
    const core::UnitId works = AddWorks(world, 400.0F, 0, 2000);
    RunHours(*labor, world, 0);
    const Crew crew = CarriersOf(world, works);
    failures +=
        Expect(crew.walkers == 0 && crew.riders == 0, "a pile beyond the pair's 300 m gets nobody");
    failures += Expect(world.ledger.current.log_pair_near_miss_days == 1 &&
                           world.ledger.current.log_pair_days == 0,
                       "and the day is a near miss in the year's book, not a day of pairs");
  }
  // A HORSE IN THE POOL: the cart takes the logs and no pair joins it.
  {
    core::WorldState world = Village(4, 1);
    const core::UnitId works = AddWorks(world, 200.0F, 0, 2000);
    RunHours(*labor, world, 0);
    const Crew crew = CarriersOf(world, works);
    std::cout << "  log pairs, one horse: riders " << crew.riders << ", walkers " << crew.walkers
              << '\n';
    failures += Expect(crew.riders == 1 && crew.walkers == 0,
                       "with a horse for it the cart carts the logs and no pair is sent");
    failures += Expect(world.ledger.current.log_pair_days == 0 &&
                           world.ledger.current.log_pair_near_miss_days == 0,
                       "and the book counts neither a pair's day nor a near miss");
  }
  // NOT DELIVERING: the gate's first term - a works not waiting for its supply.
  {
    core::WorldState world = Village(4, 0);
    const core::UnitId works = AddWorks(world, 200.0F, 0, 2000);
    world.units.rows[core::FindRow(world.units, works)].construction.phase =
        core::ConstructionPhase::kBuilding;
    RunHours(*labor, world, 0);
    failures += Expect(CarriersOf(world, works).walkers == 0,
                       "a works that is not delivering gets no pair under its logs");
  }
  // CLAY BEFORE THE LOGS: one resource a trip, the first by id - lone backs,
  // in the queue's own place (no cart in the settlement), odd counts and all.
  {
    core::WorldState world = Village(3, 0);
    const core::UnitId works = AddWorks(world, 200.0F, 5000, 2000);
    RunHours(*labor, world, 0);
    failures += Expect(CarriersOf(world, works).walkers == 3,
                       "while clay is the front leg's first resource three lone backs carry it");
  }
  // A LOAD THAT BECAME A LOG AT NOON: three lone backs were sent for the clay;
  // the clay carried out, the next trip is a log - and the hour's own gate
  // lets two of them carry it as a pair and stands the third. The pair of
  // worlds: the same three hands for six hours on logs from the morning (one
  // pair placed) drain the same.
  {
    const auto six_hours_on_logs = [&labor](bool clay_first) {
      core::WorldState world = Village(3, 0);
      const core::UnitId works = AddWorks(world, 200.0F, clay_first ? 5000 : 0, 200'000);
      core::ConstructionState& site =
          world.units.rows[core::FindRow(world.units, works)].construction;
      site.supply_days_remaining = 50.0F;
      site.supply_days_written = 50.0F;
      RunHours(*labor, world, 0);
      const std::uint32_t placed = CarriersOf(world, works).walkers;
      // The clay is on the site; the seam stands as it was for the comparison.
      site.legs.front().grams[0] = 0;
      site.awaiting[0] = 0;
      site.supply_days_remaining = 50.0F;
      for (std::uint32_t hour = 8; hour < 14; ++hour) {
        world.calendar.tick = (static_cast<core::Tick>(kWorkingDay) * core::kTicksPerDay) + hour;
        core::RefreshCalendarCaches(world.calendar);
        const core::WorldState previous = world;
        labor->RunAssignmentDecisions(previous, world);
      }
      std::cout << "  log pairs, " << (clay_first ? "clay then logs" : "logs from the morning")
                << ": " << placed << " hands placed, six hours drained "
                << 50.0F - SeamOf(world, works) << '\n';
      return 50.0F - SeamOf(world, works);
    };
    const float pair_from_morning = six_hours_on_logs(false);
    const float three_at_noon = six_hours_on_logs(true);
    failures += Expect(pair_from_morning > 0.0F && three_at_noon > pair_from_morning * 0.99F &&
                           three_at_noon < pair_from_morning * 1.01F,
                       "three lone backs whose load became a log carry it as ONE pair - the "
                       "third stands that hour");
  }
  // THE LOG'S NUMBERS COME OFF THE TABLE: with the gate's walk set to 150 m a
  // pile 200 m off gets nobody - the 300 is not a constant of the code.
  {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "unit_core_labor_log_pairs_150";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\n";
    std::ofstream(root / "resources.csv")
        << "key,kg_per_unit,cargo_form\nclay,1000,bulk\nlog,200,long\n";
    std::ofstream(root / "labor.csv") << "key,value\nlog_pair_max_leg_m,150\n";
    std::string error;
    const auto tables = core::LoadTableSet(root.string(), &error);
    const auto strict =
        tables != nullptr ? core::CreateLaborSystem(*tables, core::StubTables::kAllowed) : nullptr;
    if (Expect(strict != nullptr, "the 150 m tables build a labor system") != 0) {
      return failures + 1;
    }
    core::WorldState world = Village(4, 0);
    const core::UnitId works = AddWorks(world, 200.0F, 0, 2000);
    RunHours(*strict, world, 0);
    failures += Expect(CarriersOf(world, works).walkers == 0,
                       "with log_pair_max_leg_m 150 a pile 200 m off gets no pair");
  }
  return failures;
}
