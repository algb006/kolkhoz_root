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

/// @brief The catch-up window a mark past its need date is given, working
///        days: one game month. STUB (econ's felling-crew-sized-to-mark §3a);
///        returns to data with the player's «к сроку» on the mark order.
inline constexpr std::uint32_t kFellingCatchUpWorkingDays = 4;

/// @brief THE FELLING CREW SIZED TO THE MARK (timber design «Докуда рубят»,
///        boss 17 October 2026: «едет столько рук, сколько срубят отмеченный
///        объём к сроку нужды, не больше»; econ's felling-crew-sized-to-mark
///        §1, cured in §3a): the mark's remaining man-days over the working
///        days left to its need date (IsDayOffIn, today included, the need
///        date not) times what ONE hand brings there today (`norm_per_hand`,
///        home_reach.h HandNormToday — the placement's own price of the road),
///        rounded up, clamped to 1..`cap`. Behind the date the crew grows;
///        ahead it shrinks. PAST the date the mark is given a fresh catch-up
///        window of kFellingCatchUpWorkingDays rather than its whole remainder
///        in one day. A mark with no need date (TimberStandRow::need_by_day,
///        before save 165) or no hand to send keeps the whole cap.
///
///        Until 0.37.272 every free hand within the cap went: in #994's pair
///        (riding) 3.7 times the fellers felled the same marks sooner, and the
///        old households lost their yards' plot hours (hunger +22.6 %). Its
///        first sizing counted a hand as a whole man-day: the crew was two to
///        five times short, 240 of 612 marks ran late, and their one-day piles
///        at the cap took the yards' plot hours (hunger +74.5 %, the felling
///        thread [2]).
/// @return The crew, 0 when `cap` is 0.
inline std::uint32_t FellingCrewToday(const WorldState& world,
                                      const TimberStandRow& stand,
                                      std::uint32_t cap,
                                      float norm_per_hand) {
  if (stand.need_by_day == kNeverPlanted || cap == 0 || norm_per_hand <= 0.0F) {
    return cap;
  }
  std::uint32_t working_days = 0;
  if (world.calendar.day >= stand.need_by_day) {
    working_days = kFellingCatchUpWorkingDays;
  } else {
    for (SimDay day = world.calendar.day; day < stand.need_by_day; ++day) {
      working_days += IsDayOffIn(world, day) ? 0U : 1U;
    }
  }
  working_days = std::max(working_days, 1U);
  const auto crew = static_cast<std::uint32_t>(
      std::ceil(stand.work_days_remaining / (static_cast<float>(working_days) * norm_per_hand)));
  return std::clamp(crew, 1U, cap);
}

}  // namespace core

#endif  // CORE_COMMON_FELLING_CREW_H_
