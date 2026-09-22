// The solar curve MOVED to include/core_common/daylight.h on 23 September
// 2026: the presentation now asks for sunrise and sunset without running the
// simulation to that day (boss, thread boss-core-sunrise-for-month-2026-09-23),
// and a value with two readers cannot stay private to one of them.
//
// This file is left as the one line that forwards, so that no copy of the
// table exists twice while it is here. It has no content of its own and
// should be deleted; the role does not delete files (root rules §11a), so it
// is named to boss in the delivery parcel instead.

#ifndef CORE_TIME_DAYLIGHT_TABLE_H_
#define CORE_TIME_DAYLIGHT_TABLE_H_

#include "core_common/daylight.h"

#endif  // CORE_TIME_DAYLIGHT_TABLE_H_
