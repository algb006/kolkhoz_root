/// @file
/// @brief The count of the reaping BY FIELDS: the crew the village can put on
/// it, a field's pace under a day's light and on its own road, and the walk
/// of a field's reaping to the snow — the one home of the count the
/// harvest-will-not-be-gathered alarm and labor's last days before the snow
/// both make.
/// @threading PARALLEL_READONLY
/// Pure functions of a ledger, a table of light and the caller's numbers.
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

#include <array>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/daylight.h"
#include "core_common/ledger_state.h"

namespace core {

// ONE PACE FOR THE WHOLE VILLAGE STOOD HERE UNTIL 0.37.147
// (ReapingPacePerDay): the last day of reaping that ended with reaping still
// owed, scaled by today's daylight over its own — the season's BEST day until
// 2026-09-19 — and every hand at a norm-day before the season had such a day.
// It laid one field's yesterday on every field's tomorrow (below).

/// @brief The crew the count by fields takes (0.37.147; the harvest rule 5).
struct ReapingCrew {
  /// The hands the reaping can have: yesterday's hands free of the day's
  /// standing duties (YearLedger::reaping_last_day_hands) — or every hand of
  /// the village before the book has a day to read.
  double hands = 0.0;

  /// Norm-days the village reaps for each hand that COULD reap, an hour of
  /// the day's light: the last day of reaping that ended with reaping owed,
  /// its norm-days over the hand-hours of light that day offered
  /// (YearLedger::reaping_last_day_hours) — the men's own skill, hunger and
  /// avral are in it, AND THE QUEUE'S OWN CHOICE of how many of the hands it
  /// put on the reaping and for how long. Before this season has such a day —
  /// last year's; before any year has — the table's number.
  double norm_days_an_hour = 0.0;
};

/// @brief The crew by the year's book.
///
/// THE RATE IS WHAT THE VILLAGE DID, NOT WHAT A MAN CAN (0.37.147's first
/// form, never pushed, took the reapers' booked hours and gave every hand the
/// whole light at their rate; and before the season had a day to read, a
/// norm-day a standard day for each). Measured on that form, nine villages,
/// years 1-2: a hand is booked 5 to 6 hours of reaping in a light of 9 to 15,
/// at 0.08 norm-days an hour, and the village reaps 0.042 norm-days a hand
/// that could, an hour of light, on the days the reaping was left unfinished
/// (25 days: the median 0.0416, the lower quartile 0.039). The count promised
/// twice to three times that: on seed 1938, year 1, it said «in time» till
/// the evening of day 35 with 88.3 norm-days owed, the only dry day of six
/// was the Sunday it kept, and 61.6 t went under the snow against 19.3.
/// @param closed Last year's book: its last unfinished day stands in while
///        this season has none.
/// @param hands Every hand of the village, for a book with no morning yet.
/// @param table_rate Norm-days a hand that could reap, an hour of light, for
///        a village with no such day in either book (farming.csv
///        `reaping_days_per_hand_light_hour`).
inline ReapingCrew ReapingCrewOf(const YearLedger& book,
                                 const YearLedger& closed,
                                 std::uint32_t hands,
                                 float table_rate) {
  ReapingCrew crew;
  crew.hands = book.reaping_last_day_hands > 0.0F ? static_cast<double>(book.reaping_last_day_hands)
                                                  : static_cast<double>(hands);
  if (book.reaping_last_day > 0.0F && book.reaping_last_day_hours > 0.0F) {
    crew.norm_days_an_hour = static_cast<double>(book.reaping_last_day) /
                             static_cast<double>(book.reaping_last_day_hours);
  } else if (closed.reaping_last_day > 0.0F && closed.reaping_last_day_hours > 0.0F) {
    crew.norm_days_an_hour = static_cast<double>(closed.reaping_last_day) /
                             static_cast<double>(closed.reaping_last_day_hours);
  } else {
    crew.norm_days_an_hour = static_cast<double>(table_rate);
  }
  return crew;
}

/// @brief Norm-days that crew puts on ONE field in a day of `daylight_hours`
/// whose one-way road is `road_hours`: the day's light less the road there
/// and back, at the crew's rate an hour of light.
///
/// BY THE FIELD, NOT BY THE VILLAGE (boss, core-boss-potato-crew-trace-2026-
/// 10-01 [27]; host-boss-pin-0-37-133-2026-10-02 [42], [152]). The one pace
/// before it laid one field's yesterday on every field's tomorrow: the near
/// potato's twenty norm-days a day were promised to three far fields, and on
/// seed 1938, year 1, the count said «in time» with 88.3 norm-days owed a
/// week before the snow — 19.3 t lost. The road and the light are each
/// field's and each day's own here.
inline double ReapingPaceOnField(const ReapingCrew& crew, float daylight_hours, float road_hours) {
  const double hours =
      static_cast<double>(daylight_hours) - (2.0 * static_cast<double>(road_hours));
  return hours > 0.0 ? crew.hands * crew.norm_days_an_hour * hours : 0.0;
}

/// @brief Where a field's reaping ends when the crew starts it at `start`.
struct ReapingWalk {
  /// The calendar point (day of the year, fractional) the last norm-day is
  /// reaped at — or the point the walk stopped at, past `last_day`.
  double clock = 0.0;

  /// True when the field is reaped by the end of `last_day`.
  bool done = false;
};

/// @brief Walks one field's reaping day by day from `start` to the end of
/// `last_day`: each day gives ReapingPaceOnField under that day's own light
/// (core_common/daylight.h), times the day's dry share.
/// @param no_work_share Per day of the year, the share of it that is NO
///        working day: the climate's rain share (core_common/
///        rain_stops_work.h), 1 for a day off and for a rained-out today —
///        the caller's array, as the gathering alarm and labor's last days
///        build it.
/// @param owed_days The reaping still owed on the field, norm-days.
/// @param start The calendar point the crew comes to the field at.
/// @param last_day The last day that counts (the early snow's edge).
inline ReapingWalk WalkTheReaping(const std::array<float, kDaysPerYear>& no_work_share,
                                  const ReapingCrew& crew,
                                  float road_hours,
                                  double owed_days,
                                  double start,
                                  std::uint32_t last_day) {
  ReapingWalk walk{.clock = start, .done = !(owed_days > 0.0)};
  double left = owed_days;
  const double end = static_cast<double>(last_day) + 1.0;
  while (!walk.done && walk.clock < end) {
    const auto day = static_cast<std::uint32_t>(walk.clock);
    const double rest_of_day = static_cast<double>(day) + 1.0 - walk.clock;
    const double dry = 1.0 - static_cast<double>(no_work_share[day % kDaysPerYear]);
    const double pace = ReapingPaceOnField(crew, DaylightHoursOfDay(day), road_hours) * dry;
    if (pace > 0.0 && pace * rest_of_day >= left) {
      walk.clock += left / pace;
      walk.done = true;
      break;
    }
    left -= pace * rest_of_day;
    walk.clock = static_cast<double>(day) + 1.0;
  }
  return walk;
}

}  // namespace core

#endif  // CORE_COMMON_REAPING_PACE_H_
