// What water stands at a point, as the ice reads it (core_common/ice_water.h;
// #1859).

#include "core_common/ice_water.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace core {
namespace {

/// How far `point` is from the channel's EDGE along one segment: the distance
/// to the axis less the half width there, interpolated along the segment.
float FromChannelEdge(Vec2 point, const MapLinePoint& from, const MapLinePoint& to) {
  const float span_x = to.position.x - from.position.x;
  const float span_y = to.position.y - from.position.y;
  const float length_squared = (span_x * span_x) + (span_y * span_y);
  float along = 0.0F;
  if (length_squared > 0.0F) {
    along = std::clamp(
        (((point.x - from.position.x) * span_x) + ((point.y - from.position.y) * span_y)) /
            length_squared,
        0.0F,
        1.0F);
  }
  const float axis = std::hypot(point.x - (from.position.x + (span_x * along)),
                                point.y - (from.position.y + (span_y * along)));
  const float half_width = from.half_width_m + ((to.half_width_m - from.half_width_m) * along);
  return axis - half_width;
}

/// The least distance from `point` to any water line's channel edge; ≤ 0
/// inside a channel; +∞ on a map with no line.
float FromNearestChannel(const MapObstacles& map, Vec2 point) {
  float nearest = std::numeric_limits<float>::infinity();
  for (const MapLineDef& line : map.lines) {
    for (std::size_t index = 1; index < line.points.size(); ++index) {
      nearest =
          std::min(nearest, FromChannelEdge(point, line.points[index - 1], line.points[index]));
    }
  }
  return nearest;
}

/// The even-odd rule on a closed outline.
bool InsideArea(Vec2 point, const std::vector<Vec2>& outline) {
  bool inside = false;
  if (outline.size() < 3) {
    return false;
  }
  for (std::size_t index = 0, previous = outline.size() - 1; index < outline.size();
       previous = index++) {
    const Vec2 a = outline[index];
    const Vec2 b = outline[previous];
    if ((a.y > point.y) != (b.y > point.y) &&
        point.x < a.x + ((point.y - a.y) * (b.x - a.x) / (b.y - a.y))) {
      inside = !inside;
    }
  }
  return inside;
}

}  // namespace

IceWater IceWaterAt(const MapObstacles& map, Vec2 point, float inflow_disc_m) {
  const float from_channel = FromNearestChannel(map, point);
  if (from_channel <= 0.0F) {
    return IceWater::kRiver;
  }
  bool backwater = false;
  for (const MapAreaDef& area : map.areas) {
    const bool still = area.kind == MapAreaKind::kLake || area.kind == MapAreaKind::kPond;
    const bool slack = area.kind == MapAreaKind::kBackwater || area.kind == MapAreaKind::kShallows;
    if ((!still && !slack) || !InsideArea(point, area.outline)) {
      continue;
    }
    if (still) {
      // Still water wins over a backwater it overlaps: the lake is the body.
      return from_channel <= inflow_disc_m ? IceWater::kInflow : IceWater::kStill;
    }
    backwater = true;
  }
  return backwater ? IceWater::kBackwater : IceWater::kNone;
}

}  // namespace core
