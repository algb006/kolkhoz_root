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

bool IsCalendarDayOffIn(const WorldState& world, SimDay day) {
  if (!IsRestDay(day, world.calendar.day_zero_weekday, world.epoch)) {
    return false;
  }
  // A holiday is never the cancelled day — time §9, «праздники
  // неприкосновенны»: kCancelDayOff skips them — so the one comparison
  // below is the whole rule.
  return world.chairman.cancelled_day_off == 0 || world.chairman.cancelled_day_off != day;
}

bool IsDayOffIn(const WorldState& world, SimDay day) {
  if (!IsCalendarDayOffIn(world, day)) {
    return false;
  }
  // THE HARVEST WITHOUT DAYS OFF (farming design, the harvest rule 1;
  // ChairmanState::harvest_without_days_off): the week's day off of today or
  // later in this calendar year is worked while a ripe crop stands this
  // morning. Never a holiday, by the same paragraph of time §9.
  //
  // WorldState::gather_short_said IS NOT ASKED HERE — THE WORD HAS NO READER
  // YET (0.37.91; day_off.h). The rule's condition is to be «the count said
  // not in time», and the delivery of the harvest rule 5 puts it here; on
  // the lamp's count as it stands the word comes too late in year 1 and
  // every summer day of year 2.
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
