#include "core_common/road_cut.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core_common/road_graph.h"

namespace core {
namespace {

/// A span of axis shorter than this is no road and no strip.
constexpr float kShortestSpanMetres = 0.01F;

/// The stretch of `road` holding `chainage`, clamped to its stretches.
float WearAt(const RoadRow& road, float chainage) {
  if (road.stretches.empty()) {
    return 0.0F;
  }
  const auto index = static_cast<std::size_t>(std::max(0.0F, chainage) / kRoadStretchMetres);
  return road.stretches[std::min(index, road.stretches.size() - 1)].wear_pct;
}

/// The axis of `road` from `from` to `to`, metres along it: the cut points
/// and every vertex between.
std::vector<RoadPoint> AxisBetween(const RoadRow& road, float from, float to) {
  std::vector<RoadPoint> axis;
  axis.push_back(RoadPoint{.position = PointAtChainage(road.axis, from)});
  float walked = 0.0F;
  for (std::size_t index = 1; index < road.axis.size(); ++index) {
    const Vec2 a = road.axis[index - 1].position;
    const Vec2 b = road.axis[index].position;
    walked += std::sqrt(((b.x - a.x) * (b.x - a.x)) + ((b.y - a.y) * (b.y - a.y)));
    if (walked > from + kShortestSpanMetres && walked < to - kShortestSpanMetres) {
      axis.push_back(road.axis[index]);
    }
  }
  axis.push_back(RoadPoint{.position = PointAtChainage(road.axis, to)});
  // The span's own ends keep the old ends' marks where they are the old ends
  // (a border, a junction the map named); a cut end is plain.
  if (from <= kShortestSpanMetres) {
    axis.front().mark = road.axis.front().mark;
  }
  const float length = RoadAxisLength(road.axis);
  if (to >= length - kShortestSpanMetres) {
    axis.back().mark = road.axis.back().mark;
  }
  return axis;
}

/// The span's own stretches, each with the old road's wear at its middle.
std::vector<RoadStretch> StretchesBetween(const RoadRow& road, float from, float to) {
  const std::uint32_t count = StretchCountForLength(to - from);
  std::vector<RoadStretch> stretches(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    const float middle =
        std::min(to, from + ((static_cast<float>(index) + 0.5F) * kRoadStretchMetres));
    stretches[index].wear_pct = WearAt(road, middle);
  }
  return stretches;
}

}  // namespace

RoadCut CutRoad(const RoadRow& road, std::span<const std::pair<float, float>> taken) {
  RoadCut cut;
  const float length = RoadAxisLength(road.axis);
  std::vector<std::pair<float, float>> spans;
  for (const auto& [a, b] : taken) {
    const float from = std::clamp(std::min(a, b), 0.0F, length);
    const float to = std::clamp(std::max(a, b), 0.0F, length);
    if (to - from > kShortestSpanMetres) {
      spans.emplace_back(from, to);
    }
  }
  std::sort(spans.begin(), spans.end());
  std::vector<std::pair<float, float>> merged;
  for (const auto& span : spans) {
    if (!merged.empty() && span.first <= merged.back().second + kShortestSpanMetres) {
      merged.back().second = std::max(merged.back().second, span.second);
    } else {
      merged.push_back(span);
    }
  }
  if (merged.empty()) {
    cut.remnants.push_back(road);
    return cut;
  }
  const auto remnant = [&](float from, float to) {
    RoadRow part = road;
    part.origin = RoadOrigin::kPlayer;
    part.axis = AxisBetween(road, from, to);
    part.stretches = StretchesBetween(road, from, to);
    cut.remnants.push_back(std::move(part));
  };
  float kept_from = 0.0F;
  for (const auto& [from, to] : merged) {
    if (from - kept_from > kShortestSpanMetres) {
      remnant(kept_from, from);
    }
    LandStripRow strip;
    for (const RoadPoint& point : AxisBetween(road, from, to)) {
      strip.axis.push_back(point.position);
    }
    strip.stretches = StretchesBetween(road, from, to);
    cut.strips.push_back(std::move(strip));
    kept_from = to;
  }
  if (length - kept_from > kShortestSpanMetres) {
    remnant(kept_from, length);
  }
  return cut;
}

float StripWearAt(std::span<const LandStripRow> strips, Vec2 point, float reach_m) {
  float best = -1.0F;
  for (const LandStripRow& strip : strips) {
    if (strip.axis.size() < 2 || strip.stretches.empty()) {
      continue;
    }
    std::vector<RoadPoint> axis;
    axis.reserve(strip.axis.size());
    for (const Vec2& position : strip.axis) {
      axis.push_back(RoadPoint{.position = position});
    }
    const AxisProjection projection = ProjectOntoAxis(axis, point);
    if (projection.distance_m > reach_m) {
      continue;
    }
    const auto index =
        std::min(static_cast<std::size_t>(projection.chainage_m / kRoadStretchMetres),
                 strip.stretches.size() - 1);
    best = std::max(best, strip.stretches[index].wear_pct);
  }
  return best;
}

}  // namespace core
