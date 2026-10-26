/// @file
/// @brief The week's labour book (#647; office.md §15 «Строка недели: кто
///        расставлял людей»; registry №310 P3): man-days on the kolkhoz's
///        work by who placed them, and man-days of the able-bodied on no
///        kolkhoz work - which, in the core's own arithmetic, are the hours
///        that feed the household plot (household_plot.cpp) - for the
///        running calendar week and the last closed one.
/// @threading SINGLE_THREADED
/// Written by core_labor at the day's close, before the day's work is cleared
/// (CloseDay); read by the session's WeekLabourBook door between steps.

#ifndef CORE_COMMON_WEEK_LABOUR_H_
#define CORE_COMMON_WEEK_LABOUR_H_

#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// @brief One week's sums, in TENTHS of a man-day (a whole working day of one
///        person is 10). A day off counts nothing on any line.
struct WeekLabourSums {
  /// The week's first counted day, the campaign day: its Monday, or the
  /// campaign's day 0 for a first week that began mid-week.
  SimDay first_day = 0;
  /// Placed by the tally clerk. 0 IN EVERY WEEK WHILE THE CORE HAS NO CLERK
  /// (boss, core-boss-647 [2], office.md «без учётчика M = 0»): the morning
  /// automation is the chairman's own path then. STUB; returns with #589,
  /// the clerk's post.
  std::uint32_t ordered_by_clerk_tenths = 0;
  /// Placed on the chairman's path: every work placed that day, and every
  /// post held, while no clerk exists.
  std::uint32_t ordered_by_chairman_tenths = 0;
  /// The able-bodied (core_labor's Employable: of working age, with a home,
  /// not away in the district's hospital) on a working day with no kolkhoz
  /// work and no post - the drift, which feeds the household plot.
  std::uint32_t home_plot_tenths = 0;
};

/// @brief The running week and the last closed one. Save 190.
struct WeekLabourBook {
  WeekLabourSums running;
  WeekLabourSums last_closed;
  /// 0 until the first week has closed: the door then says «not yet».
  std::uint8_t closed_known = 0;
};

/// @brief What the office's week line reads (ISession::WeekLabourBook): the
///        last CLOSED calendar week, in man-days.
struct WeekLabourView {
  /// false before the first week has closed; every number is 0 then.
  bool known = false;
  /// The closed week's Monday, the campaign day.
  SimDay week_first_day = 0;
  float ordered_by_clerk_md = 0.0F;     ///< M: 0 while the core has no clerk (#589)
  float ordered_by_chairman_md = 0.0F;  ///< K
  float home_plot_md = 0.0F;            ///< P: «На своём хозяйстве»
};

/// @brief The view of a book: tenths to man-days.
constexpr WeekLabourView WeekLabourViewOf(const WeekLabourBook& book) {
  constexpr float kTenth = 0.1F;
  if (book.closed_known == 0) {
    return {};
  }
  return WeekLabourView{
      .known = true,
      .week_first_day = book.last_closed.first_day,
      .ordered_by_clerk_md = static_cast<float>(book.last_closed.ordered_by_clerk_tenths) * kTenth,
      .ordered_by_chairman_md =
          static_cast<float>(book.last_closed.ordered_by_chairman_tenths) * kTenth,
      .home_plot_md = static_cast<float>(book.last_closed.home_plot_tenths) * kTenth,
  };
}

}  // namespace core

#endif  // CORE_COMMON_WEEK_LABOUR_H_
