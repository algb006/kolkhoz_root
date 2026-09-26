#include "road_laying.h"

#include <cstddef>

#include "core_common/emit_event.h"
#include "core_common/road_draft.h"
#include "core_common/road_graph.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {

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
  // A fresh bed: every stretch at nought wear (a path has none anyway).
  road.stretches.assign(StretchCountForLength(traced.length_m), RoadStretch{});
  const RoadId laid = AppendRow(current.roads, road);
  current.road_index = BuildRoadIndex(current.roads);
  order.road = laid;
  SimEvent& event = EmitEvent(current, EventKind::kRoadLaid, EventSeverity::kNotable);
  event.road = laid;
  event.order = order_id;
  return OrderRefusal::kNone;
}

}  // namespace core
