/// @file
/// @brief What on the map a road may not cross or may cross only at a place
///        (roads design §9, «Свободная прокладка»; delivery 7b): the areas
///        of tables/map_areas.csv and the water lines of tables/map_lines.csv,
///        as plain data. core_catalog reads them (map_obstacles_reader.h);
///        the tracer (road_trace.h) rasterises them once.
/// @threading PARALLEL_READONLY
/// Plain data, filled once from the table set and read-only after.

#ifndef CORE_COMMON_MAP_OBSTACLES_H_
#define CORE_COMMON_MAP_OBSTACLES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core_common/geometry.h"

namespace core {

/// @brief What an area of map_areas.csv is. The file's `kind` column, one to
///        one; an unknown word is a read error, never a silent pass.
enum class MapAreaKind : std::uint8_t {
  kLake = 0,
  kPond,
  kBackwater,
  kShallows,    ///< Shelf water, not a ford: water to a road.
  kForest,      ///< Never cut for a road (roads design §9).
  kFloodplain,  ///< A path and a dirt road cross it; gravel and asphalt do not (§11а).
  kGrove,       ///< Trees: in a path's and a dirt road's way; cleared by gravel and asphalt.
  kOldOrchard,  ///< Trees, as a grove.
  kReserve,     ///< Protected: no road.
  kRuinsSite,   ///< Ruins: no road.
  /// NOT AN OBSTACLE: the village's contour, the only place asphalt with
  /// walks is laid (roads design §9; boss [64], exported 2026-09-26).
  kVillageZone,
  kMapAreaKindCount,  ///< NOT A KIND: the count, for mirrors.
};

/// @brief One closed area, as the plan sheet draws it: the outline in order,
///        the last point joined back to the first.
struct MapAreaDef {
  std::string key;
  MapAreaKind kind = MapAreaKind::kLake;
  std::vector<Vec2> outline;
};

/// @brief What a water line of map_lines.csv is.
enum class MapLineKind : std::uint8_t {
  kRiver = 0,         ///< Crossed only at a ford (and on the map's own bridges).
  kBrook,             ///< Fordable anywhere: STUB of the export, «Через ручьи есть броды».
  kMapLineKindCount,  ///< NOT A KIND: the count, for mirrors.
};

/// @brief What stands at a point of a water line. Only kFord changes what a
///        road may do; the rest are read so an unknown word is an error.
enum class MapLineMark : std::uint8_t {
  kNone = 0,
  kFord,              ///< A road may cross the river here.
  kDam,               ///< On a brook (fordable anyway).
  kArch,              ///< The stone arch under a map road.
  kOther,             ///< mouth, pond, pool, spring, fall: places, not crossings.
  kMapLineMarkCount,  ///< NOT A MARK: the count, for mirrors.
};

/// @brief One point of a water line: where, half the channel's width there,
///        and its mark.
struct MapLinePoint {
  Vec2 position;
  float half_width_m = 0.0F;
  MapLineMark mark = MapLineMark::kNone;
};

/// @brief One river or brook, point by point in `seq` order.
struct MapLineDef {
  std::string key;
  MapLineKind kind = MapLineKind::kBrook;
  std::vector<MapLinePoint> points;
};

/// @brief Everything map_areas.csv and map_lines.csv say, in file order.
struct MapObstacles {
  std::vector<MapAreaDef> areas;
  std::vector<MapLineDef> lines;
};

}  // namespace core

#endif  // CORE_COMMON_MAP_OBSTACLES_H_
