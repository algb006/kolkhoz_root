/// @file
/// @brief What rotted IN CLAMPS, potato and vegetables, a day over the last
/// year: the subject of the lamp «В буртах сгнило N т» (kClampRot; #1005;
/// econ's clamp-rot-lamp-2026-10-17 §4a, §4c).
/// @threading PARALLEL_READONLY for the read; NoteClampRot is called by
/// core_production's SpoilStores at the day's last tick, single-threaded.
///
/// WHY A COLUMN OF ITS OWN. The book's `spoiled` has three writers - the
/// units' stores, the families' larders and the field heaps - in one column
/// (ledger_state.h). Read as the lamp's amount it would light the canon,
/// whose y1 `spoiled` is 24.6 t larders included (host, 0.37.285), and it
/// cannot tell a clamp from the vegetable store whose keeping is the
/// decision. The year's clamp rot by resource is the book's
/// `spoiled_in_clamps`; this is the same grams by day, for the window.

#ifndef CORE_COMMON_CLAMP_ROT_STATE_H_
#define CORE_COMMON_CLAMP_ROT_STATE_H_

#include <array>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/quantities.h"

namespace core {

/// The window's «nothing ever noted».
inline constexpr SimDay kNoClampRotDay = 0xFFFFFFFFU;

/// @brief Potato and vegetables rotted in clamps a day over the last year,
/// grams. SAVED (save 170).
struct ClampRotYear {
  /// The last day noted; kNoClampRotDay while none was.
  SimDay last_day = kNoClampRotDay;

  /// By day % kDaysPerYear: the grams rotted in clamps that day. A slot is
  /// valid only for a day no later than `last_day` and less than a year
  /// before it - NoteClampRot clears the days between two notes.
  std::array<Grams, kDaysPerYear> per_day = {};
};

/// @brief Adds `grams` rotted in clamps on `day` (0 is a valid note: it
/// moves the window on).
/// @pre `day` is not before the last noted day.
inline void NoteClampRot(ClampRotYear& year, SimDay day, Grams grams) {
  if (year.last_day == kNoClampRotDay) {
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
  year.per_day[day % kDaysPerYear] += grams > 0 ? grams : 0;
  year.last_day = day;
}

/// @brief The grams rotted in clamps in the year of days ending `today`.
inline Grams ClampRotOfYear(const ClampRotYear& year, SimDay today) {
  if (year.last_day == kNoClampRotDay || today < year.last_day ||
      today - year.last_day >= kDaysPerYear) {
    return 0;
  }
  Grams sum = 0;
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

#endif  // CORE_COMMON_CLAMP_ROT_STATE_H_
