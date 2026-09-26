#include "core_common/obstacle_raster.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace core {
namespace {

/// The first cell index whose centre is at or past `metres` along an axis:
/// centres sit at (index + 0.5) * cell.
std::int64_t FirstCentreAtOrAfter(float metres, float cell_m) {
  return static_cast<std::int64_t>(std::ceil((metres / cell_m) - 0.5F));
}

}  // namespace

std::uint8_t ObstacleFlagOf(MapAreaKind kind) {
  switch (kind) {
    case MapAreaKind::kLake:
    case MapAreaKind::kPond:
    case MapAreaKind::kBackwater:
    case MapAreaKind::kShallows:
      return kObstacleWater;
    case MapAreaKind::kForest:
      return kObstacleForest;
    case MapAreaKind::kGrove:
    case MapAreaKind::kOldOrchard:
      return kObstacleTrees;
    case MapAreaKind::kFloodplain:
      return kObstacleFloodplain;
    case MapAreaKind::kReserve:
      return kObstacleReserve;
    case MapAreaKind::kRuinsSite:
      return kObstacleRuins;
    case MapAreaKind::kVillageZone:
    case MapAreaKind::kStoneQuarry:  // a pit wants its road, not a refusal
    case MapAreaKind::kClayPit:
    case MapAreaKind::kSandPit:
    case MapAreaKind::kMapAreaKindCount:
      return 0;
  }
  return 0;
}

ObstacleRaster::ObstacleRaster(const MapObstacles& obstacles, float side_m, float cell_m)
    : side_m_(side_m), cell_m_(cell_m) {
  assert(side_m > 0.0F && cell_m > 0.0F);
  columns_ = static_cast<std::uint32_t>(std::ceil(side_m / cell_m));
  cells_.assign(static_cast<std::size_t>(columns_) * columns_, 0);
  for (const MapAreaDef& area : obstacles.areas) {
    const std::uint8_t flag = ObstacleFlagOf(area.kind);
    if (flag != 0) {
      FillPolygon(area.outline, flag);
    }
  }
  for (const MapLineDef& line : obstacles.lines) {
    // Only the river stops a road; a brook is fordable anywhere (the
    // export's STUB), so it is not an obstacle and is not drawn.
    if (line.kind == MapLineKind::kRiver) {
      FillChannel(line, kObstacleRiver);
    }
  }
}

std::uint8_t ObstacleRaster::FlagsAt(Vec2 point) const {
  if (columns_ == 0 || point.x < 0.0F || point.y < 0.0F || point.x >= side_m_ ||
      point.y >= side_m_) {
    return 0;
  }
  const auto column = std::min(static_cast<std::uint32_t>(point.x / cell_m_), columns_ - 1U);
  const auto row = std::min(static_cast<std::uint32_t>(point.y / cell_m_), columns_ - 1U);
  return cells_[(static_cast<std::size_t>(row) * columns_) + column];
}

std::uint64_t ObstacleRaster::CountCells(ObstacleFlag flag) const {
  return static_cast<std::uint64_t>(std::count_if(
      cells_.begin(), cells_.end(), [flag](std::uint8_t cell) { return (cell & flag) != 0; }));
}

/// Even-odd scanline over the rows the outline spans: every edge drops the
/// x where it crosses a row's centre line into that row's bucket, then each
/// row fills between pairs of crossings. The bucket is per ROW of the
/// outline's own box, so an edge costs the rows it spans and nothing more —
/// the forest's twenty thousand short edges are about as many pushes.
void ObstacleRaster::FillPolygon(const std::vector<Vec2>& outline, std::uint8_t flag) {
  if (outline.size() < 3) {
    return;  // no area: the reader refuses these, a test may not
  }
  float low_y = outline.front().y;
  float high_y = outline.front().y;
  for (const Vec2& point : outline) {
    low_y = std::min(low_y, point.y);
    high_y = std::max(high_y, point.y);
  }
  const std::int64_t last_row = static_cast<std::int64_t>(columns_) - 1;
  const std::int64_t row_from = std::max<std::int64_t>(0, FirstCentreAtOrAfter(low_y, cell_m_));
  const std::int64_t row_to = std::min(last_row, FirstCentreAtOrAfter(high_y, cell_m_) - 1);
  if (row_from > row_to) {
    return;
  }
  std::vector<std::vector<float>> crossings(static_cast<std::size_t>(row_to - row_from + 1));
  for (std::size_t index = 0; index < outline.size(); ++index) {
    const Vec2 from = outline[index];
    const Vec2 to = outline[(index + 1) % outline.size()];
    if (from.y == to.y) {
      continue;  // a horizontal edge crosses no centre line
    }
    const float edge_low = std::min(from.y, to.y);
    const float edge_high = std::max(from.y, to.y);
    // Half-open, [low, high): a vertex shared by two edges is counted once.
    const std::int64_t first = std::max(row_from, FirstCentreAtOrAfter(edge_low, cell_m_));
    const std::int64_t last = std::min(row_to, FirstCentreAtOrAfter(edge_high, cell_m_) - 1);
    const float slope = (to.x - from.x) / (to.y - from.y);
    for (std::int64_t row = first; row <= last; ++row) {
      const float centre_y = (static_cast<float>(row) + 0.5F) * cell_m_;
      crossings[static_cast<std::size_t>(row - row_from)].push_back(from.x +
                                                                    ((centre_y - from.y) * slope));
    }
  }
  const std::int64_t last_column = static_cast<std::int64_t>(columns_) - 1;
  for (std::int64_t row = row_from; row <= row_to; ++row) {
    std::vector<float>& xs = crossings[static_cast<std::size_t>(row - row_from)];
    std::sort(xs.begin(), xs.end());
    for (std::size_t pair = 0; pair + 1 < xs.size(); pair += 2) {
      const std::int64_t column_from =
          std::max<std::int64_t>(0, FirstCentreAtOrAfter(xs[pair], cell_m_));
      const std::int64_t column_to =
          std::min(last_column, FirstCentreAtOrAfter(xs[pair + 1], cell_m_) - 1);
      for (std::int64_t column = column_from; column <= column_to; ++column) {
        cells_[(static_cast<std::size_t>(row) * columns_) + static_cast<std::size_t>(column)] |=
            flag;
      }
    }
  }
}

/// The channel as a chain of capsules: a cell is in when its centre lies
/// within the half-width, interpolated along the segment, of the segment.
void ObstacleRaster::FillChannel(const MapLineDef& line, std::uint8_t flag) {
  const std::int64_t last = static_cast<std::int64_t>(columns_) - 1;
  for (std::size_t index = 0; index + 1 < line.points.size(); ++index) {
    const MapLinePoint& from = line.points[index];
    const MapLinePoint& to = line.points[index + 1];
    const float reach = std::max(from.half_width_m, to.half_width_m);
    const float dx = to.position.x - from.position.x;
    const float dy = to.position.y - from.position.y;
    const float length_squared = (dx * dx) + (dy * dy);
    const std::int64_t column_from = std::max<std::int64_t>(
        0, FirstCentreAtOrAfter(std::min(from.position.x, to.position.x) - reach, cell_m_));
    const std::int64_t column_to = std::min(
        last, FirstCentreAtOrAfter(std::max(from.position.x, to.position.x) + reach, cell_m_));
    const std::int64_t row_from = std::max<std::int64_t>(
        0, FirstCentreAtOrAfter(std::min(from.position.y, to.position.y) - reach, cell_m_));
    const std::int64_t row_to = std::min(
        last, FirstCentreAtOrAfter(std::max(from.position.y, to.position.y) + reach, cell_m_));
    for (std::int64_t row = row_from; row <= row_to; ++row) {
      const float centre_y = (static_cast<float>(row) + 0.5F) * cell_m_;
      for (std::int64_t column = column_from; column <= column_to; ++column) {
        const float centre_x = (static_cast<float>(column) + 0.5F) * cell_m_;
        float along = 0.0F;
        if (length_squared > 0.0F) {
          along = std::clamp(
              (((centre_x - from.position.x) * dx) + ((centre_y - from.position.y) * dy)) /
                  length_squared,
              0.0F,
              1.0F);
        }
        const float near_x = from.position.x + (along * dx) - centre_x;
        const float near_y = from.position.y + (along * dy) - centre_y;
        const float half_width =
            from.half_width_m + (along * (to.half_width_m - from.half_width_m));
        if ((near_x * near_x) + (near_y * near_y) <= half_width * half_width) {
          cells_[(static_cast<std::size_t>(row) * columns_) + static_cast<std::size_t>(column)] |=
              flag;
        }
      }
    }
  }
}

}  // namespace core
