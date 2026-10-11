/// @file
/// @brief The works that will house a herd facing the cold: the allocators'
/// default rank from August to February (boss, 11 October 2026, on the warm
/// trace of routing stage В2; livestock design, the line boss wrote then).
/// @threading PARALLEL_READONLY: a pure read of the world and the tables'
/// data; called by labour's morning allocation and logistics' plan, both
/// sequential.
///
/// WHY. With the materials carted (0.37.220) the first pen's rung 1 stood
/// open eight days with nobody on it while 25-33 builders worked older sites
/// (seed 1931, days 37-44, October) and its warm waited its carts behind
/// every other works: the cows froze in nine villages of nine. The bot's warm
/// pause (tests/run) helped only the bot and only the warm. The rule here is
/// the ALLOCATORS' DEFAULT, the player's and the bot's alike: such works go
/// first among the construction for builders and first among the supply for
/// carts (level 1, behind the harvest's carting, econ's form (a)). It imposes
/// nothing: «порядок строек не навязан» (livestock design), and the player's
/// explicit assignment of builders to a site always wins over it - his
/// standing work orders override the morning's placement after the
/// allocator has made it (core_labor, work_orders.h, ApplyStandingWork).

#ifndef CORE_COMMON_COLD_HOUSING_H_
#define CORE_COMMON_COLD_HOUSING_H_

#include <cstdint>
#include <vector>

#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace core {

/// The window, 0-based months inclusive and wrapping the year's turn: August
/// to FEBRUARY. STUB. It was August to November (the bot's warm window) in
/// this version's first tree, and the straw coat a pen took in October
/// lost its rank on 1 December and took 15 days for 5 man-days (seed 1934,
/// days 36-51) while its cows froze — the cold season is when the rank
/// matters most.
inline constexpr std::uint8_t kColdHousingFirstMonth = 7;
inline constexpr std::uint8_t kColdHousingLastMonth = 1;

/// @brief What the rule reads of the tables (core_catalog/cold_housing_
/// catalog.h fills it): a kind's home unit and whether it freezes in a cold
/// place (livestock.csv home_unit, cold_night_cold_place_c), and which rungs
/// of a unit type are warm (unit_levels.csv warm_place).
struct ColdHousing {
  std::vector<UnitTypeId> home_by_kind;  ///< By LivestockKindId; invalid: none.

  std::vector<std::uint8_t> freezes_by_kind;  ///< By LivestockKindId; 1: freezes in a cold place.

  /// By UnitTypeId, then by level (index = level): 1 when that rung is warm.
  std::vector<std::vector<std::uint8_t>> warm_by_type_level;
};

/// @brief Whether the open works on `unit_row` will house a herd facing the
///        cold: today's month is in the window, the unit is the home of a
///        kind that freezes in a cold place, a kolkhoz herd of that kind
///        stands, and no unit of that home stands at a warm rung. The works
///        are the home's rung 1 or its warm alike - the pen must stand
///        before it can be warmed.
/// @return false for a unit row out of range, a works not open, or a table
///         set that gave no data.
bool WorksHouseAColdHerd(const ColdHousing& cold, const WorldState& world, std::uint32_t unit_row);

}  // namespace core

#endif  // CORE_COMMON_COLD_HOUSING_H_
