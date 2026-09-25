/// @file
/// @brief The road network's graph, DERIVED from the axes of RoadTable and
///        never stored (roads design §13: «Граф ядро ВЫВОДИТ, а не
///        хранит … Сохранённый граф стал бы вторым домом геометрии»).
/// @threading PARALLEL_READONLY
/// A pure function of the table it is given; the graph it returns is a value
/// the caller owns. Rebuilt when the network changes, not every step.
///
/// WHERE THE NODES ARE. Every road's two ends are nodes. An end that lies on
/// ANOTHER road's axis — a spur leaving the trunk, a lane leaving the street —
/// joins it there: the other road gets a node at that point of its axis, and
/// the spur's end and that node are one. The map draws junctions this way
/// (tables/roads.csv carries no junction rows of its own: a spur begins at a
/// point of the road it leaves, rarely at one of its vertices), and a road
/// the player lays into the middle of another will be joined the same way.
/// Two ends that lie on each other are one node. AND TWO AXES THAT CROSS meet
/// at a node cut into both: the village's lanes cross its street in the
/// middle, and a graph of mouths alone left both lanes islands (0.36.0,
/// found by the start network's own count).
///
/// WHAT AN EDGE IS: the piece of one road between two consecutive nodes on
/// it, with its length along the axis. The condition of the piece is read
/// from the road's stretches (core_common/road_state.h) by chainage.

#ifndef CORE_COMMON_ROAD_GRAPH_H_
#define CORE_COMMON_ROAD_GRAPH_H_

#include <cstdint>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/road_state.h"

namespace core {

/// @brief How far a road's end may lie from another road's axis and still
///        join it, metres. STUB, set against the start map (the test prints
///        every end's distance to the nearest foreign axis): the map's
///        `road_mouth_tol_m` rule is 1 m, and the drawn line's smoothing
///        moves an axis by less than that.
inline constexpr float kRoadJoinToleranceMetres = 2.0F;

/// @brief Index of a node in a RoadGraph.
using RoadNodeIndex = std::uint32_t;

/// @brief Index of an edge in a RoadGraph.
using RoadEdgeIndex = std::uint32_t;

inline constexpr std::uint32_t kNoRoadNode = 0xFFFFFFFFU;

/// @brief A place where roads end or meet.
struct RoadNode {
  Vec2 position;

  /// The road leaves the map here (RoadMark::kBorder at a road's end): the
  /// way out to the district and the neighbours.
  bool border = false;
};

/// @brief The piece of one road between two consecutive nodes on it.
struct RoadEdge {
  RoadId road;

  RoadNodeIndex from = kNoRoadNode;
  RoadNodeIndex to = kNoRoadNode;

  /// Metres along the road's axis where the piece begins and ends
  /// (from_chainage < to_chainage).
  float from_chainage_m = 0.0F;
  float to_chainage_m = 0.0F;

  /// to_chainage_m - from_chainage_m: the length a traveller covers.
  float length_m = 0.0F;

  RoadKind kind = RoadKind::kRoad;

  /// A bridge deck lies on the piece (RoadMark::kBridge on a vertex inside
  /// it): a passable edge that is marked (boss [23] item 7).
  bool bridge = false;
};

/// @brief The graph of a network.
struct RoadGraph {
  std::vector<RoadNode> nodes;

  std::vector<RoadEdge> edges;

  /// For each node, the edges that touch it (indices into `edges`).
  std::vector<std::vector<RoadEdgeIndex>> node_edges;
};

/// @brief Builds the graph of `roads` (see @file for where the nodes are).
/// @param join_tolerance_m kRoadJoinToleranceMetres unless a test says
///        otherwise.
/// @return The graph; a road with an axis of fewer than two points adds
///         nothing.
RoadGraph BuildRoadGraph(const RoadTable& roads, float join_tolerance_m = kRoadJoinToleranceMetres);

/// @brief The length of a road's axis, metres.
float RoadAxisLength(const std::vector<RoadPoint>& axis);

/// @brief The point of `axis` at `chainage_m` metres from its start, clamped
///        to its ends.
Vec2 PointAtChainage(const std::vector<RoadPoint>& axis, float chainage_m);

/// @brief Where on `axis` a point projects: its chainage and its distance to
///        the axis, metres.
struct AxisProjection {
  float chainage_m = 0.0F;
  float distance_m = 0.0F;
};

/// @brief The nearest point of `axis` to `point`.
AxisProjection ProjectOntoAxis(const std::vector<RoadPoint>& axis, Vec2 point);

/// @brief The component each node belongs to, numbered from 0 in order of
///        first node; nodes joined by any chain of edges share a number.
std::vector<std::uint32_t> RoadComponents(const RoadGraph& graph);

/// @brief How many stretches an axis of `length_m` metres is cut into
///        (kRoadStretchMetres each, the last one shorter; at least one).
std::uint32_t StretchCountForLength(float length_m);

}  // namespace core

#endif  // CORE_COMMON_ROAD_GRAPH_H_
