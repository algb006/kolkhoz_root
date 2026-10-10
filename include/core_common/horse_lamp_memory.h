/// @file
/// @brief The lamp «лошадей не хватает» as the world painted it, remembered
/// for a year, and the young horses a kolkhoz herd keeps by it (econ, the
/// horses-not-bred-for-points rule, econ/manual/proposals/horses-not-bred-
/// for-points-2026-10-10.md §1; boss's rulings in core-boss-c2-site-supply-
/// 2026-10-09 after [87] and in core-boss-feed-events-2026-10-10 [4]).
/// @threading PARALLEL_READONLY for the reads; NoteHorseLampPainted is called
/// once a day, single-threaded, by the world's hour-0 lamp check (world.cpp,
/// SayLampsTurnedRed) — the one writer.
///
/// THE ONE HOME OF THE LAMP'S STATE. The lamp is computed, never stored: its
/// test runs whenever someone collects the alarms. The rule needs the lamp's
/// PAST — «was it lit in the last year, and how many teams short at its
/// worst» — and a second copy of the lamp's test, kept by the herd day, was
/// the first prototype's and it disagreed with the red probe's count of lit
/// days (arm C, the thread's [87]). So the memory is written where the world
/// paints its lamps once a day, from the alarm it painted, and read by the
/// rule's three homes (the district's door, the advice, the forecast).
///
/// THE RULE IT SERVES. A kolkhoz horse herd keeps one young for three adults
/// (a six-year working life, two years in hand), and while the lamp was
/// painted in the last year as many young more as its largest «teams short»
/// of that year. The young above that are handed over first, newborns before
/// juveniles; an adult horse is handed over only when the lamp has been dark
/// for a whole year; a kept young never.

#ifndef CORE_COMMON_HORSE_LAMP_MEMORY_H_
#define CORE_COMMON_HORSE_LAMP_MEMORY_H_

#include <algorithm>
#include <array>
#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// The memory's «never painted».
inline constexpr SimDay kNoHorseLampDay = 0xFFFFFFFFU;

/// @brief The lamp «лошадей не хватает»'s painted days of the last year and
/// their «teams short». SAVED (save 147).
struct HorseLampMemory {
  /// The last day the hour-0 check found the lamp painted; kNoHorseLampDay
  /// while it never was.
  SimDay last_painted_day = kNoHorseLampDay;

  /// By day % kDaysPerYear: the «teams short» (Alarm::amount of
  /// kTooFewHorses) the lamp was painted with that day, 0 for a day it was
  /// not. A slot is valid only for a day no later than `last_painted_day`
  /// and less than a year before it — NoteHorseLampPainted clears the days
  /// between two paintings, the readers skip the rest.
  std::array<std::uint8_t, kDaysPerYear> teams_short = {};
};

/// @brief Notes that the lamp was painted on `day` with `teams_short` teams
///        short (clamped to 0..255). Days between the last painting and this
///        one are cleared: the lamp was dark on them.
/// @pre `day` is not before the last painted day.
inline void NoteHorseLampPainted(HorseLampMemory& memory, SimDay day, std::int64_t teams_short) {
  const auto teams = static_cast<std::uint8_t>(std::clamp<std::int64_t>(teams_short, 0, 255));
  if (memory.last_painted_day != kNoHorseLampDay && memory.last_painted_day == day) {
    std::uint8_t& slot = memory.teams_short[day % kDaysPerYear];
    slot = std::max(slot, teams);
    return;
  }
  if (memory.last_painted_day != kNoHorseLampDay) {
    // The dark days between, at most the year before `day`: every slot a
    // reader of a later day could still ask for.
    SimDay gap = memory.last_painted_day + 1U;
    if (day >= kDaysPerYear && day - (kDaysPerYear - 1U) > gap) {
      gap = day - (kDaysPerYear - 1U);
    }
    for (; gap < day; ++gap) {
      memory.teams_short[gap % kDaysPerYear] = 0;
    }
  } else {
    memory.teams_short.fill(0);
  }
  memory.teams_short[day % kDaysPerYear] = teams;
  memory.last_painted_day = day;
}

/// @brief Whether the lamp was painted on any of the year of days ending
///        `today` (today included).
inline bool HorseLampLitWithinYear(const HorseLampMemory& memory, SimDay today) {
  return memory.last_painted_day != kNoHorseLampDay && today >= memory.last_painted_day &&
         today - memory.last_painted_day < kDaysPerYear;
}

/// @brief The largest «teams short» the lamp was painted with in the year of
///        days ending `today`; 0 when it was not painted in that year.
inline std::int64_t HorseLampTeamsShortOfYear(const HorseLampMemory& memory, SimDay today) {
  if (!HorseLampLitWithinYear(memory, today)) {
    return 0;
  }
  std::uint8_t worst = 0;
  for (SimDay back = 0; back < kDaysPerYear && back <= today; ++back) {
    const SimDay day = today - back;
    if (day > memory.last_painted_day || memory.last_painted_day - day >= kDaysPerYear) {
      continue;
    }
    worst = std::max(worst, memory.teams_short[day % kDaysPerYear]);
  }
  return worst;
}

/// @brief The young (juveniles and newborns) a kolkhoz horse herd of
///        `adults` keeps today: one for three adults, and the year's worst
///        «teams short» more while the lamp was painted within the year.
inline std::int64_t HorseYoungKept(const HorseLampMemory& memory,
                                   std::int64_t adults,
                                   SimDay today) {
  return (adults / 3) + HorseLampTeamsShortOfYear(memory, today);
}

}  // namespace core

#endif  // CORE_COMMON_HORSE_LAMP_MEMORY_H_
