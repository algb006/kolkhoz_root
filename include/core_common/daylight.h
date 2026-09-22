/// @file
/// @brief The solar curve of the year: how much daylight a given day of the
/// year has, and therefore when the sun rises and sets on it.
/// @threading PARALLEL_READONLY
/// Pure constexpr functions over a constant table: no state, no world, no
/// simulation. Callable from any phase, any thread, and from outside the
/// simulation entirely.
///
/// WHY IT IS PUBLIC AND WHY IT IS HERE (boss, thread
/// boss-core-sunrise-for-month-2026-09-23). The presentation's viewing mode
/// stages a scene for a chosen month and needs sunrise and sunset as
/// numbers. Until this header the only way to those numbers was to RUN the
/// simulation forward until the day arrived — a question that moved the
/// world in order to be answered. The curve now answers without touching
/// anything.
///
/// The table used to live inside core_time, where one module wrote the
/// weather of a day. It is here for the same reason day_window.h is: the
/// moment a second party needs the value, a private copy becomes a second
/// home, and two homes for one number diverge silently on the day somebody
/// corrects one of them.
///
/// ONE HOME, BY CONSTRUCTION. core_time's time phase writes
/// WorldState::weather.daylight_hours out of this very table, so an answer
/// taken from here and an answer taken by advancing the clock to that day
/// cannot differ — not because they were compared, but because they are the
/// same numbers passing through the same function. The comparison is made
/// anyway, over all 48 days, in tests/unit/core_time.

#ifndef CORE_COMMON_DAYLIGHT_H_
#define CORE_COMMON_DAYLIGHT_H_

#include <array>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/day_window.h"

namespace core {

/// @brief Daylight in game hours for each day of the 48-day year, January
/// first.
///
/// Daylight by date at the fixed campaign latitude 55.75 N (time design §3:
/// June 17.5 game hours, equinoxes ~12.2, December 7.0). Structural, not
/// balance — the latitude is part of the setting, so the curve lives in
/// code, not in a table (manual/61-balance-tables.md, the CSV/constexpr
/// split).
///
/// Precomputed rather than done with libm at run time on purpose:
/// bit-for-bit determinism across compilers is a hard requirement, and trig
/// library results are not guaranteed to the last ulp. Values were generated
/// once with the standard sunrise equation (declination from the day of
/// year, -0.83 degree refraction altitude), mapping game day i (month m, day
/// d) to the real year as (m + (d + 0.5) / 4) / 12 * 365.25 days.
inline constexpr std::array<float, kDaysPerYear> kDaylightGameHours = {
    7.1892F,  7.4434F,  7.7872F,  8.2038F,  8.6773F,  9.1934F,  9.7404F,  10.3092F,
    10.8928F, 11.4860F, 12.0850F, 12.6863F, 13.2868F, 13.8828F, 14.4695F, 15.0407F,
    15.5878F, 16.0994F, 16.5611F, 16.9557F, 17.2641F, 17.4682F, 17.5538F, 17.5144F,
    17.3530F, 17.0814F, 16.7166F, 16.2777F, 15.7828F, 15.2474F, 14.6840F, 14.1022F,
    13.5090F, 12.9098F, 12.3084F, 11.7082F, 11.1124F, 10.5247F, 9.9497F,  9.3936F,
    8.8648F,  8.3740F,  7.9344F,  7.5616F,  7.2722F,  7.0819F,  7.0024F,  7.0390F,
};

/// @brief Daylight of a day, in game hours (7.00 in December, 17.55 at
/// midsummer).
/// @param day The day number. ANY day number answers: the year is a cycle,
/// so a day of the year (0..47), a day of the campaign (SimDay) and a day
/// past the end of the year all land on the same curve. This is what lets a
/// caller step "one month forward" twelve times without guarding the year
/// boundary itself.
constexpr float DaylightHoursOfDay(std::uint64_t day) {
  return kDaylightGameHours[day % kDaysPerYear];
}

/// @brief When the sun rises and sets on a day, as hours from midnight.
/// @param day Any day number, as DaylightHoursOfDay takes it.
/// The window is centred on noon — the core's clock has no timezone — so
/// this is exactly what the simulation itself works by on that day.
constexpr DayWindow SolarWindowOfDay(std::uint64_t day) {
  return SolarWindow(DaylightHoursOfDay(day));
}

/// @brief The day of the year the month forms below answer for: the SECOND
/// of the month's four days.
///
/// The second day and not "the middle": the middle of four days lies between
/// the second and the third, so a month has no middle day to speak of, and
/// the rounding has to pick a side out loud. This picks the EARLIER one.
constexpr std::uint32_t SecondDayOfMonth(Month month) {
  return (static_cast<std::uint32_t>(month) * kDaysPerMonth) + 1U;
}

/// @brief Daylight of a MONTH, in game hours — the daylight of its SECOND
/// day out of four.
///
/// A month does not have one daylight. Light moves inside it, and inside
/// September it moves by 1.20 game hours from the first day to the fourth —
/// so this answer is up to 1.2 hours of daylight away from what the month's
/// own edges have, which is 0.6 hours on the sunrise and 0.6 on the sunset.
/// Whoever needs the day right takes DaylightHoursOfDay instead; this form
/// exists for a caller who is showing "December" rather than a date, and it
/// names its own day in its name so that no caller has to guess which one it
/// got.
constexpr float DaylightHoursOfMonthSecondDay(Month month) {
  return DaylightHoursOfDay(SecondDayOfMonth(month));
}

/// @brief When the sun rises and sets in a MONTH — the window of its SECOND
/// day out of four, with the tolerance named at
/// DaylightHoursOfMonthSecondDay.
constexpr DayWindow SolarWindowOfMonthSecondDay(Month month) {
  return SolarWindow(DaylightHoursOfMonthSecondDay(month));
}

}  // namespace core

#endif  // CORE_COMMON_DAYLIGHT_H_
