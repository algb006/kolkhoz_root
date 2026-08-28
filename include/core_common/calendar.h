/// @file
/// @brief Time types and the calendar block of the world state.
/// @threading PARALLEL_READONLY
/// Written only by the "time and weather" phase (phase 1, single-threaded, the
/// first thing a step does); every other phase of the same step reads it as
/// frozen. Plain data; no synchronization needed under that discipline.
///
/// The calendar of the game (time design, §4): a month is 4 visible days, a
/// year is 48 days, and the 7-day week runs independently of months — some
/// months contain no Sunday, exactly like real months lack a fifth one. There
/// is no day-of-month in any player-facing text, only month and weekday, but
/// the state keeps the full breakdown because mechanics count in days.
///
/// Two clocks, one counter:
///   * Tick  — the fixed simulation step, the only monotone clock. One tick
///     is one game hour (kTicksPerDay = 24) — decided by the step-cycle
///     contract, rationale in manual/53-step-cycle.md.
///   * SimDay — whole days since campaign start; every calendar value below
///     is a pure function of it. Day 0 is the first day of year 1.
///
/// Human age runs on a third, faster clock: biology is accelerated ~4x while
/// the calendar is not (demography design, §2). That factor is a balance
/// parameter and lives in tables; state stores only birth days in SimDay.

#ifndef CORE_COMMON_CALENDAR_H_
#define CORE_COMMON_CALENDAR_H_

#include <cstdint>

namespace core {

// ---------------------------------------------------------------------------
// Clocks
// ---------------------------------------------------------------------------

/// @brief Fixed simulation step counter, monotone from 0 at campaign start.
/// Game speed changes how often ticks fire in real time, never what a tick
/// computes. 64 bits: no overflow on any horizon.
using Tick = std::uint64_t;

/// @brief Whole days since campaign start. Day 0 = year 1, January, day 1.
using SimDay = std::uint32_t;

// ---------------------------------------------------------------------------
// Calendar structure — fixed by design, not balance
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t kDaysPerMonth = 4;
inline constexpr std::uint32_t kMonthsPerYear = 12;
inline constexpr std::uint32_t kDaysPerYear = kDaysPerMonth * kMonthsPerYear;  // 48
inline constexpr std::uint32_t kDaysPerWeek = 7;
inline constexpr std::uint32_t kMonthsPerSeason = 3;
inline constexpr std::uint32_t kSeasonsPerYear = 4;
inline constexpr std::uint32_t kDaysPerSeason = kDaysPerMonth * kMonthsPerSeason;  // 12

/// @brief Simulation ticks per day: one tick is one game hour.
/// Fixed by the step-cycle contract (core_sim/step.h): every in-day mechanic
/// the design names — the working day by the sun, the hour-long commute
/// limit, the fatigue walk-off, skip-ahead presets — resolves in hours, and
/// nothing decides at finer grain. Structural like kDaysPerMonth: changing it
/// re-times every schedule and breaks saves (VERSION_SAVE).
inline constexpr std::uint32_t kTicksPerDay = 24;

inline constexpr std::uint32_t kTicksPerYear = kTicksPerDay * kDaysPerYear;  // 1152

/// @brief Calendar month. Values are 0-based so the enum doubles as an index.
enum class Month : std::uint8_t {
  kJanuary = 0,
  kFebruary,
  kMarch,
  kApril,
  kMay,
  kJune,
  kJuly,
  kAugust,
  kSeptember,
  kOctober,
  kNovember,
  kDecember,
};

/// @brief Day of the 7-day week. The week is real: Sunday is the day off in
/// Epochs I–II, Saturday joins it in Epoch III.
enum class Weekday : std::uint8_t {
  kMonday = 0,
  kTuesday,
  kWednesday,
  kThursday,
  kFriday,
  kSaturday,
  kSunday,
};

/// @brief Season. December–February is winter, and so on by threes.
enum class Season : std::uint8_t {
  kWinter = 0,
  kSpring,
  kSummer,
  kAutumn,
};

/// @brief Broken-down calendar date. A pure function of SimDay.
struct Date {
  /// Campaign year, counted from 1.
  std::uint16_t year = 1;

  Month month = Month::kJanuary;

  /// Day within the month, 0-based, 0..3. Never shown to the player: texts
  /// use month and weekday only (time design, §4).
  std::uint8_t day_in_month = 0;
};

// ---------------------------------------------------------------------------
// The calendar block of the world state
// ---------------------------------------------------------------------------

/// @brief Current time of the simulated world.
/// `tick` and `day` are the authority; `date`, `weekday` and `season` are
/// caches derived from `day`, refreshed by the time phase so that no other
/// phase repeats the arithmetic. Which weekday day 0 falls on is a campaign
/// setup parameter (tables), applied once when the cache is computed.
struct CalendarState {
  Tick tick = 0;

  SimDay day = 0;

  Date date;

  Weekday weekday = Weekday::kMonday;

  Season season = Season::kWinter;
};

}  // namespace core

#endif  // CORE_COMMON_CALENDAR_H_
