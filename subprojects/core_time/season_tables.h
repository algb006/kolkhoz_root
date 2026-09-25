/// @file
/// @brief The season table out of a table set — core_time's one reading of
///        the climate, shared by the time phase and the month doors.
/// @threading PARALLEL_READONLY
/// Module-internal: a pure function of the table set.

#ifndef CORE_TIME_SEASON_TABLES_H_
#define CORE_TIME_SEASON_TABLES_H_

#include <string>

#include "weather_of_day.h"

namespace core {

class ITableSet;

/// @brief The stub seasons, then `weather` and `weather_params` over them
///        where present.
/// @param seasons Overwritten.
/// @param error Why a present table did not parse.
/// @return false when a present table does not parse.
bool ReadSeasonTable(const ITableSet& tables, SeasonTable& seasons, std::string& error);

}  // namespace core

#endif  // CORE_TIME_SEASON_TABLES_H_
