/// @file
/// @brief When the holder of a post stands at his unit — the post's shift
/// (professions.csv `shift`; social units design §3; the human's word of
/// 2026-09-14, boss parcels 242 and 244).
/// @threading PARALLEL_READONLY
/// Pure functions of a calendar moment; nothing is written.
///
/// THREE SHIFTS, and only the first takes the holder off the day's work:
///   * kWorkday — the working daylight, as every post was until this header:
///     the teacher, the groom, the craftsman. The accountant does not send him
///     elsewhere.
///   * kEvening — every evening, from the end of the working day until
///     bedtime: the reading hut's librarian of Epoch I. By day he is an
///     ordinary kolkhoznik on the accountant's list.
///   * kBathDay — the bath day: Saturday from the end of the working day until
///     bedtime, and Sunday from sunrise until bedtime: the village bath's
///     keeper. On weekdays he is on the accountant's list.
///
/// THE HOURS, named because the design gives words and the core needs
/// numbers (boss asked for them at the delivery): the working day ends at
/// SUNSET — the labor model's working window is the daylight — and bedtime is
/// kPostShiftBedtimeHour. "До темноты" and "до сна" are therefore one hour in
/// Epoch I, the lamp being what makes them two (STUB until electricity).

#ifndef CORE_COMMON_POST_SHIFT_H_
#define CORE_COMMON_POST_SHIFT_H_

#include <cstdint>
#include <string_view>

#include "core_common/calendar.h"
#include "core_common/day_window.h"

namespace core {

/// @brief A post's shift (professions.csv `shift`).
enum class PostShift : std::uint8_t {
  kWorkday = 0,
  kEvening,
  kBathDay,
};

/// @brief The hour the village goes to bed: evening shifts end at it.
inline constexpr std::uint32_t kPostShiftBedtimeHour = 22;

/// @brief The shift a professions.csv cell names; an empty cell is kWorkday.
/// @return false for a word that is none of workday, evening, bath_day.
constexpr bool ParsePostShift(std::string_view text, PostShift& shift) {
  if (text.empty() || text == "workday") {
    shift = PostShift::kWorkday;
    return true;
  }
  if (text == "evening") {
    shift = PostShift::kEvening;
    return true;
  }
  if (text == "bath_day") {
    shift = PostShift::kBathDay;
    return true;
  }
  return false;
}

/// @brief Whether the tick's hour [hour, hour + 1) overlaps [from, to).
constexpr bool HourOverlaps(std::uint32_t hour, float from, float to) {
  const auto start = static_cast<float>(hour);
  return start + 1.0F > from && start < to;
}

/// @brief Whether a holder of a post with `shift` stands at his unit at `hour`
///        of a day that is `weekday` with this daylight window.
constexpr bool InPostShift(PostShift shift,
                           Weekday weekday,
                           std::uint32_t hour,
                           const DayWindow& window) {
  const auto bedtime = static_cast<float>(kPostShiftBedtimeHour);
  switch (shift) {
    case PostShift::kWorkday:
      return HourOverlaps(hour, window.sunrise, window.sunset);
    case PostShift::kEvening:
      return HourOverlaps(hour, window.sunset, bedtime);
    case PostShift::kBathDay:
      return (weekday == Weekday::kSaturday && HourOverlaps(hour, window.sunset, bedtime)) ||
             (weekday == Weekday::kSunday && HourOverlaps(hour, window.sunrise, bedtime));
  }
  return false;
}

/// @brief Whether the post takes its holder off the accountant's daily list:
///        only a workday post does.
constexpr bool PostHoldsTheDay(PostShift shift) {
  return shift == PostShift::kWorkday;
}

}  // namespace core

#endif  // CORE_COMMON_POST_SHIFT_H_
