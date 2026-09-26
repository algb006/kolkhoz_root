#include "core_common/road_pieces.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <utility>

#include "core_common/road_graph.h"
#include "core_common/state_table_ops.h"

namespace core {
namespace {

/// Metres between the samples a piece is asked about the floodplain by.
constexpr float kFloodplainSampleMetres = 10.0F;

/// A chainage this near a joint is the joint.
constexpr float kJointSlackMetres = 0.01F;

/// A stretch of one road's axis taken out: [from, to], metres.
struct Cut {
  std::uint32_t road = 0;  ///< RoadId value.
  float from_m = 0.0F;
  float to_m = 0.0F;
};

/// A point of the network something reaches: a road and how far along it.
struct AccessPoint {
  std::uint32_t road = 0;
  float chainage_m = 0.0F;
};

/// Something the network must not be cut from: a unit, or a way out.
struct Anchor {
  UnitId unit;
  MapRoadId map_road;
  std::vector<AccessPoint> points;
};

bool InsidePolygon(Vec2 point, const std::vector<Vec2>& outline) {
  bool inside = false;
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

/// The network with some stretches taken out, asked which of its places
/// still hang together. Built once per selection; each Reach call is one
/// union-find over the graph's nodes and the cut pieces' remnants.
class Connectivity {
 public:
  Connectivity(const RoadPieceSite& site, const RoadGraph& graph) : site_(site), graph_(graph) {
    for (std::size_t index = 0; index < graph_.edges.size(); ++index) {
      edges_of_road_[graph_.edges[index].road.value].push_back(index);
    }
    // Units with a road in reach: every road whose axis comes within the
    // access distance of the bed's edge.
    for (const RoadAnchorUnit& unit : site_.units) {
      Anchor anchor;
      anchor.unit = unit.unit;
      for (std::size_t row = 0; row < site_.roads->rows.size(); ++row) {
        const RoadRow& road = site_.roads->rows[row];
        if (road.axis.size() < 2) {
          continue;
        }
        const float half =
            road.kind == RoadKind::kPath ? site_.path_half_width_m : site_.road_half_width_m;
        const AxisProjection projection = ProjectOntoAxis(road.axis, unit.position);
        if (projection.distance_m <= site_.road_access_m + half) {
          anchor.points.push_back(AccessPoint{.road = site_.roads->row_ids[row].value,
                                              .chainage_m = projection.chainage_m});
        }
      }
      if (!anchor.points.empty()) {
        anchors_.push_back(std::move(anchor));
      }
    }
    // The ways out — a road's end marked as the border — and THE PLACES A
    // MAP ROAD LEADS TO: an end of a map road that meets no other road. The
    // core knows no settlement, hayfield or cemetery by name (they live in
    // map.db), and a road the map draws to a dead end leads somewhere for a
    // reason — STUB until the places are exported: the neighbours inside the
    // map (road_pond_village, road_lesnoy_spur) came out removable whole
    // without it. A player's dead end leads to nothing the map knows.
    std::vector<std::uint32_t> degree(graph_.nodes.size(), 0);
    for (const RoadEdge& edge : graph_.edges) {
      ++degree[edge.from];
      ++degree[edge.to];
    }
    for (std::size_t row = 0; row < site_.roads->rows.size(); ++row) {
      const RoadRow& road = site_.roads->rows[row];
      if (road.axis.size() < 2) {
        continue;
      }
      const std::uint32_t id = site_.roads->row_ids[row].value;
      const float length = RoadAxisLength(road.axis);
      const auto end_node = [&](float chainage) -> std::optional<RoadNodeIndex> {
        for (const RoadEdge& edge : graph_.edges) {
          if (edge.road.value != id) {
            continue;
          }
          if (std::abs(edge.from_chainage_m - chainage) <= kJointSlackMetres) {
            return edge.from;
          }
          if (std::abs(edge.to_chainage_m - chainage) <= kJointSlackMetres) {
            return edge.to;
          }
        }
        return std::nullopt;
      };
      for (const float chainage : {0.0F, length}) {
        const RoadPoint& end = chainage == 0.0F ? road.axis.front() : road.axis.back();
        const std::optional<RoadNodeIndex> node = end_node(chainage);
        const bool dead_end_of_map_road =
            road.origin == RoadOrigin::kMap && node && degree[*node] == 1;
        if (end.mark == RoadMark::kBorder || dead_end_of_map_road) {
          anchors_.push_back(Anchor{.unit = UnitId{},
                                    .map_road = road.map_road,
                                    .points = {{.road = id, .chainage_m = chainage}}});
        }
      }
    }
    reached_before_ = Reach({});
  }

  /// The first anchor that reached the district's network before and does
  /// not with `cuts` taken out; nothing when every one still does.
  std::optional<Anchor> FirstStranded(const std::vector<Cut>& cuts) const {
    const std::vector<bool> reached = Reach(cuts);
    for (std::size_t index = 0; index < anchors_.size(); ++index) {
      if (reached_before_[index] && !reached[index]) {
        return anchors_[index];
      }
    }
    return std::nullopt;
  }

 private:
  /// Whether each anchor reaches the district's network with `cuts` out.
  std::vector<bool> Reach(const std::vector<Cut>& cuts) const {
    std::vector<std::uint32_t> parent(graph_.nodes.size());
    std::iota(parent.begin(), parent.end(), 0U);
    const auto find = [&parent](std::uint32_t item) {
      while (parent[item] != item) {
        parent[item] = parent[parent[item]];
        item = parent[item];
      }
      return item;
    };
    const auto join = [&](std::uint32_t a, std::uint32_t b) { parent[find(a)] = find(b); };

    // What stands of each edge: [from, to] and the set it hangs in.
    struct Kept {
      float from_m;
      float to_m;
      std::uint32_t set;
    };

    std::vector<std::vector<Kept>> kept(graph_.edges.size());
    for (std::size_t index = 0; index < graph_.edges.size(); ++index) {
      const RoadEdge& edge = graph_.edges[index];
      std::vector<Cut> on_edge;
      for (const Cut& cut : cuts) {
        if (cut.road == edge.road.value && cut.to_m > edge.from_chainage_m + kJointSlackMetres &&
            cut.from_m < edge.to_chainage_m - kJointSlackMetres) {
          on_edge.push_back(cut);
        }
      }
      if (on_edge.empty()) {
        join(edge.from, edge.to);
        kept[index].push_back(
            Kept{.from_m = edge.from_chainage_m, .to_m = edge.to_chainage_m, .set = edge.from});
        continue;
      }
      std::sort(on_edge.begin(), on_edge.end(), [](const Cut& a, const Cut& b) {
        return a.from_m < b.from_m;
      });
      float start = edge.from_chainage_m;
      std::vector<std::pair<float, float>> spans;
      for (const Cut& cut : on_edge) {
        if (cut.from_m > start + kJointSlackMetres) {
          spans.emplace_back(start, cut.from_m);
        }
        start = std::max(start, cut.to_m);
      }
      if (start < edge.to_chainage_m - kJointSlackMetres) {
        spans.emplace_back(start, edge.to_chainage_m);
      }
      for (const auto& [from, to] : spans) {
        std::uint32_t set = 0;
        if (from <= edge.from_chainage_m + kJointSlackMetres) {
          set = edge.from;
        } else if (to >= edge.to_chainage_m - kJointSlackMetres) {
          set = edge.to;
        } else {
          set = static_cast<std::uint32_t>(parent.size());  // an island remnant
          parent.push_back(set);
        }
        kept[index].push_back(Kept{.from_m = from, .to_m = to, .set = set});
      }
    }
    // The set a point of the network hangs in, or nothing when it was taken.
    const auto set_of = [&](const AccessPoint& point) -> std::optional<std::uint32_t> {
      const auto found = edges_of_road_.find(point.road);
      if (found == edges_of_road_.end()) {
        return std::nullopt;
      }
      for (const std::size_t index : found->second) {
        for (const Kept& piece : kept[index]) {
          if (point.chainage_m >= piece.from_m - kJointSlackMetres &&
              point.chainage_m <= piece.to_m + kJointSlackMetres) {
            return find(piece.set);
          }
        }
      }
      return std::nullopt;
    };
    // The district's network: the sets holding a road that may not be taken.
    std::vector<std::uint32_t> main_sets;
    for (const RoadEdge& edge : graph_.edges) {
      const std::uint32_t row = FindRow(*site_.roads, edge.road);
      if (row != kNoRow && site_.roads->rows[row].removable == 0) {
        main_sets.push_back(find(edge.from));
      }
    }
    if (main_sets.empty()) {
      // No such road: the set most of what is reached hangs in.
      std::vector<std::pair<std::uint32_t, std::uint32_t>> counts;
      for (const Anchor& anchor : anchors_) {
        for (const AccessPoint& point : anchor.points) {
          if (const std::optional<std::uint32_t> set = set_of(point)) {
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& entry) {
              return entry.first == *set;
            });
            if (it == counts.end()) {
              counts.emplace_back(*set, 1U);
            } else {
              ++it->second;
            }
          }
        }
      }
      if (!counts.empty()) {
        main_sets.push_back(
            std::max_element(counts.begin(), counts.end(), [](const auto& a, const auto& b) {
              return a.second < b.second;
            })->first);
      }
    }
    std::vector<bool> reached(anchors_.size(), false);
    for (std::size_t index = 0; index < anchors_.size(); ++index) {
      for (const AccessPoint& point : anchors_[index].points) {
        const std::optional<std::uint32_t> set = set_of(point);
        if (set && std::find(main_sets.begin(), main_sets.end(), *set) != main_sets.end()) {
          reached[index] = true;
          break;
        }
      }
    }
    return reached;
  }

  const RoadPieceSite& site_;
  const RoadGraph& graph_;
  std::unordered_map<std::uint32_t, std::vector<std::size_t>> edges_of_road_;
  std::vector<Anchor> anchors_;
  std::vector<bool> reached_before_;
};

/// The surface an upgrade makes; nothing for a demolition.
std::optional<RoadSurface> TargetOf(RoadOperation operation) {
  switch (operation) {
    case RoadOperation::kUpgradeToGravel:
      return RoadSurface::kGravel;
    case RoadOperation::kUpgradeToAsphalt:
      return RoadSurface::kAsphalt;
    case RoadOperation::kUpgradeToAsphaltWalks:
      return RoadSurface::kAsphaltWalks;
    case RoadOperation::kDemolish:
    case RoadOperation::kRoadOperationCount:
      return std::nullopt;
  }
  return std::nullopt;
}

/// Why an upgrade to `target` leaves this piece of `road` out.
RoadPieceRefusal UpgradeRefusal(
    const RoadPieceSite& site, const RoadRow& road, RoadSurface target, float from_m, float to_m) {
  if (!site.costs[static_cast<std::size_t>(target)].open) {
    return RoadPieceRefusal::kClosedByEpoch;
  }
  if (road.kind == RoadKind::kPath) {
    return RoadPieceRefusal::kNotThisStep;  // no upgrade makes a path a road
  }
  const auto rank = [](RoadSurface surface) {
    // Walks are asphalt with a pavement: the same rung, one more thing on it.
    switch (surface) {
      case RoadSurface::kDirt:
        return 1;
      case RoadSurface::kGravel:
        return 2;
      case RoadSurface::kAsphalt:
        return 3;
      case RoadSurface::kAsphaltWalks:
        return 4;
      default:
        return 0;
    }
  };
  if (rank(road.surface) >= rank(target)) {
    return RoadPieceRefusal::kAlreadyThat;
  }
  if (road.surface == RoadSurface::kDirt && target != RoadSurface::kGravel) {
    return RoadPieceRefusal::kNotThisStep;  // the chain goes through gravel (§9)
  }
  if (road.surface == RoadSurface::kDirt && site.raster != nullptr && site.raster->HasMap()) {
    for (float s = from_m; s <= to_m; s += kFloodplainSampleMetres) {
      if ((site.raster->FlagsAt(PointAtChainage(road.axis, s)) & kObstacleFloodplain) != 0) {
        return RoadPieceRefusal::kFloodplain;
      }
    }
  }
  if (target == RoadSurface::kAsphaltWalks) {
    const Vec2 middle = PointAtChainage(road.axis, 0.5F * (from_m + to_m));
    const bool in_village = std::any_of(
        site.village.begin(), site.village.end(), [middle](const std::vector<Vec2>& outline) {
          return outline.size() >= 3 && InsidePolygon(middle, outline);
        });
    if (!in_village) {
      return RoadPieceRefusal::kOutsideVillage;
    }
  }
  return RoadPieceRefusal::kNone;
}

}  // namespace

RoadPieces SelectRoadPiecesOn(const RoadPieceSite& site,
                              const RoadSelection& selection,
                              RoadOperation operation) {
  RoadPieces result;
  if (site.roads == nullptr) {
    return result;
  }
  const std::uint32_t row = FindRow(*site.roads, selection.road);
  if (row == kNoRow || site.roads->rows[row].axis.size() < 2) {
    return result;
  }
  const RoadRow& road = site.roads->rows[row];
  const float drag_from = ProjectOntoAxis(road.axis, selection.from).chainage_m;
  const float drag_to = ProjectOntoAxis(road.axis, selection.to).chainage_m;
  const float low = std::min(drag_from, drag_to);
  const float high = std::max(drag_from, drag_to);

  const RoadGraph graph = BuildRoadGraph(*site.roads);
  std::vector<const RoadEdge*> edges;
  for (const RoadEdge& edge : graph.edges) {
    if (edge.road.value == selection.road.value) {
      edges.push_back(&edge);
    }
  }
  std::sort(edges.begin(), edges.end(), [](const RoadEdge* a, const RoadEdge* b) {
    return a->from_chainage_m < b->from_chainage_m;
  });
  // The pieces the drag overlaps, snapped: a click (no length) takes the
  // piece it falls on.
  for (const RoadEdge* edge : edges) {
    const bool overlaps = high > low ? high > edge->from_chainage_m && low < edge->to_chainage_m
                                     : low >= edge->from_chainage_m && low <= edge->to_chainage_m;
    if (!overlaps) {
      continue;
    }
    float from = std::max(low, edge->from_chainage_m);
    float to = std::min(high, edge->to_chainage_m);
    if (high <= low || from - edge->from_chainage_m < kMinRemnantMetres) {
      from = edge->from_chainage_m;
    }
    if (high <= low || edge->to_chainage_m - to < kMinRemnantMetres) {
      to = edge->to_chainage_m;
    }
    RoadPiece piece;
    piece.road = selection.road;
    piece.s_from_m = from;
    piece.s_to_m = to;
    result.pieces.push_back(piece);
  }
  if (drag_from > drag_to) {
    std::reverse(result.pieces.begin(), result.pieces.end());
  }

  const std::optional<RoadSurface> target = TargetOf(operation);
  if (target) {
    for (RoadPiece& piece : result.pieces) {
      piece.refusal = UpgradeRefusal(site, road, *target, piece.s_from_m, piece.s_to_m);
    }
  } else {
    // A demolition: never a road the map keeps, never the only road to
    // something — asked piece by piece, each with those already taken.
    std::unique_ptr<Connectivity> network;
    std::vector<Cut> taken;
    for (RoadPiece& piece : result.pieces) {
      if (road.removable == 0) {
        piece.refusal = RoadPieceRefusal::kStartRoad;
        continue;
      }
      if (!network) {
        network = std::make_unique<Connectivity>(site, graph);
      }
      std::vector<Cut> with_this = taken;
      with_this.push_back(
          Cut{.road = selection.road.value, .from_m = piece.s_from_m, .to_m = piece.s_to_m});
      if (const std::optional<Anchor> stranded = network->FirstStranded(with_this)) {
        piece.refusal = RoadPieceRefusal::kOnlyRoad;
        piece.stranded_unit = stranded->unit;
        piece.stranded_map_road = stranded->map_road;
        continue;
      }
      taken = std::move(with_this);
    }
  }

  // The estimate of the pieces that are in: an upgrade the target's price.
  if (target) {
    const RoadSurfaceCost& cost = site.costs[static_cast<std::size_t>(*target)];
    float metres = 0.0F;
    for (const RoadPiece& piece : result.pieces) {
      metres += piece.refusal == RoadPieceRefusal::kNone ? piece.s_to_m - piece.s_from_m : 0.0F;
    }
    const float hundreds = metres / 100.0F;
    result.estimate.man_days = cost.man_days_per_100m * hundreds;
    result.estimate.materials.resize(cost.materials_per_100m.size(), 0);
    for (std::size_t resource = 0; resource < cost.materials_per_100m.size(); ++resource) {
      result.estimate.materials[resource] = static_cast<Grams>(std::llround(
          static_cast<double>(cost.materials_per_100m[resource]) * static_cast<double>(hundreds)));
    }
  }
  return result;
}

}  // namespace core
