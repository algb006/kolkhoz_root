/// @file
/// @brief The one door for «is this day a day off in THIS world»: the
/// calendar's rest day or holiday, unless the chairman cancelled it
/// (OrderKind::kCancelDayOff; boss seq 103 and 107) or the harvest stands
/// under his standing order (OrderKind::kHarvestWithoutDaysOff; farming
/// design, the harvest rule 1). Holidays are never worked (time §9).
/// @threading PARALLEL_READONLY
/// A pure read of the world.
///
/// WHY A DOOR AND NOT A FLAG BESIDE IsRestDay. Five callers in three modules
/// ask the calendar today: labor's placement (twice), its pay, residents'
/// rest recovery and the MTS column. A cancelled day off that only one of
/// them knew of would work the fields and recover the village's rest as on a
/// Sunday — or the reverse. Two doors to one question, and the rule that
/// hangs on one is walked around through the other in silence. The
/// implementation moved every caller of «is this a day off» here; calendar.h's
/// IsRestDay stays as the calendar's own answer, which this door reads first
/// — and which one caller asks on purpose: rush.cpp's IsCancelledDayOff wants
/// the calendar's Sunday, not the village's.

#ifndef CORE_COMMON_DAY_OFF_H_
#define CORE_COMMON_DAY_OFF_H_

#include "core_common/calendar.h"

namespace core {

struct WorldState;

/// @brief Whether the harvest stands TODAY: a field of the arable is in its
/// reaping with work left (FieldPhase::kHarvest, work_days_remaining above
/// nought). The meadow's cut is not the harvest: grass has no snow edge.
/// The view's flag «страда сегодня», whatever the chairman's switch says.
bool HarvestStands(const WorldState& world);

/// @brief Whether `day` is a day off BY THE CALENDAR AND THE CHAIRMAN'S ONE
/// CANCELLED DAY, the harvest not asked: IsRestDay by the world's calendar
/// and epoch, and not the day he cancelled (kCancelDayOff).
///
/// THE COUNT «NOT IN TIME WITH THE DAYS OFF KEPT» ASKS THIS, NOT IsDayOffIn
/// (boss, core-boss-potato-crew-trace-2026-10-01 [27] and the answer to
/// [28]): a count that asked the door it is to drive would go round — the
/// lamp lights, the day off is lifted, the days are enough, the lamp goes
/// out, the day off comes back.
bool IsCalendarDayOffIn(const WorldState& world, SimDay day);

/// @brief Whether the gathering count's word «not in time with the days off
/// kept» STANDS TODAY (WorldState::gather_short_said): said at yesterday's
/// close, or at today's own — so that the evening reads the day the morning
/// read. The condition of the harvest without days off since 0.37.147.
bool GatherShortStands(const WorldState& world);

/// @brief Whether `day` is a day off in `world`: IsCalendarDayOffIn — and,
/// while ChairmanState::harvest_without_days_off stands, the harvest stands
/// (HarvestStands) AND THE GATHERING COUNT'S WORD STANDS (GatherShortStands),
/// not a week's day off of today or later in this calendar year. Never a
/// holiday (time §9).
/// @param day A simulation day; the cancelled day is today or later.
/// @note THE WORD IS THE RULE'S CONDITION SINCE 0.37.147 (boss [27]; the
///       counterweight of the harvest rule 1). Until then «a ripe crop
///       stands» alone lifted the day off: true every autumn, it cost 7 and
///       12 points of the adults' rest in years 2-3 where nothing was going
///       under the snow (econ [26]), and one or two Sundays a year in a
///       village nobody commands. Hung on the count of 0.37.91 — one pace
///       for every field — the door lost 64 t more in year 1 (the word came
///       after the Sunday on 1938 and 1939) and still lifted the Sundays of
///       year 2 in nine villages of nine; the count is by fields, light,
///       road and hands since 0.37.147 (core_common/reaping_pace.h).
/// @note THE ANSWER FOR A DAY AHEAD IS TODAY'S: nobody knows the day the
///       last field will be reaped, so a day to come is judged by what
///       stands this morning. A past day, or a day of another year, is the
///       calendar's.
/// @note The answer can change within a day: the hour the last field is
///       reaped a Sunday becomes a day off again, and the hour a crop
///       ripens it stops being one. The morning's placement has been made
///       by then; the evening's rest reads the evening's answer.
bool IsDayOffIn(const WorldState& world, SimDay day);

}  // namespace core

#endif  // CORE_COMMON_DAY_OFF_H_
