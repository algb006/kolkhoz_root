/// @file
/// @brief Where nothing may be built, and why (#1788; rules §13 «Что блокирует
/// размещение»; layers §6 «Где нельзя строить и почему»): the type-independent
/// bans of a building's placement, one bit each, read by the build order and by
/// the layer's map alike.
/// @threading PARALLEL_READONLY
/// Plain data.
///
/// THE BANS, as §13 writes them (boss, #1886, 18 Nov 2026):
///   - water: a channel, the lake, a pond, a backwater, shallows, and the
///     floodplain's CONTOUR («заливаемая пойма»; #1749: the contour stays the
///     no-building ground, whatever the drawn flood);
///   - forest: untouchable, except the hunting lodge;
///   - reserve: «only its own unit stands» - STUB, the own-unit exception never
///     fires: no reserve names its unit anywhere today (boss files the mapping);
///   - road: a building's contour on a laid road's surface (paths block nothing);
///   - plot: another unit's plot (the build order's kTooClose, the same circles);
///   - edge: within `edge_build_setback_m` of the map's edge - STUB while the
///     map rule is not exported (map.csv column; 0 does not fire).
/// NOT HERE, and not enforced by the core today: the slope (no heights in the
/// core), the stink zone for houses and the polluted ground water for drinking
/// wells (water design §3, §4; their own row), stony ground («по обстоятельствам»,
/// no rule).

#ifndef CORE_COMMON_PLACEMENT_BANS_H_
#define CORE_COMMON_PLACEMENT_BANS_H_

#include <cstdint>
#include <vector>

namespace core {

/// @brief One ban; a cell or a contour may carry several.
enum PlacementBan : std::uint8_t {
  kBanWater = 1U << 0U,    ///< Channel, lake, pond, backwater, shallows, floodplain.
  kBanForest = 1U << 1U,   ///< The forest (a hunting lodge may stand in it).
  kBanReserve = 1U << 2U,  ///< A reserve (its own unit's exception is a STUB).
  kBanRoad = 1U << 3U,     ///< A laid road's surface.
  kBanPlot = 1U << 4U,     ///< Another unit's plot.
  kBanEdge = 1U << 5U,     ///< The edge band.
};

/// @brief The whole map's bans, row-major from the south-west corner.
struct PlacementBansView {
  /// False from a bare engine, or a world without a map.
  bool measured = false;

  /// The cell's side, metres; `columns` cells a row and as many rows.
  float cell_m = 0.0F;
  std::uint32_t columns = 0;

  /// One byte a cell, PlacementBan bits: the ban at the cell's CENTRE.
  std::vector<std::uint8_t> cells;
};

}  // namespace core

#endif  // CORE_COMMON_PLACEMENT_BANS_H_
