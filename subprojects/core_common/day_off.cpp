// The one door for «is this day a day off in THIS world» (core_common/day_off.h).

#include "core_common/day_off.h"

#include "core_common/world_state.h"

namespace core {

bool IsDayOffIn(const WorldState& world, SimDay day) {
  if (!IsRestDay(day, world.calendar.day_zero_weekday, world.epoch)) {
    return false;
  }
  // A holiday is never the cancelled day — time §9, «праздники
  // неприкосновенны»: kCancelDayOff skips them — so the one comparison
  // below is the whole rule.
  return world.chairman.cancelled_day_off == 0 || world.chairman.cancelled_day_off != day;
}

}  // namespace core
