/// @file
/// @brief The one door for «is this day a day off in THIS world»: the
/// calendar's rest day or holiday, unless the chairman cancelled it
/// (OrderKind::kCancelDayOff; boss seq 103 and 107). Holidays are never
/// cancelled (time §9).
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

/// @brief Whether `day` is a day off in `world`: IsRestDay by the world's
/// calendar and epoch, and not the day the chairman cancelled.
/// @param day A simulation day; the cancelled day is today or later.
bool IsDayOffIn(const WorldState& world, SimDay day);

}  // namespace core

#endif  // CORE_COMMON_DAY_OFF_H_
