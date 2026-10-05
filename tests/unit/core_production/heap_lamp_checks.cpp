// The checks of the heap lamp's days to its loss (heap_lamp_checks.h).

#include "heap_lamp_checks.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
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

/// The heap lamp of the world's one field, painted with the red window 8.
const core::Alarm* HeapLamp(std::vector<core::Alarm>& alarms) {
  core::PaintAlarms(alarms, 8);
  for (const core::Alarm& alarm : alarms) {
    if (alarm.kind == core::AlarmKind::kHarvestWaitingOnField) {
      return &alarm;
    }
  }
  return nullptr;
}

}  // namespace

int CheckTheHeapLampIsRedOnlyWhenItSpoils() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "heap lamp: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  core::WorldState world;
  world.calendar.tick = 30U * core::kTicksPerDay;  // August
  core::RefreshCalendarCaches(world.calendar);
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = 5.0F;
  field.reaped_grams = 2'000'000;
  field.reaped_resource = config.hay_resource;
  const core::FieldId heap = core::AppendRow(world.fields, field);
  core::LogisticsTaskRow task;
  task.load_kind = core::LogisticsLoadKind::kFieldHeap;
  task.field = heap;
  task.level = core::LogisticsLevel::kTerm;
  core::AppendRow(world.logistics_tasks, task);

  // THE ORDINARY COURSE: two tonnes waiting, the task at its own level — a
  // yellow lamp, no loss in sight. 0.37.197 painted it red.
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    const core::Alarm* lamp = HeapLamp(alarms);
    failures += Expect(lamp != nullptr && lamp->colour == core::AlarmColour::kYellow,
                       "heap lamp: a heap waiting for the carts is yellow, not red");
  }
  // THE DAY IT SPOILS: the groom has raised its task to level 0 — red.
  world.logistics_tasks.rows[0].level = core::LogisticsLevel::kUrgent;
  {
    std::vector<core::Alarm> alarms;
    core::CollectFieldAlarms(config, world, alarms);
    const core::Alarm* lamp = HeapLamp(alarms);
    failures += Expect(
        lamp != nullptr && lamp->colour == core::AlarmColour::kRed && lamp->days_to_loss == 0,
        "heap lamp: a heap whose task is at level 0 is red, its loss today");
  }
  return failures;
}
