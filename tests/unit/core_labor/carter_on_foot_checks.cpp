// The checks of the carter on foot (carter_on_foot_checks.h).

#include "carter_on_foot_checks.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "core_common/calendar.h"
#include "core_common/haul.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "core_common/work_seam.h"
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
/// `horses` grown kolkhoz horses (row 0 of the livestock table below).
core::WorldState Village(std::uint32_t adults, std::uint16_t horses) {
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
  if (horses > 0) {
    core::HerdRow team;
    team.kind = core::LivestockKindId{0};
    team.adult_count = horses;
    core::AppendRow(world.herds, team);
  }
  return world;
}

/// A field 500 m out with a heap lying on it and `days` of hauling written.
core::FieldId AddHeap(core::WorldState& world, float days, float metres_out = 500.0F) {
  core::FieldRow field;
  field.center = core::Vec2{.x = metres_out, .y = 0.0F};
  field.area_ga = 10.0F;
  field.phase = core::FieldPhase::kIdle;
  field.reaped_grams = 10'000'000;
  field.reaped_resource = core::ResourceId{0};
  field.haul_days_remaining = days;
  field.haul_days_written = days;
  return core::AppendRow(world.fields, field);
}

/// A zone to plant 300 m out: windowless work for every free hand.
void AddPlanting(core::WorldState& world) {
  core::TimberStandRow zone;
  zone.kind = core::TimberStandKind::kPlanted;
  zone.position = core::Vec2{.x = 0.0F, .y = 300.0F};
  zone.planted_area_ha = 5.0F;
  zone.work_days_remaining = 20.0F;
  core::AppendRow(world.stands, zone);
}

/// Logs lying felled 300 m out: twelve minutes on foot.
core::TimberStandId AddLogs(core::WorldState& world) {
  core::TimberStandRow stand;
  stand.position = core::Vec2{.x = 0.0F, .y = -300.0F};
  stand.load_grams = 10'000'000;
  stand.haul_days_remaining = 5.0F;
  stand.haul_days_written = 5.0F;
  return core::AppendRow(world.stands, stand);
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
      std::filesystem::temp_directory_path() / "unit_core_labor_carter_on_foot";
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

struct Crew {
  std::uint32_t riders = 0;
  std::uint32_t walkers = 0;
};

Crew CartersOfField(const core::WorldState& world, core::FieldId field) {
  Crew crew;
  for (const core::ResidentRow& person : world.residents.rows) {
    if (person.work.kind == core::WorkKind::kHauling && person.work.field.value == field.value) {
      ++(person.work.rides_horse != 0 ? crew.riders : crew.walkers);
    }
  }
  return crew;
}

Crew CartersOfStand(const core::WorldState& world, core::TimberStandId stand) {
  Crew crew;
  for (const core::ResidentRow& person : world.residents.rows) {
    if (person.work.kind == core::WorkKind::kHauling && person.work.stand.value == stand.value) {
      ++(person.work.rides_horse != 0 ? crew.riders : crew.walkers);
    }
  }
  return crew;
}

std::uint32_t Planters(const core::WorldState& world) {
  std::uint32_t hands = 0;
  for (const core::ResidentRow& person : world.residents.rows) {
    hands += person.work.kind == core::WorkKind::kPlanting ? 1U : 0U;
  }
  return hands;
}

/// Hauling norm-days drained off the heap in one whole day.
float DrainedInADay(core::ILaborSystem& labor, std::uint32_t adults, std::uint16_t horses) {
  core::WorldState world = Village(adults, horses);
  const core::FieldId heap = AddHeap(world, 50.0F);  // more than any crew here drains in a day
  RunHours(labor, world, core::kTicksPerDay - 2U);
  return 50.0F - world.fields.rows[core::FindRow(world.fields, heap)].haul_days_remaining;
}

}  // namespace

int CheckCarterOnFoot() {
  int failures = 0;
  // The share itself: 20 kg against 750, 2.4 game hours a kilometre on foot
  // against 1.0 in harness.
  const float share = core::WalkerShareOfCartDay(20'000, 750'000, 2.4F, 1.0F);
  failures += Expect(std::fabs(share - (20.0F / 750.0F / 2.4F)) < 1.0e-6F,
                     "carter on foot: a walker's day is the carry over the cart's load, by the "
                     "paces — a ninetieth of a cart's");
  failures += Expect(core::WalkerShareOfCartDay(0, 750'000, 2.4F, 1.0F) == 1.0F &&
                         core::WalkerShareOfCartDay(20'000, 750'000, 0.0F, 1.0F) == 1.0F,
                     "carter on foot: a number missing changes nothing — the share is a whole day");

  const auto labor = Labor();
  if (Expect(labor != nullptr, "carter on foot: the tables build a labor system") != 0) {
    return failures + 1;
  }

  // THE QUEUE GIVES THE HEAP ITS HORSES AND NOBODY ON FOOT: one horse, four
  // hands, a heap and a zone to plant. Until 0.37.105 the heap took the
  // rider and then every hand on foot, «until the expected output covered
  // the work» — and at a cart's price for a walker's day it looked covered.
  {
    core::WorldState world = Village(4, 1);
    const core::FieldId heap = AddHeap(world, 5.0F);
    AddPlanting(world);
    RunHours(*labor, world, 0);
    const Crew crew = CartersOfField(world, heap);
    std::cout << "  carter on foot, the queue: riders " << crew.riders << ", walkers "
              << crew.walkers << ", planters " << Planters(world) << '\n';
    failures += Expect(crew.riders == 1, "carter on foot, the queue: the one horse carts the heap");
    failures += Expect(crew.walkers == 0,
                       "carter on foot, the queue: nobody is sent to the heap on foot while other "
                       "work stands open");
    failures += Expect(Planters(world) == 3,
                       "carter on foot, the queue: the three hands with no horse plant the zone");
  }

  // THE HANDS LEFT WITH NO WORK CARRY, LAST — and write off their own carry.
  // The pair: one adult with the horse drains a rider's day; four adults
  // with the one horse drain hardly more, the three walkers a ninetieth each.
  {
    core::WorldState world = Village(4, 1);
    const core::FieldId heap = AddHeap(world, 5.0F);
    RunHours(*labor, world, 0);
    const Crew crew = CartersOfField(world, heap);
    failures += Expect(crew.riders == 1 && crew.walkers == 3,
                       "carter on foot, the idle: with nothing else to do the three carry on "
                       "their backs");
    const float rider_alone = DrainedInADay(*labor, 1, 1);
    const float with_walkers = DrainedInADay(*labor, 4, 1);
    std::cout << "  carter on foot, the idle: a rider alone drains " << rider_alone
              << " cart-days, with three walkers " << with_walkers << '\n';
    failures += Expect(rider_alone > 0.5F,
                       "carter on foot, the idle: a rider drains the better part of a cart-day");
    failures += Expect(with_walkers > rider_alone && with_walkers < rider_alone + 0.1F,
                       "carter on foot, the idle: three walkers add their own carry and no more — "
                       "under a tenth of a cart-day between them");
  }

  // THE WALKERS ARE THE LAST OF THE QUEUE AT THE TOP-UP TOO: a reaping
  // production opens after the morning takes them off the heap at hour 1,
  // and the rider keeps his horse and his heap. The pair: with nothing
  // opened they carry on.
  {
    const auto after_hour_1 = [&labor](bool a_reaping_opens, Crew& heap_crew) {
      core::WorldState world = Village(4, 1);
      const core::FieldId heap = AddHeap(world, 5.0F);
      core::FieldRow late;
      late.center = core::Vec2{.x = 0.0F, .y = 20.0F};
      late.area_ga = 10.0F;
      late.phase = core::FieldPhase::kGrowing;
      const core::FieldId late_id = core::AppendRow(world.fields, late);
      RunHours(*labor, world, 0);
      if (a_reaping_opens) {
        core::FieldRow& opened = world.fields.rows[core::FindRow(world.fields, late_id)];
        opened.phase = core::FieldPhase::kHarvest;
        opened.work_days_remaining = 20.0F;
      }
      world.calendar.tick = (static_cast<core::Tick>(kWorkingDay) * core::kTicksPerDay) + 1U;
      core::RefreshCalendarCaches(world.calendar);
      const core::WorldState previous = world;
      labor->RunAssignmentDecisions(previous, world);
      heap_crew = CartersOfField(world, heap);
      std::uint32_t reapers = 0;
      for (const core::ResidentRow& person : world.residents.rows) {
        reapers +=
            person.work.kind == core::WorkKind::kHarvest && person.work.field.value == late_id.value
                ? 1U
                : 0U;
      }
      return reapers;
    };
    Crew opened_crew;
    Crew quiet_crew;
    const std::uint32_t reapers = after_hour_1(true, opened_crew);
    const std::uint32_t nobody = after_hour_1(false, quiet_crew);
    std::cout << "  carter on foot, the top-up: a reaping opened — reapers " << reapers
              << ", heap riders " << opened_crew.riders << ", walkers " << opened_crew.walkers
              << "; none opened — walkers " << quiet_crew.walkers << '\n';
    failures += Expect(reapers == 3 && opened_crew.walkers == 0,
                       "carter on foot, the top-up: a reaping opened after the morning takes the "
                       "three walkers off the heap");
    failures += Expect(opened_crew.riders == 1,
                       "carter on foot, the top-up: and the rider keeps his horse and his heap");
    failures += Expect(nobody == 0 && quiet_crew.riders == 1 && quiet_crew.walkers == 3,
                       "carter on foot, the top-up: with nothing opened the walkers carry on");
  }

  // THE HARNESS COUNTS A WALKER ONLY AS FAR AS THE LOAD WANTED A CART
  // (work_seam.h, HarnessCount::harnessed): «too few horses» and the
  // mechanisation share read that count, and the idle who carry must not
  // read as cart-days pulled by hand. A heap of 2.5 cart-days with one rider
  // wanted two carts more: of five walkers two count. A heap its rider
  // covers wanted none: of three walkers none counts.
  {
    const auto harnessed = [](float seam_days, std::uint32_t walkers) {
      core::WorldState world = Village(1 + walkers, 1);
      const core::FieldId heap = AddHeap(world, seam_days);
      for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
        core::WorkAssignment& work = world.residents.rows[row].work;
        work.kind = core::WorkKind::kHauling;
        work.field = heap;
        work.rides_horse = row == 0 ? 1U : 0U;
      }
      return core::CountHarness(world);
    };
    const core::HarnessCount wanted_two = harnessed(2.5F, 5);
    const core::HarnessCount wanted_none = harnessed(0.5F, 3);
    failures += Expect(wanted_two.in_traces == 1 && wanted_two.harnessed == 3,
                       "carter on foot, the harness: a heap of 2.5 cart-days with one rider counts "
                       "two of its five walkers — the carts it wanted");
    failures += Expect(wanted_none.in_traces == 1 && wanted_none.harnessed == 1,
                       "carter on foot, the harness: a heap its rider covers counts none of its "
                       "three walkers");
  }

  // A WALKER GOES ONLY WHERE A DAY GIVES TWO TRIPS (0.37.109; labor.csv
  // walker_min_trips_per_day): with twelve hours of light a heap a kilometre
  // out is 2.4 hours one way and two and a half trips — he goes; a heap a
  // kilometre and a half out is 3.6 hours and one trip and two thirds — he
  // stays home, where until 0.37.109 the road rule's six hours let him walk.
  // AND HE IS PAID BY WHAT HE CARRIED (walker_norm_kg_per_day): two and a
  // half trips of 20 kg against a norm of 80 are 0.625 of a trudoden's
  // norm-day, where his own hours gave 0.72.
  //
  // THE TRIPS ARE COUNTED IN THE STANDARD DAY, NOT IN THE LIGHT (0.37.113;
  // boss, 2 October 2026): in June's sixteen hours the heap a kilometre and
  // a half out gave 2.2 trips «in the light» and he was sent — the pits three
  // hours out stayed walked all summer. Ten hours hold 1.4 trips: not sent.
  // AND WHAT HE CARRIED CARRIES HIS OWN OUTPUT (econ, the same day): a
  // walker in poor health carries less in the same trips, as his norm-day
  // always was cut — «по принесённому» had dropped the man out of the sum.
  {
    const auto walker_day = [&labor](float metres_out,
                                     std::uint32_t& walkers,
                                     float light_hours = 12.0F,
                                     float health = 70.0F) {
      core::WorldState world = Village(2, 1);
      world.weather.daylight_hours = light_hours;
      for (core::ResidentRow& person : world.residents.rows) {
        person.health = health;
      }
      const core::FieldId heap = AddHeap(world, 50.0F, metres_out);
      RunHours(*labor, world, 0);
      walkers = CartersOfField(world, heap).walkers;
      RunHours(*labor, world, core::kTicksPerDay - 3U);  // to hour 21: worked, not yet paid off
      float paid = 0.0F;
      for (const core::ResidentRow& person : world.residents.rows) {
        if (person.work.kind == core::WorkKind::kHauling && person.work.rides_horse == 0) {
          paid = person.work.worked_norm_days_today;
        }
      }
      return paid;
    };
    std::uint32_t near_walkers = 0;
    std::uint32_t far_walkers = 0;
    const float near_paid = walker_day(1000.0F, near_walkers);
    walker_day(1500.0F, far_walkers);
    std::cout << "  carter on foot, two trips: a heap 1 km out — walkers " << near_walkers
              << ", his day's norm-days " << near_paid << "; 1.5 km out — walkers " << far_walkers
              << '\n';
    failures += Expect(near_walkers == 1,
                       "carter on foot, two trips: a kilometre out the day gives two and a half "
                       "trips, and he carries");
    failures += Expect(far_walkers == 0,
                       "carter on foot, two trips: a kilometre and a half out the day gives under "
                       "two trips, and he is not sent");
    failures += Expect(near_paid > 0.60F && near_paid < 0.65F,
                       "carter on foot, the pay: two and a half trips of 20 kg against a norm of "
                       "80 are 0.625 of a norm-day");
    std::uint32_t june_walkers = 0;
    walker_day(1500.0F, june_walkers, 16.0F);
    failures += Expect(june_walkers == 0,
                       "carter on foot, two trips: June's sixteen hours of light do not send him a "
                       "kilometre and a half out — the trips are counted in the standard day");
    // AND THE OTHER SIDE: a winter day shorter than the standard one holds
    // the trips its light holds — eight hours, a kilometre out, 1.67 trips.
    std::uint32_t winter_walkers = 0;
    walker_day(1000.0F, winter_walkers, 8.0F);
    failures += Expect(winter_walkers == 0,
                       "carter on foot, two trips: a winter day of eight hours does not send him a "
                       "kilometre out — never more trips than the light holds");
    std::uint32_t weak_walkers = 0;
    const float weak_paid = walker_day(1000.0F, weak_walkers, 12.0F, 30.0F);
    std::cout << "  carter on foot, the pay: in good health " << near_paid << ", in poor health "
              << weak_paid << '\n';
    failures += Expect(weak_walkers == 1 && weak_paid > 0.0F && weak_paid < near_paid - 0.03F,
                       "carter on foot, the pay: a walker in poor health carries less in the same "
                       "trips and is paid less");
  }

  // A LOG IS NEVER CARRIED ON A BACK: with the one horse out, nobody walks to
  // the stand; with no horse at all nobody carts it.
  {
    core::WorldState world = Village(3, 1);
    const core::TimberStandId stand = AddLogs(world);
    RunHours(*labor, world, 0);
    const Crew crew = CartersOfStand(world, stand);
    failures += Expect(crew.riders == 1 && crew.walkers == 0,
                       "carter on foot, the logs: the horse carts them and nobody carries a log "
                       "on foot");
    core::WorldState horseless = Village(3, 0);
    const core::TimberStandId lying = AddLogs(horseless);
    RunHours(*labor, horseless, 0);
    const Crew none = CartersOfStand(horseless, lying);
    failures += Expect(none.riders == 0 && none.walkers == 0,
                       "carter on foot, the logs: a settlement with no horse moves no log");
  }

  // THE SETTLEMENT WITH NO HORSE CARRIES AS IT ALWAYS DID: its seam is
  // written in a walker's days, and two walkers drain two days of it.
  {
    core::WorldState world = Village(2, 0);
    const core::FieldId heap = AddHeap(world, 5.0F);
    RunHours(*labor, world, 0);
    const Crew crew = CartersOfField(world, heap);
    const float two_walkers = DrainedInADay(*labor, 2, 0);
    std::cout << "  carter on foot, no horse: two walkers drain " << two_walkers
              << " walker-days\n";
    failures += Expect(crew.riders == 0 && crew.walkers == 2,
                       "carter on foot, no horse: both hands carry the heap in the queue's own "
                       "place");
    failures += Expect(two_walkers > 1.0F,
                       "carter on foot, no horse: and their days are whole days of a seam written "
                       "for walkers");
  }
  return failures;
}
