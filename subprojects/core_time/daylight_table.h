// Internal to core_time: the solar daylight curve, precomputed.
//
// Daylight by date at the fixed campaign latitude 55.75° N (time design §3:
// June 17.5 game hours, equinoxes ~12.2, December 7.0). Structural, not
// balance — the latitude is part of the setting, so the curve lives in code,
// not in a table (manual/61-balance-tables.md, the CSV/constexpr split).
//
// Precomputed rather than done with libm at run time on purpose: bit-for-bit
// determinism across compilers is a hard requirement, and trig library
// results are not guaranteed to the last ulp. Values were generated once
// with the standard sunrise equation (declination from the day of year,
// -0.83° refraction altitude), mapping game day i (month m, day d) to the
// real year as (m + (d + 0.5) / 4) / 12 * 365.25 days.

#ifndef CORE_TIME_DAYLIGHT_TABLE_H_
#define CORE_TIME_DAYLIGHT_TABLE_H_

#include <array>
#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// Daylight in game hours for each day of the 48-day year, January first.
inline constexpr std::array<float, kDaysPerYear> kDaylightGameHours = {
    7.1892F,  7.4434F,  7.7872F,  8.2038F,  8.6773F,  9.1934F,  9.7404F,  10.3092F,
    10.8928F, 11.4860F, 12.0850F, 12.6863F, 13.2868F, 13.8828F, 14.4695F, 15.0407F,
    15.5878F, 16.0994F, 16.5611F, 16.9557F, 17.2641F, 17.4682F, 17.5538F, 17.5144F,
    17.3530F, 17.0814F, 16.7166F, 16.2777F, 15.7828F, 15.2474F, 14.6840F, 14.1022F,
    13.5090F, 12.9098F, 12.3084F, 11.7082F, 11.1124F, 10.5247F, 9.9497F,  9.3936F,
    8.8648F,  8.3740F,  7.9344F,  7.5616F,  7.2722F,  7.0819F,  7.0024F,  7.0390F,
};

}  // namespace core

#endif  // CORE_TIME_DAYLIGHT_TABLE_H_
