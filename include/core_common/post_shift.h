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
/// numbers: the working day ends at SUNSET — the labor model's working window
/// is the daylight — and bedtime is kPostShiftBedtimeHour. The reading hut's
/// evening is shorter: "до темноты" is sunset plus kEveningDuskHours of dusk
/// and a kerosene lamp, and with electricity it lasts until bedtime (boss,
/// parcel 251, a number assigned and not measured). "The length of the
/// evening is a live signal" (social units §1), so the two must differ; the
/// core has no electricity yet, and the evening is the dusk one always
/// (STUB). The bath day's Saturday evening runs until bedtime.

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

  /// The night: from sunset to sunrise, every night, weekends too — the
  /// watchman (crime design §11) and, if the tables ever carry one, the
  /// firefighter (time design: the only two night posts). Takes the holder
  /// off the day's list: "сторож не выходит на другие работы". Each night's
  /// start is said (EventKind::kPostShiftStarted). Boss, parcel 360.
  kNight,
};

/// @brief The hour the village goes to bed: evening shifts end at it.
inline constexpr std::uint32_t kPostShiftBedtimeHour = 22;

/// @brief Hours an evening shift lasts past sunset without electricity: dusk
///        and a kerosene lamp. STUB: always, until the core has electricity.
inline constexpr float kEveningDuskHours = 1.5F;

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
  // The night shift was read as a workday from parcel 274 until its door
  // (boss, parcel 360): the watchman now has his night.
  if (text == "night") {
    shift = PostShift::kNight;
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
      return HourOverlaps(hour, window.sunset, window.sunset + kEveningDuskHours);
    case PostShift::kBathDay:
      return (weekday == Weekday::kSaturday && HourOverlaps(hour, window.sunset, bedtime)) ||
             (weekday == Weekday::kSunday && HourOverlaps(hour, window.sunrise, bedtime));
    case PostShift::kNight:
      // Sunset to sunrise. The window is this day's on both sides of
      // midnight: the night's two halves belong to two days whose daylight
      // differs by minutes, and asking the next day's sunrise would be a
      // precision the hour cannot show.
      return HourOverlaps(hour, window.sunset, static_cast<float>(kTicksPerDay)) ||
             HourOverlaps(hour, 0.0F, window.sunrise);
  }
  return false;
}

/// @brief Whether the post takes its holder off the accountant's daily list:
///        a workday post does, and so does a night post — "сторож не выходит
///        на другие работы" (crime design §11).
constexpr bool PostHoldsTheDay(PostShift shift) {
  return shift == PostShift::kWorkday || shift == PostShift::kNight;
}

}  // namespace core

#endif  // CORE_COMMON_POST_SHIFT_H_
