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
/// the design names — the working day by the sun, the two-hour commute
/// limit, the fatigue walk-off, skip-ahead presets — resolves in hours, and
/// nothing decides at finer grain. Structural like kDaysPerMonth: changing it
/// re-times every schedule and breaks saves (VERSION_SAVE).
inline constexpr std::uint32_t kTicksPerDay = 24;

inline constexpr std::uint32_t kTicksPerYear = kTicksPerDay * kDaysPerYear;  // 1152

/// @brief The unified chronometer (time design, §3): the game clock runs
/// this many times faster than real time, and visible movement is always
/// real. Effective speeds in game hours are therefore real speeds divided
/// by this constant; balance tables keep the real, human-readable numbers
/// (tables/transport.csv). Structural, not balance.
inline constexpr std::uint32_t kClockScale = 12;

/// @brief Real man-days behind one GAME man-day of work (root rules §9:
/// "real man-days / 7 = game days"). Balance tables keep the real,
/// human-readable norms of the agronomy books; every consumer divides by this
/// once at parse time and works in game man-days afterwards. Structural like
/// kClockScale — it follows from the 48-day year, not from balance.
inline constexpr float kRealDaysPerGameDay = 7.0F;

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
/// `tick` is the authority; `day`, `date`, `weekday` and `season` are caches
/// derived from it, refreshed by the time phase (RefreshCalendarCaches) so
/// that no other phase repeats the arithmetic. Which weekday day 0 falls on
/// is a campaign setup parameter (tables), applied once when the cache is
/// computed.
struct CalendarState {
  Tick tick = 0;

  SimDay day = 0;

  Date date;

  Weekday weekday = Weekday::kMonday;

  Season season = Season::kWinter;

  /// Which weekday day 0 falls on. Campaign setup: written once at genesis
  /// from the campaign table (world genesis, core_world), read by every
  /// cache refresh afterwards — the one home of this fact.
  Weekday day_zero_weekday = Weekday::kMonday;
};

// ---------------------------------------------------------------------------
// Calendar arithmetic — pure functions of the clocks
// ---------------------------------------------------------------------------

constexpr SimDay SimDayFromTick(Tick tick) {
  return static_cast<SimDay>(tick / kTicksPerDay);
}

/// @brief Hour within the day, 0..23. Hour 0 is the start of the day.
constexpr std::uint32_t HourFromTick(Tick tick) {
  return static_cast<std::uint32_t>(tick % kTicksPerDay);
}

/// @brief Broken-down date of a day. Year is uint16: fine for any campaign.
constexpr Date DateFromDay(SimDay day) {
  const std::uint32_t day_of_year = day % kDaysPerYear;
  return Date{
      .year = static_cast<std::uint16_t>(1 + (day / kDaysPerYear)),
      .month = static_cast<Month>(day_of_year / kDaysPerMonth),
      .day_in_month = static_cast<std::uint8_t>(day_of_year % kDaysPerMonth),
  };
}

/// @brief Weekday of a day. The week runs independently of months: it is
/// plain modulo-7 from the campaign's day-zero weekday.
constexpr Weekday WeekdayFromDay(SimDay day, Weekday day_zero_weekday) {
  return static_cast<Weekday>((static_cast<std::uint32_t>(day_zero_weekday) + day) % kDaysPerWeek);
}

/// @brief Season of a month: December–February is winter, and so on by threes.
constexpr Season SeasonOfMonth(Month month) {
  return static_cast<Season>(((static_cast<std::uint32_t>(month) + 1) / kMonthsPerSeason) %
                             kSeasonsPerYear);
}

/// @brief Recomputes every cached field of `calendar` from its tick and its
/// stored day-zero weekday. The time phase calls this once per step after
/// advancing the tick; tests and world setup call it after setting the tick
/// directly.
constexpr void RefreshCalendarCaches(CalendarState& calendar) {
  calendar.day = SimDayFromTick(calendar.tick);
  calendar.date = DateFromDay(calendar.day);
  calendar.weekday = WeekdayFromDay(calendar.day, calendar.day_zero_weekday);
  calendar.season = SeasonOfMonth(calendar.date.month);
}

/// @brief A person's age in BIOLOGICAL years — what the body has lived,
/// not what the calendar has.
/// @param life_speedup tables/life.csv `life_speedup`: game years per
///        biological year (×4 by the canon). It belongs to the balance
///        tables, so it is a parameter and not a constant here.
/// @param birth_day SIGNED on purpose. The starting generation was born
///        BEFORE day 0 (resident_state.h), so the subtraction has to happen
///        in a signed type; done in SimDay's own unsigned type, the
///        old-timers come out four billion days old.
///
/// ONE HOME, and it took five to notice. This was written out by hand in
/// core_residents (three times), core_labor and core_boundary — five copies
/// of four lines, agreeing only because nobody had yet changed one of them
/// (task A6, 2026-09-04). It belongs here because it is calendar
/// arithmetic and nothing else: no state, no module, no subsystem's rule.
constexpr float BiologicalAgeYears(float life_speedup, std::int32_t birth_day, SimDay day) {
  const float game_years = static_cast<float>(static_cast<std::int32_t>(day) - birth_day) /
                           static_cast<float>(kDaysPerYear);
  return game_years * life_speedup;
}

}  // namespace core

#endif  // CORE_COMMON_CALENDAR_H_
