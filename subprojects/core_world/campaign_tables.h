// Internal to core_world: shared readers of the campaign and life tables,
// defined in world.cpp, used by genesis.cpp.

#ifndef CORE_WORLD_CAMPAIGN_TABLES_H_
#define CORE_WORLD_CAMPAIGN_TABLES_H_

#include <string_view>

#include "core_common/calendar.h"

namespace core {

class ITableSet;

/// @brief The campaign-setup weekday of day 0; Monday when absent (STUB).
Weekday CampaignDayZeroWeekday(const ITableSet& tables);

/// @brief One numeric value from the campaign key/value table; `fallback`
/// when the table or the key is absent, logged error + fallback when the
/// cell is present but not numeric.
float CampaignValue(const ITableSet& tables, std::string_view key, float fallback);

/// @brief life_speedup from tables/life.csv; 4 when absent.
float LifeSpeedupFromTables(const ITableSet& tables);

}  // namespace core

#endif  // CORE_WORLD_CAMPAIGN_TABLES_H_
