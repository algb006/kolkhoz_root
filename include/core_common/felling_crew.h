/// @file felling_crew.h
/// @brief The felling crew sized to its mark (timber design «Докуда рубят»).
/// @threading SINGLE_THREADED - read by the labour's job collection (phase 3).

#ifndef CORE_COMMON_FELLING_CREW_H_
#define CORE_COMMON_FELLING_CREW_H_

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/day_off.h"
#include "core_common/timber_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief THE FELLING CREW SIZED TO THE MARK (timber design «Докуда рубят»,
///        boss 17 October 2026: «едет столько рук, сколько срубят отмеченный
///        объём к сроку нужды, не больше»; econ's felling-crew-sized-to-mark
///        §1): the mark's remaining man-days over the working days left to its
///        need date (IsDayOffIn, today included, the need date not), rounded
///        up, clamped to 1..`cap`. Behind the date the crew grows; ahead it
///        shrinks; past it the whole remainder is asked in one day, capped.
///        A mark with no need date (TimberStandRow::need_by_day, before save
///        165) keeps the whole cap. Until 0.37.272 every free hand within the
///        cap went: in #994's pair (riding) 3.7 times the fellers felled the
///        same marks sooner, and the old households lost their yards' plot
///        hours (hunger +22.6 %).
/// @return The crew, 0 when `cap` is 0.
inline std::uint32_t FellingCrewToday(const WorldState& world,
                                      const TimberStandRow& stand,
                                      std::uint32_t cap) {
  if (stand.need_by_day == kNeverPlanted || cap == 0) {
    return cap;
  }
  std::uint32_t working_days = 0;
  for (SimDay day = world.calendar.day; day < stand.need_by_day; ++day) {
    working_days += IsDayOffIn(world, day) ? 0U : 1U;
  }
  working_days = std::max(working_days, 1U);
  const auto crew = static_cast<std::uint32_t>(
      std::ceil(stand.work_days_remaining / static_cast<float>(working_days)));
  return std::clamp(crew, 1U, cap);
}

}  // namespace core

#endif  // CORE_COMMON_FELLING_CREW_H_
