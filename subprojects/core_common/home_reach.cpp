#include "core_common/home_reach.h"

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace core {

float NearestHomeTravelHours(const WorldState& world,
                             Vec2 place,
                             float speed_kmh,
                             TravelMode mode) {
  if (!(speed_kmh > 0.0F)) {
    return -1.0F;
  }
  // The labour model's own chronometer (labor_day.cpp, HoursPerKm): real
  // km/h divided by the clock's scale, so one number means one road for the
  // assignment and for the alarms.
  const float hours_per_km = static_cast<float>(kClockScale) / speed_kmh;
  float best = -1.0F;
  for (const UnitRow& unit : world.units.rows) {
    // A LIVED-IN house: the brigade sets out from where people sleep, and an
    // empty house far out is nobody's road.
    if (unit.level == 0 || unit.household.value == kInvalidEntityIdValue) {
      continue;
    }
    // BY THE WAY THERE IS, not the crow's line (roads design §11-§13;
    // 0.36.2).
    const float hours = RoadKm(world, mode, unit.position, place) * hours_per_km;
    best = best < 0.0F || hours < best ? hours : best;
  }
  return best;
}

}  // namespace core
