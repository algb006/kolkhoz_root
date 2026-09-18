/// @file
/// @brief The ripening gap of a crop, read off its two windows: the one piece
/// of crop arithmetic that production and labor both need.
/// @threading SINGLE_THREADED — pure functions, no state.
///
/// ONE HOME FOR ONE NUMBER (2026-09-18). Production's sowing gate refuses a
/// seed that cannot ripen before the snow; labor ranks the preparation of a
/// field by the same last day. Two copies of the gap would be two answers
/// to "when is it too late", and the queue would work toward a day the gate
/// does not keep — which is the defect this header was cut out to close:
/// the gate learned the snow on 2026-09-13 and the queue kept the window.

#ifndef CORE_COMMON_CROP_CALENDAR_H_
#define CORE_COMMON_CROP_CALENDAR_H_

#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// @brief Days from the LAST day a crop may be sown to the FIRST day it may
/// be reaped, off its 0-based `sow_to_month` and `harvest_from_month`: how
/// long it takes to ripen (farming design, «Поздний сев»). The windows
/// already rest on this gap; naming it invents no duration.
/// @return 0 for a crop reaped in another year than it is sown — a winter
///         crop or a perennial — or when the windows leave no gap; callers
///         read 0 as "ripening does not gate this".
constexpr std::int32_t RipenGapDays(std::uint8_t sow_to_month,
                                    std::uint8_t harvest_from_month,
                                    bool reaped_another_year) {
  if (reaped_another_year) {
    return 0;
  }
  const auto last_sowing = static_cast<std::int32_t>(
      ((static_cast<std::uint32_t>(sow_to_month) + 1U) * kDaysPerMonth) - 1U);
  const auto first_reaping =
      static_cast<std::int32_t>(static_cast<std::uint32_t>(harvest_from_month) * kDaysPerMonth);
  const std::int32_t gap = first_reaping - last_sowing;
  return gap > 0 ? gap : 0;
}

}  // namespace core

#endif  // CORE_COMMON_CROP_CALENDAR_H_
