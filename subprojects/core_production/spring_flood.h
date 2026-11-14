/// @file
/// @brief THE SPRING FLOOD (#1375; livestock.md, the row «Половодье забирает то, что стоит на
///        пойме» - the human's «Да» of 1 November 2026, #840, #1561; boss,
///        core-boss-1375-flood-contract [2], the six points answered).
/// @threading SINGLE_THREADED
/// Called from core_production's sequential slot of the step, once a day; the alarm is a read
/// of a completed state.
///
/// THE RULE, as the design and the contract say it:
///   * WHEN: from the second spring, on or after 1 March, the first day the village's snow
///     cover lies no more (WeatherState::snow_cover_days falls from above 0 to 0, or is 0 on
///     1 March already); 1 May whatever the cover (STUB kFloodFallbackMonth). The design's
///     depth threshold `flood_snow_cover` is dropped: the core keeps the cover as days, and the
///     melt clears it whole (weather_of_day.cpp).
///   * WHAT: everything lying on floodplain ground that day - (a) the hay and straw heap of
///     every field of kind kFloodplainMeadow and (b) the hay and straw of every unit whose
///     position's cell carries kObstacleFloodplain. Nothing above the floodplain. All of it,
///     with no share and no draw, booked to YearLedger::lost_to_flood.
///   * THE WATER stands FarmingConfig::flood_water_days (STUB 8) - FloodState.
///   * THE LAMP: AlarmKind::kFloodplainFodderAhead from 1 December to the flood day, while
///     (a)+(b) holds a gram (alarm_state.h).
///   * THE DEADLINE: the projected flood day (core_common/flood_state.h) is the carting
///     deadline of a floodplain meadow's heap for the heaps' readers (#1200's interlock).

#ifndef CORE_PRODUCTION_SPRING_FLOOD_H_
#define CORE_PRODUCTION_SPRING_FLOOD_H_

#include <cstdint>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/flood_state.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The fodder lying on floodplain ground today, by resource (hay and straw), and what
///        holds it.
struct FloodplainFodder {
  ResourceAmounts grams;              ///< Hay and straw together, by resource.
  Grams total = 0;                    ///< The sum of `grams`.
  std::uint32_t meadow_heaps = 0;     ///< (a) kFloodplainMeadow fields with a heap.
  std::uint32_t stacks_on_flood = 0;  ///< (b) units on a floodplain cell holding hay or straw.
  std::uint32_t stacks_above = 0;     ///< Units holding hay or straw OFF the floodplain (the
                                      ///< design's way out: «скирда выше линии паводка»).
};

/// @brief What lies on floodplain ground today (the rule's WHAT).
/// @return Zero everywhere with no floodplain meadow, no floodplain cell or no fodder there.
FloodplainFodder FloodplainFodderOf(const ProductionConfig& config, const WorldState& world);

/// @brief Whether a unit lies on floodplain ground for the flood: its position's cell carries
///        kObstacleFloodplain in the map's obstacle raster (config.meadow_ground). False with
///        no map.
bool UnitFloods(const ProductionConfig& config, const WorldState& world, UnitId unit);

/// @brief Whether today is this year's flood day (the rule's WHEN).
/// @param previous Yesterday's completed state (its snow_cover_days).
/// @param current Today's state after the weather phase.
bool IsFloodDay(const WorldState& previous, const WorldState& current);

/// @brief The flood itself, once a day: on the flood day every gram in FloodplainFodderOf is
///        taken from its field heap or unit store and booked to YearLedger::lost_to_flood;
///        FloodState::last_flood_year and water_until_day are written. No-op on any other day.
/// @param previous Yesterday's completed state (the cover's fall is read against it).
/// @param current The state being written (fields, units, ledger, flood).
/// Side effects: fields' heaps and units' stocks of hay and straw on floodplain ground go to 0.
void RunSpringFlood(const ProductionConfig& config,
                    const WorldState& previous,
                    WorldState& current);

/// @brief AlarmKind::kFloodplainFodderAhead, appended to `alarms` from 1 December to the flood
///        day while FloodplainFodderOf(...).total > 0 (alarm_state.h has the fields).
void CollectFloodAlarms(const ProductionConfig& config,
                        const WorldState& world,
                        std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_SPRING_FLOOD_H_
