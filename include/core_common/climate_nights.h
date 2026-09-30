/// @file
/// @brief The climate's mean night of each day of the year — the one figure
///        the cold ladder plans against (Livestock design, «Числа лестницы —
///        Эпоха I»; 0.37.61).
/// @threading PARALLEL_READONLY
/// A value type: an array, read by anyone, written once by the assembly.
///
/// THE CLIMATE'S ANSWER AND NOT THE SEED'S, for the rain days' reason
/// (core_common/rain_stops_work.h, RainDayShares): the autumn's yellow «к
/// зиме площадка не укроет» counts the days to the first cold night by the
/// climate — a forecast the chairman can make — and the months in which the
/// billet keeps its places before the pen are the climate's months, not the
/// drawn weather's (boss-core-start-no-yards [18], [19]).
///
/// core_time OWNS THE CURVE (ITimeSystem::ClimateNightCelsius): the season
/// means and the interpolation between their centre days live there, and a
/// second copy of the interpolation in core_production is the drift this
/// project keeps finding. The assembly hands the array in, as it hands the
/// growing season's last day and the rain days.
#ifndef CORE_COMMON_CLIMATE_NIGHTS_H_
#define CORE_COMMON_CLIMATE_NIGHTS_H_

#include <array>

#include "core_common/calendar.h"

namespace core {

/// @brief Degrees Celsius per day of the year: the seasonal mean minus the
/// season's half-swing, the mean night on the camera design's terms («ночь =
/// среднее − размах»). A day's own sky widens or narrows its swing; the
/// season's multiplier is one by construction, so this is the typical night.
using ClimateNights = std::array<float, kDaysPerYear>;

}  // namespace core

#endif  // CORE_COMMON_CLIMATE_NIGHTS_H_
