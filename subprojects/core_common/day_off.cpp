// The one door for «is this day a day off in THIS world» (core_common/day_off.h).

#include "core_common/day_off.h"

#include "core_common/world_state.h"

namespace core {

bool HarvestStands(const WorldState& world) {
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind == LandKind::kArable && field.phase == FieldPhase::kHarvest &&
        field.work_days_remaining > 0.0F) {
      return true;
    }
  }
  return false;
}

bool IsDayOffIn(const WorldState& world, SimDay day) {
  if (!IsRestDay(day, world.calendar.day_zero_weekday, world.epoch)) {
    return false;
  }
  // A holiday is never the cancelled day — time §9, «праздники
  // неприкосновенны»: kCancelDayOff skips them — so the one comparison
  // below is the whole rule.
  if (world.chairman.cancelled_day_off != 0 && world.chairman.cancelled_day_off == day) {
    return false;
  }
  // THE HARVEST WITHOUT DAYS OFF (farming design, the harvest rule 1;
  // ChairmanState::harvest_without_days_off): the week's day off of today or
  // later in this calendar year is worked while a ripe crop stands this
  // morning. Never a holiday, by the same paragraph of time §9.
  const SimDay today = world.calendar.day;
  if (world.chairman.harvest_without_days_off != 0 && day >= today &&
      day / kDaysPerYear == today / kDaysPerYear &&
      HolidayOn(day, world.calendar.day_zero_weekday, world.epoch) == Holiday::kNone &&
      HarvestStands(world)) {
    return false;
  }
  return true;
}

}  // namespace core
