/// @file
/// @brief The village's reaping pace today, in norm-days a day: the one home
/// of the number the harvest-will-not-be-gathered alarm and labor's last days
/// before the snow both read.
/// @threading PARALLEL_READONLY
/// A pure function of a ledger and two numbers.
///
/// WHY ONE HOME (boss, core-host-l1-stage1 seq 28, 2026-09-19). Labor's last
/// days asked "can this field still be finished" with every hand at a norm-day
/// scaled by the light, and the alarm asked "will this field be gathered" with
/// the best day scaled by the light. On host's seed 9 with the reaping made
/// three times longer, 46 hands reaped 22.7 norm-days a November day, the
/// first estimate said about 39 and the second about 19: the rule called both
/// fields finishable and the alarm, on the old pace, stood silent while the
/// snow took 150.7 t. Two paces for one question part in silence.

#ifndef CORE_COMMON_REAPING_PACE_H_
#define CORE_COMMON_REAPING_PACE_H_

#include <cstdint>

#include "core_common/ledger_state.h"

namespace core {

/// @brief Norm-days of hand reaping the village can put in today.
///
/// Once the season has reaped (YearLedger::reaping_last_day), the LAST day of
/// reaping that ended with reaping still owed — the hands the village puts on
/// it now — scaled by today's daylight over that day's own (boss seq 95, 161
/// Б). Until 2026-09-19 it was the season's BEST day, which overstated the
/// autumn and, set by a few June hands, understated it (ledger_state.h).
/// Before the season's first such day, `hands` at one norm-day each —
/// optimistic on purpose, so nothing is called lost before there is a season
/// to read. A day with no daylight booked is taken as it stands.
/// @param book The current year's ledger.
/// @param daylight_hours Today's daylight (WorldState::weather).
/// @param hands The caller's count of people who could reap, for the season's
///        first days only.
inline double ReapingPacePerDay(const YearLedger& book, float daylight_hours, std::uint32_t hands) {
  if (!(book.reaping_last_day > 0.0F)) {
    return static_cast<double>(hands);
  }
  double pace = static_cast<double>(book.reaping_last_day);
  if (book.reaping_last_day_daylight > 0.0F && daylight_hours > 0.0F) {
    pace *=
        static_cast<double>(daylight_hours) / static_cast<double>(book.reaping_last_day_daylight);
  }
  return pace;
}

}  // namespace core

#endif  // CORE_COMMON_REAPING_PACE_H_
