/// @file
/// @brief The migrants turned away for want of a home, counted a day over the
/// last year (task 433, the lamp «жилья не хватает»; boss, core-boss-housing-
/// short-2026-10-10 [2]).
/// @threading PARALLEL_READONLY for the read; NoteTurnedAway is called by
/// the residents' migration (demography.cpp, RunMigration), single-threaded.
///
/// WHY A COUNT AND NOT A QUEUE. The district's settlers come «only to a free
/// house» (district design §2): with no free house and no barrack place a
/// migrant simply does not come, and until this count nothing kept or said
/// it. The design's «four newcomers wait» (demography, the lamp's condition
/// (c), the former «ехать некуда» of 1 October) is read as «four turned away
/// in the last year» — there is no queue to wait in.

#ifndef CORE_COMMON_TURNED_AWAY_STATE_H_
#define CORE_COMMON_TURNED_AWAY_STATE_H_

#include <array>
#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// The year's «never turned away».
inline constexpr SimDay kNoTurnedAwayDay = 0xFFFFFFFFU;

/// @brief The migrants turned away a day over the last year. SAVED (save 148).
struct TurnedAwayYear {
  /// The last day a migrant was turned away; kNoTurnedAwayDay while none was.
  SimDay last_day = kNoTurnedAwayDay;

  /// By day % kDaysPerYear: the migrants turned away that day (at most 255).
  /// A slot is valid only for a day no later than `last_day` and less than a
  /// year before it — NoteTurnedAway clears the days between two notes.
  std::array<std::uint8_t, kDaysPerYear> per_day = {};
};

/// @brief Notes one migrant turned away on `day`.
/// @pre `day` is not before the last noted day.
inline void NoteTurnedAway(TurnedAwayYear& year, SimDay day) {
  if (year.last_day == kNoTurnedAwayDay) {
    year.per_day.fill(0);
  } else if (year.last_day != day) {
    SimDay gap = year.last_day + 1U;
    if (day >= kDaysPerYear && day - (kDaysPerYear - 1U) > gap) {
      gap = day - (kDaysPerYear - 1U);
    }
    for (; gap <= day; ++gap) {
      year.per_day[gap % kDaysPerYear] = 0;
    }
  }
  std::uint8_t& slot = year.per_day[day % kDaysPerYear];
  slot = slot == 255 ? slot : static_cast<std::uint8_t>(slot + 1U);
  year.last_day = day;
}

/// @brief The migrants turned away in the year of days ending `today`.
inline std::uint32_t TurnedAwayOfYear(const TurnedAwayYear& year, SimDay today) {
  if (year.last_day == kNoTurnedAwayDay || today < year.last_day ||
      today - year.last_day >= kDaysPerYear) {
    return 0;
  }
  std::uint32_t sum = 0;
  for (SimDay back = 0; back < kDaysPerYear && back <= today; ++back) {
    const SimDay day = today - back;
    if (day > year.last_day || year.last_day - day >= kDaysPerYear) {
      continue;
    }
    sum += year.per_day[day % kDaysPerYear];
  }
  return sum;
}

}  // namespace core

#endif  // CORE_COMMON_TURNED_AWAY_STATE_H_
