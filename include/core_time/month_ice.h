/// @file
/// @brief The ice of a MONTH — how much of the water has set and how much has
///        opened again — read from the balance tables without a world.
/// @threading PARALLEL_READONLY
/// A pure function of the table set and the month: no world, no simulation,
/// no state. Callable from any thread, and from outside the simulation.
///
/// WHY IT IS PUBLIC (boss, core-boss-epoch1-6 [41]; the human's word on the
/// next demo build: «Делайте все» — snow and ice in it). The layer lays ice
/// on the water by the mask ice-order.png: R the order the water sets in, G
/// the order it opens. Two shares say how far along each order the month
/// is; they are the world's state — the winter road (roads design §16) and
/// the fords (§15) live by them — so the core counts them and the layer
/// reads them. The rapids (B) never set; the layer reads that off the mask.
///
/// OPENING IS NOT SETTING BACKWARDS (map design, «Лёд, вскрытие и ледоход»):
/// a river opens down its course, a lake from the shore, so the thaw is its
/// own quantity and not the freeze run in reverse.
///
/// THE RULE, walked day by day by the time phase's own weather and snow
/// (WeatherOfDay, SnowCoverAfter), kMonthClimateYears years of one seed after
/// a warm-up year — the month climate door's walk (month_climate.h):
///   - every day whose mean is below nought adds its frost to the winter's
///     degree-days; `freeze` is that sum over world_params
///     `ice_freeze_full_degree_days` (30), up to 1;
///   - a day above nought BEFORE the winter's first snow melts the young ice
///     back (the sum falls); AFTER a cover has lain, a day above nought with
///     no snow lying adds to the thaw, once some ice has set; `thaw` is that
///     sum over `ice_thaw_full_degree_days` (40);
///   - the day the thaw reaches 1 the ice is gone, and a new winter begins.
/// On the game's tables, mid-month: November 0.09 set, December 0.58,
/// January to March 1; March 0.13 open, April 0.56, May gone.
/// Degree-days are °C × GAME days (48 a year). STUB, both fulls: the order is
/// the mask's and the time the weather's, the two numbers are the default
/// the design base holds with its reason.

#ifndef CORE_TIME_MONTH_ICE_H_
#define CORE_TIME_MONTH_ICE_H_

#include <span>
#include <string>
#include <string_view>

#include "core_common/calendar.h"

namespace core {

class ITableSet;

/// @brief A month's ice, as shares of the mask's two orders, 0..1.
struct MonthIce {
  /// How far along the setting order (the mask's R) the water has set by
  /// the middle of the month — the median over the years.
  float freeze = 0.0F;

  /// How far along the opening order (the mask's G) it has opened again —
  /// the median over the years. Read with `freeze`: ice lies where the
  /// water has set and has not opened.
  float thaw = 0.0F;
};

/// @brief The world_params.csv keys of the ice (for the assembly's
///        declared-readers check).
std::span<const std::string_view> IceWorldParamKeys();

/// @brief The ice of `month`, on its third day of four (the middle), from the
///        weather and world tables of `tables`.
/// @param ice Written on success, untouched on failure.
/// @param error Why it failed: no weather table, a malformed one, or an ice
///        key out of its range.
/// @return false on failure.
bool MonthIceOfTables(const ITableSet& tables, Month month, MonthIce& ice, std::string& error);

}  // namespace core

#endif  // CORE_TIME_MONTH_ICE_H_
