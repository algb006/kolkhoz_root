/// @file
/// @brief The solar day window: when the sun is up, as the core lays a day
/// out, and which tick holds sunrise and sunset.
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments over a plain aggregate: no state, no
/// synchronization, readable from any phase and any thread.
///
/// It lives in core_common because TWO modules must agree on it and they may
/// not name each other (module law, manual/54-modules.md): core_labor works
/// the daylight window — the workday by the sun, time design §6 — and
/// core_boundary stops a fast-forward at the next sunrise or sunset
/// (core_boundary/session.h). Two copies of "noon plus or minus half the
/// daylight" would eventually differ, and the fast-forward would then stop
/// at an hour the workers do not recognise as morning.
///
/// The daylight itself is weather, written once a day by the time phase
/// (WorldState::weather.daylight_hours), so within a day the window stands
/// still and each day has exactly one sunrise tick and one sunset tick.

#ifndef CORE_COMMON_DAY_WINDOW_H_
#define CORE_COMMON_DAY_WINDOW_H_

#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// @brief Today's daylight, as hours from midnight. The core's clock has no
/// timezone, so the window is centred on noon: what matters is its length
/// and that everyone shares it.
struct DayWindow {
  float sunrise = 6.0F;

  float sunset = 18.0F;
};

/// @brief The window of a day with `daylight_hours` of sun.
constexpr DayWindow SolarWindow(float daylight_hours) {
  const float half = daylight_hours * 0.5F;
  return DayWindow{.sunrise = 12.0F - half, .sunset = 12.0F + half};
}

/// @brief The hour of the day that CONTAINS the moment `hours_from_midnight`.
/// A tick is one hour and covers [h, h + 1), so the answer is the whole part.
/// The answer is CLAMPED into the day, and the clamp is not decoration: a
/// polar-summer daylight of 24 hours puts sunset at 24.0, which belongs to
/// the next day and would leave this day with no sunset tick at all. Hour 23
/// is the last hour the sun is up on such a day, and it is the answer a
/// caller waiting for sunset needs.
constexpr std::uint32_t HourOfDayMoment(float hours_from_midnight) {
  if (!(hours_from_midnight > 0.0F)) {
    return 0;  // written positively so a NaN daylight lands on hour 0
  }
  if (hours_from_midnight >= static_cast<float>(kTicksPerDay)) {
    return kTicksPerDay - 1;
  }
  return static_cast<std::uint32_t>(hours_from_midnight);
}

/// @brief The tick-of-day the sun rises in.
constexpr std::uint32_t SunriseHour(float daylight_hours) {
  return HourOfDayMoment(SolarWindow(daylight_hours).sunrise);
}

/// @brief The tick-of-day the sun sets in.
constexpr std::uint32_t SunsetHour(float daylight_hours) {
  return HourOfDayMoment(SolarWindow(daylight_hours).sunset);
}

}  // namespace core

#endif  // CORE_COMMON_DAY_WINDOW_H_
