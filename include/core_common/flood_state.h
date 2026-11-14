/// @file
/// @brief The spring flood's two numbers (#1375; livestock.md, the row «Половодье забирает то,
///        что стоит на пойме», the human's «Да» of 1 November 2026; boss,
///        core-boss-1375-flood-contract [2]).
/// @threading SINGLE_THREADED
/// Written once a year by core_production's flood rule (spring_flood.h, RunSpringFlood),
/// read by everything else from the completed state.

#ifndef CORE_COMMON_FLOOD_STATE_H_
#define CORE_COMMON_FLOOD_STATE_H_

#include <cstdint>

#include "core_common/calendar.h"

namespace core {

/// @brief What the flood left in the world: when it came last, and until when its water
///        stands on the floodplain. Save 211.
struct FloodState {
  /// The calendar year (1-based) of the last flood; 0 before the first. A flood comes at most
  /// once a year, and never in year 1 (the design: «первой весной паводка нет»).
  std::uint16_t last_flood_year = 0;

  /// The first day the floodplain is dry again: the flood day plus
  /// FarmingConfig::flood_water_days. While `calendar.day` is below it the water stands
  /// (the layer paints it; nothing is laid on floodplain ground). 0 when no water stands.
  SimDay water_until_day = 0;
};

}  // namespace core

#endif  // CORE_COMMON_FLOOD_STATE_H_
