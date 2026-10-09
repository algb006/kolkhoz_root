// The checks of the seed lamp's two false alarms (seed_lamp_checks.h).

#include "seed_lamp_checks.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "production_alarms.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// The seed alarm of `field`, or nullptr; the list is painted with the red
/// window 8, as the world paints it.
const core::Alarm* SeedAlarm(std::vector<core::Alarm>& alarms, core::FieldId field) {
  core::PaintAlarms(alarms, 8);
  for (const core::Alarm& alarm : alarms) {
    if (alarm.kind == core::AlarmKind::kSeedShort && alarm.field == field) {
      return &alarm;
    }
  }
  return nullptr;
}

/// The first crop of `resource` that is, or is not, a winter crop.
core::CropId CropOf(const core::ProductionConfig& config, core::ResourceId resource, bool winter) {
  for (std::size_t index = 0; index < config.crops.size(); ++index) {
    if (config.crops[index].resource == resource &&
        (config.crops[index].is_winter != 0) == winter &&
        config.crops[index].sowing_norm_kg_per_ha > 0.0F) {
      return core::DefIdFromIndex<core::CropIdTag>(index);
    }
  }
  return core::CropId{};
}

void SetDay(core::WorldState& world, std::uint32_t day) {
  world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
}

/// A standing store holding `grams` of `resource` and nothing else.
void PutInTheBarn(core::WorldState& world, core::ResourceId resource, core::Grams grams) {
  core::UnitRow barn;
  barn.level = 1;
  barn.stock.assign(static_cast<std::size_t>(resource.value) + 1U, 0);
  barn.stock[resource.value] = grams;
  world.units.rows.clear();
  world.units.row_ids.clear();
  core::AppendRow(world.units, barn);
}

/// THE CLOSING DAY (cure 1): barley's window is days 12-19 of the year. A
/// field under the harrow for barley on day 20 is a sowing begun: its crew
/// finishes it past the window.
int CheckTheClosingDay(const core::ProductionConfig& config, core::ResourceId barley) {
  int failures = 0;
  const core::CropId crop = CropOf(config, barley, false);
  if (Expect(crop.value != core::kInvalidDefIdValue,
             "seed lamp: the tables name a spring barley") != 0) {
    return 1;
  }
  core::WorldState world;
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 10.0F;
  field.phase = core::FieldPhase::kHarrowing;
  field.crop = crop;
  field.rotation_year0 = crop;
  const core::FieldId id = core::AppendRow(world.fields, field);
  const core::Grams bare =
      core::GramsFromKilograms(config.crops[crop.value].sowing_norm_kg_per_ha * field.area_ga);

  // The seed in the barn at 2 % over the bare need, the day the window shuts:
  // not short. Until 0.37.204 the rot margin wrapped to next year's window on
  // this day and the same barn read some 8 % short — a red lamp over a field
  // sown two days later.
  SetDay(world, 20);
  PutInTheBarn(world, barley, bare + (bare / 50));
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    failures += Expect(SeedAlarm(alarms, id) == nullptr,
                       "seed lamp: a sowing begun, its seed in the barn, is not short on the "
                       "day its window shuts");
  }
  // Half the seed, the window still open (day 14): the lamp, red.
  SetDay(world, 14);
  PutInTheBarn(world, barley, bare / 2);
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    const core::Alarm* alarm = SeedAlarm(alarms, id);
    failures +=
        Expect(alarm != nullptr && alarm->lamp == 1 && alarm->colour == core::AlarmColour::kRed,
               "seed lamp: half the seed inside the open window lights the lamp red");
  }
  // The same half on the day the window has shut: the line stays, the lamp
  // does not light — there is no day left to answer it by.
  SetDay(world, 20);
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    const core::Alarm* alarm = SeedAlarm(alarms, id);
    failures += Expect(alarm != nullptr && alarm->lamp == 0,
                       "seed lamp: once the window has shut the shortfall is a line, not a lamp");
  }
  return failures;
}

/// THE REAPED HEAP (cure 2): a field that sows winter rye this autumn, two
/// days before the window (day 26; the window opens on day 28), the barn
/// empty of rye.
int CheckTheReapedHeap(const core::ProductionConfig& config, core::ResourceId rye) {
  int failures = 0;
  const core::CropId crop = CropOf(config, rye, true);
  if (Expect(crop.value != core::kInvalidDefIdValue, "seed lamp: the tables name a winter rye") !=
      0) {
    return 1;
  }
  core::WorldState world;
  SetDay(world, 26);
  core::FieldRow sowing;
  sowing.kind = core::LandKind::kArable;
  sowing.area_ga = 10.0F;
  sowing.rotation_year1 = crop;  // a winter crop of slot 1 is sown this autumn
  const core::FieldId id = core::AppendRow(world.fields, sowing);
  const core::Grams bare =
      core::GramsFromKilograms(config.crops[crop.value].sowing_norm_kg_per_ha * sowing.area_ga);
  PutInTheBarn(world, rye, 0);
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    const core::Alarm* alarm = SeedAlarm(alarms, id);
    failures += Expect(alarm != nullptr && alarm->lamp == 1,
                       "seed lamp: no rye in the barn and none on the fields lights the lamp");
  }
  // The neighbour's rye was reaped yesterday and lies in its heap, twice the
  // need: the harvest has given the seed. Until 0.37.204 the lamp read the
  // stores alone and turned red beside it.
  core::FieldRow reaped;
  reaped.kind = core::LandKind::kArable;
  reaped.area_ga = 10.0F;
  reaped.reaped_grams = 2 * bare;
  reaped.reaped_resource = rye;
  const core::FieldId heap = core::AppendRow(world.fields, reaped);
  core::LogisticsTaskRow task;
  task.load_kind = core::LogisticsLoadKind::kFieldHeap;
  task.field = heap;
  task.level = core::LogisticsLevel::kTerm;
  core::AppendRow(world.logistics_tasks, task);
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    failures += Expect(SeedAlarm(alarms, id) == nullptr,
                       "seed lamp: the rye lying reaped in a neighbour's heap is seed held — "
                       "no shortfall");
  }
  // The plan is owed the whole heap: it is the district's, not the seed's.
  world.plan.announced = 1;
  world.plan.due.assign(static_cast<std::size_t>(rye.value) + 1U, 0);
  world.plan.due[rye.value] = 2 * bare;
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    const core::Alarm* alarm = SeedAlarm(alarms, id);
    failures += Expect(alarm != nullptr && alarm->lamp == 1,
                       "seed lamp: a heap the plan is owed whole does not count as seed");
  }
  world.plan.announced = 0;
  world.plan.due.clear();
  // A heap is not a barn: one that spoils by tomorrow (its carting task at
  // level 0) does not silence the lamp.
  world.logistics_tasks.rows[0].level = core::LogisticsLevel::kUrgent;
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    const core::Alarm* alarm = SeedAlarm(alarms, id);
    failures += Expect(alarm != nullptr && alarm->lamp == 1,
                       "seed lamp: a heap that spoils by tomorrow does not count as seed");
  }
  return failures;
}

}  // namespace

int CheckTheSeedLampIsNotAFalseAlarm() {
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "seed lamp: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ITable* const resources = tables->FindTable("resources");
  if (Expect(resources != nullptr, "seed lamp: the shipped tables hold resources") != 0) {
    return 1;
  }
  const std::uint32_t barley_row = resources->FindRowByKey("barley");
  const std::uint32_t rye_row = resources->FindRowByKey("rye");
  if (Expect(barley_row != core::kNoTableRow && rye_row != core::kNoTableRow,
             "seed lamp: the roster names barley and rye") != 0) {
    return 1;
  }
  int failures = 0;
  failures += CheckTheClosingDay(config, core::DefIdFromRow<core::ResourceIdTag>(barley_row));
  failures += CheckTheReapedHeap(config, core::DefIdFromRow<core::ResourceIdTag>(rye_row));
  return failures;
}
