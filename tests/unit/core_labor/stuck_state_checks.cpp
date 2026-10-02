// The checks of labour's states that stood for ever (stuck_state_checks.h).

#include "stuck_state_checks.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
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

/// One household of `adults` at the map's origin, twelve hours of light, no
/// horse: the settlement carries on backs, as it always did with none.
core::WorldState Village(std::uint32_t adults) {
  core::WorldState world;
  world.calendar.day_zero_weekday = core::Weekday::kMonday;
  world.weather.daylight_hours = 12.0F;
  world.chairman.harvest_without_days_off = 0;
  core::UnitRow house;
  house.position = core::Vec2{.x = 0.0F, .y = 0.0F};
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
  return world;
}

/// A field 300 m out in `phase` with `days` of its work left.
core::FieldId AddField(core::WorldState& world, core::FieldPhase phase, float days) {
  core::FieldRow field;
  field.center = core::Vec2{.x = 300.0F, .y = 0.0F};
  field.area_ga = 10.0F;
  field.phase = phase;
  field.work_days_remaining = days;
  return core::AppendRow(world.fields, field);
}

/// A heap 400 m the other way with five days of carting written.
core::FieldId AddHeap(core::WorldState& world) {
  core::FieldRow field;
  field.center = core::Vec2{.x = -400.0F, .y = 0.0F};
  field.area_ga = 10.0F;
  field.phase = core::FieldPhase::kIdle;
  field.reaped_grams = 10'000'000;
  field.reaped_resource = core::ResourceId{0};
  field.haul_days_remaining = 5.0F;
  field.haul_days_written = 5.0F;
  return core::AppendRow(world.fields, field);
}

/// The chairman's standing order, as the book holds it once accepted.
void OrderToField(core::WorldState& world, core::WorkKind work, core::FieldId field) {
  core::OrderRow order;
  order.kind = core::OrderKind::kAssignWork;
  order.status = core::OrderStatus::kAccepted;
  order.resident = world.residents.row_ids[0];
  order.work = work;
  order.field = field;
  core::AppendRow(world.orders, order);
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
      std::filesystem::temp_directory_path() / "unit_core_labor_stuck_states";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  std::ofstream(root / "livestock.csv") << "key,care_days_per_year\nhorse,0\n";
  std::ofstream(root / "unit_types.csv") << "key\nhorse_yard\n";
  std::ofstream(root / "professions.csv")
      << "key,min_education,min_age,max_age,gender,single_post\n"
         "groom,any,16,,any,0\n";
  std::ofstream(root / "unit_staff.csv") << "unit,profession,level,slots\nhorse_yard,groom,,1\n";
  std::string error;
  const auto tables = core::LoadTableSet(root.string(), &error);
  if (tables == nullptr) {
    std::cout << error << '\n';
    return nullptr;
  }
  return core::CreateLaborSystem(*tables, core::StubTables::kAllowed);
}

std::uint32_t Count(const core::WorldState& world, core::EventKind kind) {
  std::uint32_t count = 0;
  for (const core::SimEvent& event : world.step_events) {
    count += event.kind == kind ? 1U : 0U;
  }
  return count;
}

/// A STANDING ORDER ON A TARGET WITH NO WORK (work_orders.h,
/// ApplyStandingWork): the field exists and is in no phase of the ordered
/// work — until 0.37.115 the man was pinned to it every morning, for as long
/// as the order stood, and did nothing.
int CheckTheStandingOrderWithNoWork(core::ILaborSystem& labor) {
  int failures = 0;
  {
    core::WorldState world = Village(1);
    const core::FieldId idle = AddField(world, core::FieldPhase::kIdle, 0.0F);
    const core::FieldId heap = AddHeap(world);
    OrderToField(world, core::WorkKind::kSowing, idle);
    RunHours(labor, world, 0);
    const core::WorkAssignment& work = world.residents.rows[0].work;
    std::cout << "  stuck states, the standing order: ordered to sow a field with no sowing — his "
              << "work kind " << static_cast<int>(work.kind) << " on field " << work.field.value
              << " (the heap is " << heap.value << ", the ordered field " << idle.value << ")\n";
    failures += Expect(!(work.kind == core::WorkKind::kSowing && work.field.value == idle.value),
                       "stuck states, the standing order: with no sowing open on the ordered "
                       "field the man is not pinned to it");
    failures += Expect(work.kind == core::WorkKind::kHauling && work.field.value == heap.value,
                       "stuck states, the standing order: he is left where the accountant put "
                       "him for the day — on the heap");
    failures += Expect(world.orders.rows[0].status == core::OrderStatus::kAccepted,
                       "stuck states, the standing order: the order still stands for the day the "
                       "work opens");
  }
  // THE ALLOWED SIDE: the same order with the sowing open takes him, heap or
  // no heap.
  {
    core::WorldState world = Village(1);
    const core::FieldId sowing = AddField(world, core::FieldPhase::kSowing, 3.0F);
    AddHeap(world);
    OrderToField(world, core::WorkKind::kSowing, sowing);
    RunHours(labor, world, 0);
    const core::WorkAssignment& work = world.residents.rows[0].work;
    failures += Expect(work.kind == core::WorkKind::kSowing && work.field.value == sowing.value,
                       "stuck states, the standing order: with the sowing open the order takes "
                       "its man");
  }
  return failures;
}

/// A POST WHOSE UNIT IS GONE (labor_system.h, the day's close): until
/// 0.37.115 only kDismiss cleared a post, and the holder of a day post at a
/// unit taken down stayed out of the accountant's candidates for good.
int CheckThePostWithNoUnit(core::ILaborSystem& labor) {
  int failures = 0;
  const auto with_post = [](core::UnitId unit) {
    core::WorldState world = Village(1);
    world.residents.rows[0].post.profession = core::ProfessionId{0};
    world.residents.rows[0].post.unit = unit;
    return world;
  };
  {
    core::WorldState world = with_post(core::UnitId{999});  // no such row
    RunHours(labor, world, core::kTicksPerDay - 1U);
    const core::ResidentRow& holder = world.residents.rows[0];
    failures += Expect(holder.post.profession.value == core::kInvalidDefIdValue &&
                           holder.post.unit.value == core::kInvalidEntityIdValue,
                       "stuck states, the post: a post whose unit is gone is taken off at the "
                       "day's close");
    failures += Expect(Count(world, core::EventKind::kPostVacated) == 1,
                       "stuck states, the post: and it is said once — kPostVacated, the post "
                       "that emptied by itself");
  }
  // THE ALLOWED SIDE: a post at a standing unit is nobody's to take off.
  {
    core::WorldState world = Village(1);
    core::UnitRow yard;
    yard.type = core::UnitTypeId{0};
    yard.level = 1;
    yard.position = core::Vec2{.x = 50.0F, .y = 0.0F};
    const core::UnitId yard_id = core::AppendRow(world.units, yard);
    world.residents.rows[0].post.profession = core::ProfessionId{0};
    world.residents.rows[0].post.unit = yard_id;
    RunHours(labor, world, core::kTicksPerDay - 1U);
    failures += Expect(world.residents.rows[0].post.unit.value == yard_id.value &&
                           Count(world, core::EventKind::kPostVacated) == 0,
                       "stuck states, the post: a post at a standing unit stays, and nothing is "
                       "said");
  }
  return failures;
}

/// A MODULE BEING TAKEN DOWN WITH NO SOUND PARENT (labor_system.h, the site
/// jobs): a module is not BUILT while its parent does not stand sound — and
/// until 0.37.115 it was not taken down either, so the site and its plot
/// stood for ever.
int CheckTheOrphanedModule(core::ILaborSystem& labor) {
  int failures = 0;
  const auto crew_of = [&labor](core::ConstructionPhase phase) {
    core::WorldState world = Village(2);
    core::UnitRow module;
    module.position = core::Vec2{.x = 100.0F, .y = 0.0F};
    module.level = 0;
    module.parent = core::UnitId{999};  // the parent's row is gone
    module.construction.phase = phase;
    module.construction.labor_days_remaining = 4.0F;
    module.construction.labor_days_total = 4.0F;
    module.construction.max_crew = 2;
    const core::UnitId module_id = core::AppendRow(world.units, module);
    RunHours(labor, world, 0);
    std::uint32_t crew = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      crew += person.work.kind == core::WorkKind::kConstruction &&
                      person.work.unit.value == module_id.value
                  ? 1U
                  : 0U;
    }
    return crew;
  };
  const std::uint32_t taking_down = crew_of(core::ConstructionPhase::kDemolishing);
  const std::uint32_t building = crew_of(core::ConstructionPhase::kBuilding);
  std::cout << "  stuck states, the orphaned module: a crew of " << taking_down
            << " takes it down; " << building << " build it\n";
  failures += Expect(taking_down == 2,
                     "stuck states, the orphaned module: a module with no sound parent is "
                     "taken down by a crew");
  failures += Expect(building == 0,
                     "stuck states, the orphaned module: and is still not BUILT — the rule of "
                     "the parent stands for the building");
  return failures;
}

}  // namespace

int CheckStuckStates() {
  const std::unique_ptr<core::ILaborSystem> labor = Labor();
  if (Expect(labor != nullptr, "stuck states: the labour system builds") != 0) {
    return 1;
  }
  int failures = 0;
  failures += CheckTheStandingOrderWithNoWork(*labor);
  failures += CheckThePostWithNoUnit(*labor);
  failures += CheckTheOrphanedModule(*labor);
  return failures;
}
