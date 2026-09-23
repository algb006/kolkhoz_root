/// @file
/// @brief Rain stops the sowing and the reaping (farming design §5, «Погода
/// останавливает работу, а не портит её»), and the climate's share of rain
/// days that a forecast of the work has to discount.
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments: no state, no synchronization, readable
/// from any phase and any thread.
///
/// WHY IT IS SHARED. Three places must agree on one rule: the accountant who
/// does not send a crew to a rained-out field, the standing order that does
/// not hold a man on one, and the MTS column whose combine stops like the
/// scythe. And two places must agree on one expectation: the gathering alarm
/// and labor's last days before the snow both turn "norm-days owed" into
/// "calendar days needed", and until rain stopped the work a norm-day of pace
/// was a calendar day. A private copy in either module would drift the day
/// the rule grows a kind.
///
/// THE WORK IS STOPPED, NOT SPOILED. What is already reaped lies at the field
/// untouched (boss, boss-core-epoch1-resume seq 3: the design's §6 line on
/// grain suffering on the open floor was rewritten to agree with §5). The
/// loss rain causes is the lost days, which the snow then collects.

#ifndef CORE_COMMON_RAIN_STOPS_WORK_H_
#define CORE_COMMON_RAIN_STOPS_WORK_H_

#include <array>

#include "core_common/calendar.h"
#include "core_common/labor_state.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Whether a day of this precipitation stops this kind of work.
///
/// Any rain of any strength stops the sowing and the reaping, and wet snow
/// between −1 and +1 is rain here because the generator already writes it
/// as Precipitation::kRain (core_time/weather_of_day.cpp). The meadow's cut
/// is WorkKind::kHarvest too, and is stopped with it — the design's «как и
/// косца». Ploughing and harrowing are not: their stops are frozen ground and
/// mud, which have rules of their own.
/// @param precipitation The day's precipitation (WorldState::weather).
/// @param kind          The work in question.
/// @return true when the work does not go on this day.
bool RainStopsWork(Precipitation precipitation, WorkKind kind);

/// @brief Whether the core's winter season stops this building site.
///
/// THE SEASON'S HALF OF "WHAT STOPS THE WORK", beside the rain's, for the
/// same three readers — the accountant, the standing order, and whoever asks
/// what a site is doing. Construction design §8 «Сезонность»: «кладка и
/// земляные работы стоят, плотницкие идут»; the class is resolved into the
/// site's `winter_works` byte when the site starts (unit_state.h). WINTER IS
/// THE CORE'S WINTER SEASON, December to February (SeasonOfMonth); boss
/// named no second definition (seq 8).
/// @param season The day's season (CalendarState::season).
/// @param winter_works The site's byte: 0 stands in winter, anything else goes.
bool WinterStopsSite(Season season, std::uint8_t winter_works);

/// @brief The expected share of rain days on each day of the year, 0..1 —
/// the climate's answer and not the seed's (core_time/time_system.h,
/// ITimeSystem::ClimateRainDayShares). All zeros means "rain never stops the
/// work", which is what a build without a weather table and every unit test
/// that does not ask for rain get.
using RainDayShares = std::array<float, kDaysPerYear>;

/// @brief Expected dry days in the calendar span [from, to), both in days of
/// the year and fractional: a part of a day counts its part of that day's
/// dry share. Days past the year's end wrap onto the next year's shares.
/// @return 0 when `to` is not after `from`.
double DryDaysBetween(const RainDayShares& shares, double from, double to);

/// @brief The calendar point, in days of the year from the same origin as
/// `from`, by which `dry_days` expected dry days have gone by. The inverse
/// of DryDaysBetween over the span it returns.
/// @return `from` for no dry days owed; +infinity when the shares say it
///         rains every day for a whole year after `from`, which no climate
///         table in the design does.
double CalendarPointAfterDryDays(const RainDayShares& shares, double from, double dry_days);

}  // namespace core

#endif  // CORE_COMMON_RAIN_STOPS_WORK_H_
