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

bool GatherShortStands(const WorldState& world) {
  // The word is its day plus one, and nought is «never said». It stands
  // today when the count said it at yesterday's close — or at today's own:
  // the evening's rest must read the answer the morning's placement read,
  // and a word rewritten at the close would otherwise change the day's kind
  // between them.
  const SimDay today = world.calendar.day;
  return world.gather_short_said != 0 &&
         (world.gather_short_said == today || world.gather_short_said == today + 1U);
}

bool IsDayOffIn(const WorldState& world, SimDay day) {
  if (!IsCalendarDayOffIn(world, day)) {
    return false;
  }
  // THE HARVEST WITHOUT DAYS OFF (farming design, the harvest rule 1;
  // ChairmanState::harvest_without_days_off): the week's day off of today or
  // later in this calendar year is worked while a ripe crop stands this
  // morning AND THE GATHERING COUNT HAS SAID «NOT IN TIME WITH THE DAYS OFF
  // KEPT» (GatherShortStands; 0.37.147 — the rule's counterweight, boss,
  // core-boss-potato-crew-trace-2026-10-01 [27]; host-boss-pin-0-37-133-
  // 2026-10-02 [32], [36]). Never a holiday, by the same paragraph of time §9.
  //
  // UNTIL 0.37.147 «A RIPE CROP STANDS» WAS THE WHOLE CONDITION: true every
  // autumn, it took one or two Sundays a year from a village nobody commands
  // — 56 over nine villages in five years, 44 of them with no word from the
  // count (0.37.142's print) — and on host's sixty villages two walked off
  // their work for it. The word was written since 0.37.91 and not read here:
  // on the count of that day — one pace for every field — it came too late in
  // year 1 and stood all summer in year 2. The count is by fields since this
  // commit (core_common/reaping_pace.h), and the door reads it.
  const SimDay today = world.calendar.day;
  if (world.chairman.harvest_without_days_off != 0 && day >= today &&
      day / kDaysPerYear == today / kDaysPerYear &&
      HolidayOn(day, world.calendar.day_zero_weekday, world.epoch) == Holiday::kNone &&
      HarvestStands(world) && GatherShortStands(world)) {
    return false;
  }
  return true;
}

}  // namespace core
