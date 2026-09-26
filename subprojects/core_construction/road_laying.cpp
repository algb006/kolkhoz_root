#include "road_laying.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "core_common/emit_event.h"
#include "core_common/road_cut.h"
#include "core_common/road_draft.h"
#include "core_common/road_graph.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {
namespace {

/// How near a strip's axis a new stretch's middle must be to take its wear:
/// half a bed (roads design §7), the boss's «a new axis within half-width
/// inherits it» (the roads work plan, answer 8).
constexpr float kStripReachMetres = 4.0F;

}  // namespace

OrderRefusal LayRoad(const RoadTracer& trace,
                     WorldState& current,
                     OrderId order_id,
                     OrderRow& order) {
  if (!trace) {
    return OrderRefusal::kNoConsumer;
  }
  // Gravel and asphalt are built, not marked: road work, delivery 7e. STUB.
  if (order.road_surface != RoadSurface::kNone && order.road_surface != RoadSurface::kDirt) {
    return OrderRefusal::kNoConsumer;
  }
  RoadDraft draft;
  draft.kind = order.road_kind;
  draft.surface = order.road_surface;
  draft.point_count = order.road_point_count;
  draft.points = order.road_points;
  const RoadDraftResult traced = trace(current, draft);
  if (!traced.blocks.empty() || traced.axis.size() < 2) {
    return OrderRefusal::kRuleForbids;
  }
  RoadRow road;
  road.kind = draft.kind;
  road.surface = draft.surface;
  road.origin = RoadOrigin::kPlayer;  // the axis lives in the save, as traced
  road.removable = 1;
  // The start's word does not reach a road laid in play; the default stands
  // until the core counts traffic (road_state.h).
  road.traffic_word = RoadTrafficWord::kRegular;
  road.axis.reserve(traced.axis.size());
  for (const RoadAxisPoint& point : traced.axis) {
    road.axis.push_back(RoadPoint{.position = point.position, .mark = point.mark});
  }
  // A fresh bed — unless the land under it was a road's: a stretch along a
  // strip takes the strip's wear back (7d, «Износ принадлежит ЗЕМЛЕ»). A path
  // wears nothing (roads design §4) and takes nothing.
  road.stretches.assign(StretchCountForLength(traced.length_m), RoadStretch{});
  if (road.kind == RoadKind::kRoad && !current.land_strips.rows.empty()) {
    for (std::size_t index = 0; index < road.stretches.size(); ++index) {
      const float middle =
          std::min(traced.length_m, (static_cast<float>(index) + 0.5F) * kRoadStretchMetres);
      const float wear = StripWearAt(
          current.land_strips.rows, PointAtChainage(road.axis, middle), kStripReachMetres);
      road.stretches[index].wear_pct = std::max(0.0F, wear);
    }
  }
  const RoadId laid = AppendRow(current.roads, road);
  current.road_index = BuildRoadIndex(current.roads);
  order.road = laid;
  SimEvent& event = EmitEvent(current, EventKind::kRoadLaid, EventSeverity::kNotable);
  event.road = laid;
  event.order = order_id;
  return OrderRefusal::kNone;
}

OrderRefusal DemolishRoad(const RoadSelector& select,
                          WorldState& current,
                          OrderId order_id,
                          OrderRow& order) {
  if (!select) {
    return OrderRefusal::kNoConsumer;
  }
  const std::uint32_t row = FindRow(current.roads, order.road);
  if (row == kNoRow) {
    return OrderRefusal::kRuleForbids;
  }
  const RoadSurface surface = current.roads.rows[row].surface;
  if (surface != RoadSurface::kNone && surface != RoadSurface::kDirt) {
    return OrderRefusal::kNoConsumer;  // a paved road's demolition is road work, 7e
  }
  const RoadPieces pieces = select(
      current,
      RoadSelection{.road = order.road, .from = order.road_points[0], .to = order.road_points[1]},
      RoadOperation::kDemolish);
  std::vector<std::pair<float, float>> taken;
  for (const RoadPiece& piece : pieces.pieces) {
    if (piece.refusal == RoadPieceRefusal::kNone) {
      taken.emplace_back(piece.s_from_m, piece.s_to_m);
    }
  }
  if (taken.empty()) {
    return OrderRefusal::kRuleForbids;
  }
  const RoadId road_id = order.road;
  const bool path = current.roads.rows[row].kind == RoadKind::kPath;
  RoadCut cut = CutRoad(current.roads.rows[row], taken);
  if (cut.strips.empty()) {
    // Nothing of length came out (a sliver under a centimetre): the road is
    // as it was, and no event may say otherwise (static review of 0.36.31).
    return OrderRefusal::kRuleForbids;
  }
  if (cut.remnants.empty()) {
    RemoveRow(current.roads, road_id);
  } else {
    current.roads.rows[row] = std::move(cut.remnants.front());
    for (std::size_t index = 1; index < cut.remnants.size(); ++index) {
      AppendRow(current.roads, cut.remnants[index]);
    }
  }
  // A path wears nothing (roads design §4) and leaves no strip. The strips
  // only grow — one a demolition, none consumed by a road laid again — and
  // their forgetting waits for 3b (road_state.h); named, not bounded.
  if (!path) {
    for (const LandStripRow& strip : cut.strips) {
      AppendRow(current.land_strips, strip);
    }
  }
  current.road_index = BuildRoadIndex(current.roads);
  SimEvent& event = EmitEvent(current, EventKind::kRoadDemolished, EventSeverity::kNotable);
  event.road = road_id;
  event.order = order_id;
  return OrderRefusal::kNone;
}

}  // namespace core
