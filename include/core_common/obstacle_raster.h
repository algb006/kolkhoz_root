/// @file
/// @brief The map's obstacles to a road, rasterised once (delivery 7b): a
///        grid of cells over the square map, each a set of flags — water,
///        forest, trees, floodplain, reserve, ruins, river — read by the
///        road tracer (road_trace.h) a sample at a time.
/// @threading PARALLEL_READONLY
/// Built once from MapObstacles (a pure function of them and the cell size),
/// read-only after.
///
/// WHY A RASTER AND NOT THE POLYGONS. The forest alone is some twenty
/// thousand vertices; a trace corner to corner is thousands of samples of
/// five points each. A point-in-polygon per sample against that is
/// milliseconds a preview does not have (ue's ~0.5 ms, boss [59] p.9). A cell
/// lookup is one multiply and one load. What it costs is memory — one byte a
/// cell, (side / cell)² bytes — and exactness: a boundary is right to half a
/// cell. kObstacleCellMetres is the STUB that trades the two, named in the
/// delivery with both prices.
///
/// A CELL IS FLAGGED WHEN ITS CENTRE IS INSIDE. The village's contour is not
/// here: it forbids nothing, and the tracer asks it by its polygons (three,
/// a dozen points).

#ifndef CORE_COMMON_OBSTACLE_RASTER_H_
#define CORE_COMMON_OBSTACLE_RASTER_H_

#include <cstdint>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/map_obstacles.h"

namespace core {

/// @brief The side of a raster cell, metres. STUB (boss [64] item 6): half
///        a road's bed is 4 m, so a boundary is placed to 1.25 m; the start
///        map (12 km) costs 4800² = 23 MB.
inline constexpr float kObstacleCellMetres = 2.5F;

/// @brief One flag of a cell. A cell may carry several: the river runs
///        through the floodplain, a pond lies in a grove.
enum ObstacleFlag : std::uint8_t {
  kObstacleWater = 1U << 0U,       ///< Lake, pond, backwater, shallows.
  kObstacleForest = 1U << 1U,      ///< Never cut for a road.
  kObstacleTrees = 1U << 2U,       ///< Grove or old orchard.
  kObstacleFloodplain = 1U << 3U,  ///< Only a path or a dirt road.
  kObstacleReserve = 1U << 4U,
  kObstacleRuins = 1U << 5U,
  kObstacleRiver = 1U << 6U,  ///< The river's channel: crossed only at a ford.
};

/// @brief The rasterised obstacles of one map.
class ObstacleRaster {
 public:
  /// @brief An empty raster: every cell clear, no map. What a table set
  ///        without map_areas and map_lines gets.
  ObstacleRaster() = default;

  /// @brief Rasterises `obstacles` over a square map of `side_m` metres.
  /// @param cell_m The cell's side; kObstacleCellMetres outside tests.
  /// @pre side_m > 0 and cell_m > 0.
  ObstacleRaster(const MapObstacles& obstacles, float side_m, float cell_m);

  /// @brief The flags of the cell holding `point`; 0 off the map.
  std::uint8_t FlagsAt(Vec2 point) const;

  /// @brief Whether anything was rasterised at all — false for the empty
  ///        raster, and the tracer then says it saw no map.
  bool HasMap() const { return columns_ > 0; }

  float CellMetres() const { return cell_m_; }

  float SideMetres() const { return side_m_; }

  /// @brief How many cells carry `flag`, for a test's count.
  std::uint64_t CountCells(ObstacleFlag flag) const;

  /// @brief Bytes the cells take — the memory price of the cell size.
  std::uint64_t Bytes() const { return cells_.size(); }

 private:
  void FillPolygon(const std::vector<Vec2>& outline, std::uint8_t flag);
  void FillChannel(const MapLineDef& line, std::uint8_t flag);

  float side_m_ = 0.0F;
  float cell_m_ = kObstacleCellMetres;
  std::uint32_t columns_ = 0;        ///< And as many rows: the map is square.
  std::vector<std::uint8_t> cells_;  ///< Row-major, row 0 the southmost.
};

/// @brief The flag an area of `kind` sets; 0 for the village's contour,
///        which forbids nothing.
std::uint8_t ObstacleFlagOf(MapAreaKind kind);

}  // namespace core

#endif  // CORE_COMMON_OBSTACLE_RASTER_H_
