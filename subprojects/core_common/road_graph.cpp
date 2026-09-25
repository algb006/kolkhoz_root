#include "core_common/road_graph.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace core {
namespace {

float Distance(Vec2 a, Vec2 b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt((dx * dx) + (dy * dy));
}

/// A node to be cut into one road's axis at a chainage, before the nodes are
/// numbered.
struct Cut {
  float chainage_m = 0.0F;
  RoadNodeIndex node = kNoRoadNode;
};

/// The node at `position`, reused when one already stands within the
/// tolerance — two ends that lie on each other are one node.
RoadNodeIndex NodeAt(RoadGraph& graph, Vec2 position, float tolerance_m) {
  for (RoadNodeIndex index = 0; index < graph.nodes.size(); ++index) {
    if (Distance(graph.nodes[index].position, position) <= tolerance_m) {
      return index;
    }
  }
  graph.nodes.push_back(RoadNode{.position = position});
  graph.node_edges.emplace_back();
  return static_cast<RoadNodeIndex>(graph.nodes.size() - 1);
}

}  // namespace

float RoadAxisLength(const std::vector<RoadPoint>& axis) {
  float length = 0.0F;
  for (std::size_t index = 1; index < axis.size(); ++index) {
    length += Distance(axis[index - 1].position, axis[index].position);
  }
  return length;
}

Vec2 PointAtChainage(const std::vector<RoadPoint>& axis, float chainage_m) {
  if (axis.empty()) {
    return Vec2{};
  }
  float walked = 0.0F;
  for (std::size_t index = 1; index < axis.size(); ++index) {
    const Vec2 from = axis[index - 1].position;
    const Vec2 to = axis[index].position;
    const float step = Distance(from, to);
    if (walked + step >= chainage_m && step > 0.0F) {
      const float share = std::clamp((chainage_m - walked) / step, 0.0F, 1.0F);
      return Vec2{.x = from.x + ((to.x - from.x) * share), .y = from.y + ((to.y - from.y) * share)};
    }
    walked += step;
  }
  return chainage_m <= 0.0F ? axis.front().position : axis.back().position;
}

AxisProjection ProjectOntoAxis(const std::vector<RoadPoint>& axis, Vec2 point) {
  AxisProjection best{.chainage_m = 0.0F, .distance_m = axis.empty() ? 0.0F : 1.0e30F};
  if (axis.size() == 1) {
    best.distance_m = Distance(axis.front().position, point);
    return best;
  }
  float walked = 0.0F;
  for (std::size_t index = 1; index < axis.size(); ++index) {
    const Vec2 from = axis[index - 1].position;
    const Vec2 to = axis[index].position;
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float step_sq = (dx * dx) + (dy * dy);
    float share = 0.0F;
    if (step_sq > 0.0F) {
      share =
          std::clamp((((point.x - from.x) * dx) + ((point.y - from.y) * dy)) / step_sq, 0.0F, 1.0F);
    }
    const Vec2 foot{.x = from.x + (dx * share), .y = from.y + (dy * share)};
    const float distance = Distance(foot, point);
    const float step = std::sqrt(step_sq);
    if (distance < best.distance_m) {
      best = AxisProjection{.chainage_m = walked + (step * share), .distance_m = distance};
    }
    walked += step;
  }
  return best;
}

std::uint32_t StretchCountForLength(float length_m) {
  if (!(length_m > 0.0F)) {
    return 1;
  }
  return std::max<std::uint32_t>(
      1U, static_cast<std::uint32_t>(std::ceil(length_m / kRoadStretchMetres)));
}

RoadGraph BuildRoadGraph(const RoadTable& roads, float join_tolerance_m) {
  RoadGraph graph;
  const std::size_t count = roads.rows.size();
  std::vector<float> lengths(count, 0.0F);
  std::vector<std::vector<Cut>> cuts(count);
  for (std::size_t row = 0; row < count; ++row) {
    lengths[row] = RoadAxisLength(roads.rows[row].axis);
  }
  // THE ENDS FIRST: every road's two ends are nodes, and an end lying on
  // another end is the same node.
  for (std::size_t row = 0; row < count; ++row) {
    const std::vector<RoadPoint>& axis = roads.rows[row].axis;
    if (axis.size() < 2) {
      continue;
    }
    for (const bool first : {true, false}) {
      const RoadPoint& end = first ? axis.front() : axis.back();
      const RoadNodeIndex node = NodeAt(graph, end.position, join_tolerance_m);
      graph.nodes[node].border = graph.nodes[node].border || end.mark == RoadMark::kBorder;
      cuts[row].push_back(Cut{.chainage_m = first ? 0.0F : lengths[row], .node = node});
    }
  }
  // THEN THE MOUTHS: an end lying on another road's axis cuts that road
  // there — near that road's own end too (static review of 0.36.0: skipping
  // those left a spur whose end lay 2.5 m from the other road's end an
  // island, the ends pass having merged nothing past 2 m). A cut that lands
  // on the other road's own end node is dropped when the edges are made.
  for (std::size_t row = 0; row < count; ++row) {
    const std::vector<RoadPoint>& axis = roads.rows[row].axis;
    if (axis.size() < 2) {
      continue;
    }
    for (const bool first : {true, false}) {
      const Vec2 end = first ? axis.front().position : axis.back().position;
      const RoadNodeIndex node = cuts[row][first ? 0 : 1].node;
      for (std::size_t other = 0; other < count; ++other) {
        if (other == row || roads.rows[other].axis.size() < 2) {
          continue;
        }
        const AxisProjection projection = ProjectOntoAxis(roads.rows[other].axis, end);
        if (projection.distance_m > join_tolerance_m) {
          continue;
        }
        cuts[other].push_back(Cut{.chainage_m = projection.chainage_m, .node = node});
      }
    }
  }

  // AND THE CROSSINGS: two axes that cross each other — the village's lanes
  // cross its street in the middle, a cross and not a mouth — meet at a node
  // cut into both. Found by the segments of different roads that intersect,
  // bucketed on a grid so that the map's 38 thousand points do not compare
  // every segment with every other.
  struct Segment {
    std::uint32_t road = 0;
    Vec2 from;
    Vec2 to;
    float chainage_m = 0.0F;  // at `from`
  };

  constexpr float kCell = 100.0F;
  std::vector<Segment> segments;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> grid;
  const auto cell_key = [](std::int32_t cx, std::int32_t cy) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32U) |
           static_cast<std::uint32_t>(cy);
  };
  for (std::size_t row = 0; row < count; ++row) {
    const std::vector<RoadPoint>& axis = roads.rows[row].axis;
    float walked = 0.0F;
    for (std::size_t index = 1; index < axis.size(); ++index) {
      const Segment segment{.road = static_cast<std::uint32_t>(row),
                            .from = axis[index - 1].position,
                            .to = axis[index].position,
                            .chainage_m = walked};
      walked += Distance(segment.from, segment.to);
      const auto id = static_cast<std::uint32_t>(segments.size());
      segments.push_back(segment);
      const auto low_x =
          static_cast<std::int32_t>(std::floor(std::min(segment.from.x, segment.to.x) / kCell));
      const auto high_x =
          static_cast<std::int32_t>(std::floor(std::max(segment.from.x, segment.to.x) / kCell));
      const auto low_y =
          static_cast<std::int32_t>(std::floor(std::min(segment.from.y, segment.to.y) / kCell));
      const auto high_y =
          static_cast<std::int32_t>(std::floor(std::max(segment.from.y, segment.to.y) / kCell));
      for (std::int32_t cx = low_x; cx <= high_x; ++cx) {
        for (std::int32_t cy = low_y; cy <= high_y; ++cy) {
          grid[cell_key(cx, cy)].push_back(id);
        }
      }
    }
  }
  // THE CANDIDATES ARE SORTED BEFORE THEY ARE TESTED (static review of
  // 0.36.0): the grid is a hash map, and the order a hash map is walked in
  // differs between standard libraries — so the nodes the crossings make
  // would have been numbered one way under Clang and another under MSVC. The
  // core owes the same answer on both.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates;
  for (const auto& [key, in_cell] : grid) {
    for (std::size_t first = 0; first < in_cell.size(); ++first) {
      for (std::size_t second = first + 1; second < in_cell.size(); ++second) {
        if (segments[in_cell[first]].road != segments[in_cell[second]].road) {
          candidates.emplace_back(std::min(in_cell[first], in_cell[second]),
                                  std::max(in_cell[first], in_cell[second]));
        }
      }
    }
  }
  std::ranges::sort(candidates);
  const auto duplicates = std::ranges::unique(candidates);
  candidates.erase(duplicates.begin(), duplicates.end());
  for (const auto& [first, second] : candidates) {
    const Segment& a = segments[first];
    const Segment& b = segments[second];
    const float rx = a.to.x - a.from.x;
    const float ry = a.to.y - a.from.y;
    const float sx = b.to.x - b.from.x;
    const float sy = b.to.y - b.from.y;
    const float denominator = (rx * sy) - (ry * sx);
    // PARALLEL BY ANGLE, NOT BY AN ABSOLUTE NUMBER (static review of 0.36.0):
    // the cross product of two ten-metre segments is a hundred square metres,
    // so 1e-6 let two roads sharing a drawn stretch through as crossing it.
    const float lengths_product = std::sqrt(((rx * rx) + (ry * ry)) * ((sx * sx) + (sy * sy)));
    if (std::abs(denominator) <= 1.0e-4F * lengths_product) {
      continue;  // parallel: a shared stretch, not a crossing
    }
    const float qx = b.from.x - a.from.x;
    const float qy = b.from.y - a.from.y;
    const float t = ((qx * sy) - (qy * sx)) / denominator;
    const float u = ((qx * ry) - (qy * rx)) / denominator;
    if (t < 0.0F || t > 1.0F || u < 0.0F || u > 1.0F) {
      continue;
    }
    const Vec2 point{.x = a.from.x + (rx * t), .y = a.from.y + (ry * t)};
    const float along_a = a.chainage_m + (Distance(a.from, a.to) * t);
    const float along_b = b.chainage_m + (Distance(b.from, b.to) * u);
    // A crossing AT an end is a mouth, and the pass above has it.
    const bool at_an_end =
        along_a <= join_tolerance_m || along_a >= lengths[a.road] - join_tolerance_m ||
        along_b <= join_tolerance_m || along_b >= lengths[b.road] - join_tolerance_m;
    if (at_an_end) {
      continue;
    }
    const RoadNodeIndex node = NodeAt(graph, point, join_tolerance_m);
    cuts[a.road].push_back(Cut{.chainage_m = along_a, .node = node});
    cuts[b.road].push_back(Cut{.chainage_m = along_b, .node = node});
  }
  // THE EDGES: each road cut at its nodes in chainage order.
  for (std::size_t row = 0; row < count; ++row) {
    std::vector<Cut>& on_road = cuts[row];
    if (on_road.size() < 2) {
      continue;
    }
    // Ties in chainage broken by node, so the order is total and the same on
    // every compiler.
    std::ranges::sort(on_road, [](const Cut& left, const Cut& right) {
      return left.chainage_m != right.chainage_m ? left.chainage_m < right.chainage_m
                                                 : left.node < right.node;
    });
    const RoadRow& road = roads.rows[row];
    // The bridge decks of this road: consecutive pairs of bridge marks by
    // chainage, a lone mark a point (the stone arch).
    std::vector<float> bridge_marks;
    {
      float walked = 0.0F;
      for (std::size_t vertex = 0; vertex < road.axis.size(); ++vertex) {
        if (vertex > 0) {
          walked += Distance(road.axis[vertex - 1].position, road.axis[vertex].position);
        }
        if (road.axis[vertex].mark == RoadMark::kBridge) {
          bridge_marks.push_back(walked);
        }
      }
    }
    for (std::size_t index = 1; index < on_road.size(); ++index) {
      const Cut& from = on_road[index - 1];
      const Cut& to = on_road[index];
      // ONE NODE TWICE, AT ONE PLACE: two mouths on one point of the axis, or
      // a mouth on this road's own end. Dropped. But the SAME node at two
      // places is a loop — a lane that leaves the street and comes back —
      // and a loop is an edge (static review of 0.36.0: skipping it lost
      // the whole road); and two DIFFERENT nodes at one chainage are joined
      // by a piece of no length, or the road would be broken between them.
      if (from.node == to.node && to.chainage_m - from.chainage_m <= join_tolerance_m) {
        continue;
      }
      RoadEdge edge{.road = roads.row_ids[row],
                    .from = from.node,
                    .to = to.node,
                    .from_chainage_m = from.chainage_m,
                    .to_chainage_m = to.chainage_m,
                    .length_m = to.chainage_m - from.chainage_m,
                    .kind = road.kind};
      // THE PIECE IS A BRIDGE WHEN IT OVERLAPS A DECK (static review of
      // 0.36.0: a vertex inside the closed interval marked the piece that
      // only ENDS at the deck, and missed one cut wholly inside it). A deck is
      // two consecutive marks; a lone mark is a point, and the piece holding
      // it in [from, to) — the last piece its end too — is the bridge.
      for (std::size_t mark = 0; mark < bridge_marks.size(); mark += 2) {
        const float deck_from = bridge_marks[mark];
        const float deck_to = mark + 1 < bridge_marks.size() ? bridge_marks[mark + 1] : deck_from;
        const bool last_piece = index + 1 == on_road.size();
        const bool overlaps =
            deck_to > deck_from
                ? std::max(from.chainage_m, deck_from) < std::min(to.chainage_m, deck_to)
                : deck_from >= from.chainage_m &&
                      (deck_from < to.chainage_m || (last_piece && deck_from <= to.chainage_m));
        edge.bridge = edge.bridge || overlaps;
      }
      const auto edge_index = static_cast<RoadEdgeIndex>(graph.edges.size());
      graph.edges.push_back(edge);
      graph.node_edges[from.node].push_back(edge_index);
      graph.node_edges[to.node].push_back(edge_index);
    }
  }
  return graph;
}

std::vector<std::uint32_t> RoadComponents(const RoadGraph& graph) {
  constexpr std::uint32_t kUnset = 0xFFFFFFFFU;
  std::vector<std::uint32_t> component(graph.nodes.size(), kUnset);
  std::uint32_t next = 0;
  std::vector<RoadNodeIndex> stack;
  for (RoadNodeIndex start = 0; start < graph.nodes.size(); ++start) {
    if (component[start] != kUnset) {
      continue;
    }
    component[start] = next;
    stack.push_back(start);
    while (!stack.empty()) {
      const RoadNodeIndex node = stack.back();
      stack.pop_back();
      for (const RoadEdgeIndex edge_index : graph.node_edges[node]) {
        const RoadEdge& edge = graph.edges[edge_index];
        const RoadNodeIndex other = edge.from == node ? edge.to : edge.from;
        if (component[other] == kUnset) {
          component[other] = next;
          stack.push_back(other);
        }
      }
    }
    ++next;
  }
  return component;
}

}  // namespace core
