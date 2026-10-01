/// @file
/// @brief The one door for «is this day a day off in THIS world»: the
/// calendar's rest day or holiday, unless the chairman cancelled it
/// (OrderKind::kCancelDayOff; boss seq 103 and 107) or the harvest stands
/// (OrderKind::kHarvestWithoutDaysOff; farming design, the harvest rule 1).
/// Holidays are never worked (time §9).
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

/// @brief Whether `day` is a day off in `world`: IsRestDay by the world's
/// calendar and epoch, and not the day the chairman cancelled — and, while
/// ChairmanState::harvest_without_days_off stands and HarvestStands(world),
/// not a week's day off of today or later in this calendar year either.
/// @param day A simulation day; the cancelled day is today or later.
/// @note THE HARVEST'S ANSWER FOR A DAY AHEAD IS TODAY'S: nobody knows the
///       day the last field will be reaped, so a day to come is judged by
///       what stands this morning. The counts that walk the days to the
///       snow (labor's last days, the gathering alarm) therefore count the
///       Sundays as working while the crop stands, which is the rule's
///       point; a past day, or a day of another year, is the calendar's.
/// @note The answer can change within a day: the hour the last field is
///       reaped a Sunday becomes a day off again, and the hour a crop
///       ripens it stops being one. The morning's placement has been made
///       by then; the evening's rest reads the evening's answer.
bool IsDayOffIn(const WorldState& world, SimDay day);

}  // namespace core

#endif  // CORE_COMMON_DAY_OFF_H_
