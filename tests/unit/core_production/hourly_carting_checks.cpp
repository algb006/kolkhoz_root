// The checks of the carted share reaching the store in its hour
// (hourly_carting_checks.h).

#include "hourly_carting_checks.h"

#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "field_haul.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

core::Grams HayIn(const core::WorldState& world, core::ResourceId hay) {
  core::Grams total = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    total += hay.value < unit.stock.size() ? unit.stock[hay.value] : 0;
  }
  return total;
}

}  // namespace

int CheckTheCartingReachesTheStoreInItsHour() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "hourly carting: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  const core::ResourceId hay = config.hay_resource;
  core::WorldState world;
  world.calendar.tick = 25U * core::kTicksPerDay + 10U;  // June, ten o'clock
  core::RefreshCalendarCaches(world.calendar);
  // A hay stack with room, and a tonne of sown-grass hay lying on an arable
  // field — its door is the stores' (meadow_hay_checks.cpp).
  core::UnitRow stack;
  stack.type = core::UnitTypeId{
      static_cast<std::uint16_t>(tables->FindTable("unit_types")->FindRowByKey("hay_stack"))};
  stack.level = 1;
  stack.stock.assign(config.feed_values.size(), 0);
  core::AppendRow(world.units, stack);
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.center = core::Vec2{.x = 800.0F, .y = 0.0F};
  field.area_ga = 5.0F;
  field.reaped_grams = 1'000'000;
  field.reaped_resource = hay;
  field.haul_days_written = 2.0F;
  field.haul_days_remaining = 2.0F;
  core::AppendRow(world.fields, field);

  // AN HOUR NOBODY CARTED: nothing comes in, and the seam is as it was.
  core::DeliverCartedLoads(config, world);
  failures += Expect(HayIn(world, hay) == 0 && world.fields.rows[0].reaped_grams == 1'000'000 &&
                         world.fields.rows[0].haul_days_written == 2.0F,
                     "hourly carting: an hour nobody carted brings nothing to the stores");

  // AN HOUR THAT CARTED A QUARTER of the seam: a quarter of the load is in
  // the stack this hour — not at the day's last tick — and the seam left is
  // measured from here.
  world.fields.rows[0].haul_days_remaining = 1.5F;
  core::DeliverCartedLoads(config, world);
  const core::Grams first = HayIn(world, hay);
  std::cout << "  hourly carting: a quarter of the seam carted — " << first / 1000
            << " kg in the stores at once\n";
  failures += Expect(first >= 249'000 && first <= 251'000 &&
                         world.fields.rows[0].reaped_grams == 1'000'000 - first &&
                         world.fields.rows[0].haul_days_written == 1.5F &&
                         world.fields.rows[0].haul_days_remaining == 1.5F,
                     "hourly carting: a quarter of the seam carted puts a quarter of the load in "
                     "the stores that hour, and the seam is measured from there");

  // AND THE NEXT HOUR counts only its own carting: another half man-day of
  // the 1.5 left is a third of what is left, 250 kg more — not the hour
  // before again.
  world.fields.rows[0].haul_days_remaining = 1.0F;
  core::DeliverCartedLoads(config, world);
  const core::Grams second = HayIn(world, hay) - first;
  failures += Expect(second >= 249'000 && second <= 251'000,
                     "hourly carting: the next hour brings only what it carted itself");

  // A DOOR THAT TAKES NOTHING KEEPS THE HOUR'S WORK (0.37.200's amend): no
  // store in the village — the hour's quarter is carted and nothing goes in,
  // so the drained half man-day stays in the seam, `written` above
  // `remaining`, for the next hour or the evening. The first V1 pair dropped
  // it, and logs at a full pile lost the day's carting.
  core::WorldState closed;
  closed.calendar = world.calendar;
  core::FieldRow heap = field;
  heap.haul_days_written = 2.0F;
  heap.haul_days_remaining = 1.5F;
  core::AppendRow(closed.fields, heap);
  core::DeliverCartedLoads(config, closed);
  failures += Expect(closed.fields.rows[0].reaped_grams == 1'000'000 &&
                         closed.fields.rows[0].haul_days_written == 2.0F &&
                         closed.fields.rows[0].haul_days_remaining == 1.5F,
                     "hourly carting: an hour whose door takes nothing keeps its carting in the "
                     "seam for the next hour or the evening");
  return failures;
}
