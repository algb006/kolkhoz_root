/// @file
/// @brief Taking stretches out of a road (construction design §13; delivery
///        7d): what stands of it after, and the land the taken stretches
///        leave, keeping their wear («Износ принадлежит ЗЕМЛЕ»).
/// @threading PARALLEL_READONLY
/// A pure function of the road and the stretches.
///
/// THE WEAR TRAVELS BY CHAINAGE. A remnant or a strip is cut into its own
/// kRoadStretchMetres stretches from its own first point; each takes the
/// wear the old road had at that stretch's middle. So a stretch keeps what
/// the land under it had, whichever side of the cut it fell.

#ifndef CORE_COMMON_ROAD_CUT_H_
#define CORE_COMMON_ROAD_CUT_H_

#include <span>
#include <utility>
#include <vector>

#include "core_common/road_state.h"

namespace core {

/// @brief What cutting a road leaves.
struct RoadCut {
  /// What stands, in order along the old axis: none when the whole went,
  /// one when an end went, more when a middle went. Each keeps the old
  /// road's kind, surface, map road, removability and traffic word; its
  /// origin is the player's — its axis is no longer the map's, and it goes
  /// into the save as it now is.
  std::vector<RoadRow> remnants;

  /// The land each taken stretch leaves, in the same order.
  std::vector<LandStripRow> strips;
};

/// @brief Cuts `taken` out of `road`.
/// @param taken [from, to] chainages, metres along the axis; any order,
///        clamped to the axis; overlapping or touching ones merge.
/// @return The road whole as its one remnant, and no strip, when nothing
///         of length is taken.
RoadCut CutRoad(const RoadRow& road, std::span<const std::pair<float, float>> taken);

/// @brief The wear the land under `point` keeps from `strips`, if a strip's
///        axis passes within `reach_m` of it: the most of them. Negative when
///        none does.
float StripWearAt(std::span<const LandStripRow> strips, Vec2 point, float reach_m);

}  // namespace core

#endif  // CORE_COMMON_ROAD_CUT_H_
