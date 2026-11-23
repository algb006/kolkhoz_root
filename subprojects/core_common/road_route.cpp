#include "core_common/road_route.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <span>
#include <utility>

#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "trail_lattice.h"

namespace core {
namespace {

constexpr float kNoWay = std::numeric_limits<float>::infinity();

/// Cell of the grid the nearest road piece is looked up in, metres.
constexpr float kGridCell = 100.0F;

/// How far beyond the nearest road piece the search still considers others,
/// metres: the nearest one may be a dead-end spur while a road a little
/// farther leads where the traveller is going.
constexpr float kAccessSlack = 150.0F;

/// At most this many road pieces are tried at each end of a way: nine
/// pairings, four ways round each. Eight cost the debug build 290 us a query.
constexpr std::size_t kAccessCandidates = 3;

/// FNV-1a, 64 bits: the day's ground key (DayWeights::key) hashed from the beds and factors.
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

float Distance(Vec2 a, Vec2 b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt((dx * dx) + (dy * dy));
}

/// Whether `mode` may travel along a piece of `kind`: paths are for people
/// (roads design §12: «Дороги — для транспорта, тропинки — для людей»).
bool UsableBy(TravelMode mode, RoadKind kind) {
  return kind == RoadKind::kRoad || mode == TravelMode::kWalk;
}

/// Whether `mode` may leave the road for the whole way (roads design §11):
/// a cart with produce may not.
bool MayGoAcross(TravelMode mode) {
  return mode != TravelMode::kCart;
}

/// One segment of a graph edge's axis, with where it lies along the road.
struct EdgeSegment {
  RoadEdgeIndex edge = 0;
  Vec2 from;
  Vec2 to;
  float from_chainage_m = 0.0F;  // along the ROAD at `from`
  float to_chainage_m = 0.0F;
};

using Access = RoadAccess;

/// How far beyond the network the near table reaches, metres: a place
/// farther than this from every road falls back to the ring search.
constexpr float kNearReach = 400.0F;

/// Edges a near-table cell lists, nearest its centre first.
constexpr std::uint8_t kNearEdges = 8;

struct NearEntry {
  RoadEdgeIndex edge = 0;
  std::uint32_t segment = 0;
  float distance_m = 0.0F;
};

struct NearCell {
  std::array<NearEntry, kNearEdges> entries{};
  std::uint8_t count = 0;
};

/// THE DAY'S WALKING WORLD (#2072's trails and roads.md §3's ground factors, econ's «one
/// composition»): a leg's cost is its weight ÷ the ground's factor, read by every query of the
/// index - so walking time and every reach rule see one world.
struct DayWeights {
  const TrailTable* trails = nullptr;
  /// Per mode, per edge: the cost multiplier (1 ÷ the factor of the edge's ground). Empty for
  /// a mode the ground does not touch (every factor 1).
  std::array<std::vector<float>, kTravelModeCountValue> edge_cost;
  /// Per mode: the open ground's cost multiplier, and on a leg whose trail carries snow (a
  /// snow trail walks as wet ground, roads.md §3).
  std::array<float, kTravelModeCountValue> open_cost{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, kTravelModeCountValue> snow_trail_cost{1.0F, 1.0F, 1.0F, 1.0F};
  /// Per mode with edge costs: its own node-to-node tables (Dijkstra under the costs).
  std::array<std::shared_ptr<const std::vector<float>>, kTravelModeCountValue> distance;
  std::array<std::shared_ptr<const std::vector<RoadEdgeIndex>>, kTravelModeCountValue> arrival;
  /// The beds' states and factors the tables were built for: a day with the same key reuses
  /// them (the recompute only on a day the beds change).
  std::uint64_t key = 0;
};

// The ground's states, the factors and the lattice live in trail_lattice.cpp (lattice::).
using lattice::CostOf;
using lattice::GroundFactorOf;
using lattice::GroundOffRoad;
using lattice::GroundOnRoad;

class RoadIndexImpl final : public RoadIndex {
 public:
  RoadIndexImpl(const RoadTable& roads, const RoadTravelRules& rules)
      : rules_(rules), graph_(BuildRoadGraph(roads)) {
    BuildSegments(roads);
    edge_bed_.assign(graph_.edges.size(), RoadBed::kDirt);
    for (std::size_t edge = 0; edge < graph_.edges.size(); ++edge) {
      const std::uint32_t row = FindRow(roads, graph_.edges[edge].road);
      if (row != kNoRow && roads.rows[row].kind == RoadKind::kRoad) {
        edge_bed_[edge] = BedOf(roads.rows[row].surface);
      }
    }
    // A WORLD WITH NO NETWORK AT ALL has no road for open ground to be slower
    // than: it measures the straight line at the traveller's own pace, as
    // every trip was measured before 0.36.2. Only a hand-built world is so —
    // the game's always has the map's roads — and a cart there still says it
    // had no road.
    if (segments_.empty()) {
      rules_.off_road_weight.fill(1.0F);
    }
    BuildNearTable();
    BuildDistances();
  }

  float EffectiveKm(TravelMode mode, Vec2 from, Vec2 to) const override {
    return EffectiveKmWith(mode, from, to, nullptr);
  }

  // THE DAY'S QUERIES (#2072's trails, roads.md §3's ground; DayWeights): the same queries
  // with the day's costs; nullptr is the bare network. DayRoadIndex below answers through
  // these, so the network is shared and only a mode the ground touches gets its own tables.
  float EffectiveKmWith(TravelMode mode, Vec2 from, Vec2 to, const DayWeights* day) const {
    const std::vector<Access> starts = Accesses(mode, from);
    const std::vector<Access> ends = Accesses(mode, to);
    return Choose(mode, from, to, starts, ends, day).km;
  }

  float EffectiveKmWith(const NetworkPlace& from,
                        const NetworkPlace& to,
                        const DayWeights* day) const {
    return Choose(from.mode,
                  from.position,
                  to.position,
                  std::span<const Access>(from.accesses.data(), from.access_count),
                  std::span<const Access>(to.accesses.data(), to.access_count),
                  day)
        .km;
  }

  NetworkPlace Locate(TravelMode mode, Vec2 position) const override {
    NetworkPlace place;
    place.position = position;
    place.mode = mode;
    const std::vector<Access> found = Accesses(mode, position);
    for (std::size_t index = 0; index < found.size() && index < place.accesses.size(); ++index) {
      place.accesses[index] = found[index];
      ++place.access_count;
    }
    return place;
  }

  float EffectiveKm(const NetworkPlace& from, const NetworkPlace& to) const override {
    return EffectiveKmWith(from, to, nullptr);
  }

  RouteMeasure Measure(TravelMode mode, Vec2 from, Vec2 to) const override {
    return MeasureWith(mode, from, to, nullptr);
  }

  RouteMeasure MeasureWith(TravelMode mode, Vec2 from, Vec2 to, const DayWeights* day) const {
    const std::vector<Access> starts = Accesses(mode, from);
    const std::vector<Access> ends = Accesses(mode, to);
    const Choice choice = Choose(mode, from, to, starts, ends, day);
    RouteMeasure measure;
    measure.effective_km = choice.km;
    // THE SAME CHOICE Way unfolds: across open ground the whole line is off
    // the road; on the network, the two pieces that join it.
    const bool across = choice.kind == Choice::kStraight || choice.kind == Choice::kNoRoad;
    measure.off_road_start_m = across ? Distance(from, to) : choice.start.distance_m;
    measure.off_road_end_m = across ? 0.0F : choice.end.distance_m;
    measure.off_road_m = measure.off_road_start_m + measure.off_road_end_m;
    return measure;
  }

  Route Way(TravelMode mode, Vec2 from, Vec2 to) const override {
    return WayWith(mode, from, to, nullptr);
  }

  Route WayWith(TravelMode mode, Vec2 from, Vec2 to, const DayWeights* day) const {
    const auto index = static_cast<std::size_t>(mode);
    const std::vector<Access> starts = Accesses(mode, from);
    const std::vector<Access> ends = Accesses(mode, to);
    const Choice choice = Choose(mode, from, to, starts, ends, day);
    if (choice.kind == Choice::kStraight || choice.kind == Choice::kNoRoad) {
      Route route = Straight(from, to, LegCost(mode, from, to, day));
      route.cart_without_road = choice.kind == Choice::kNoRoad;
      return route;
    }
    // THE LEGS ONLY FOR THE WAY CHOSEN: the choice itself is arithmetic, and
    // the places that only need a number never build a leg (0.36.1: building
    // the legs of every candidate cost 290 us a query in the debug build).
    Route route;
    const RouteLeg in =
        OpenLeg(from, choice.start.point, LegCost(mode, from, choice.start.point, day));
    const RouteLeg out = OpenLeg(choice.end.point, to, LegCost(mode, choice.end.point, to, day));
    route.legs.push_back(in);
    if (choice.kind == Choice::kSameEdge) {
      route.legs.push_back(
          RoadLeg(choice.start.edge, choice.start.chainage_m, choice.end.chainage_m, mode, day));
    } else {
      const RoadEdge& first = graph_.edges[choice.start.edge];
      const RoadEdge& last = graph_.edges[choice.end.edge];
      const RoadNodeIndex exit_node = choice.start_forward ? first.to : first.from;
      const float exit_chainage =
          choice.start_forward ? first.to_chainage_m : first.from_chainage_m;
      const RoadNodeIndex entry_node = choice.end_forward ? last.from : last.to;
      const float entry_chainage = choice.end_forward ? last.from_chainage_m : last.to_chainage_m;
      route.legs.push_back(
          RoadLeg(choice.start.edge, choice.start.chainage_m, exit_chainage, mode, day));
      AppendNodeWay(index, exit_node, entry_node, route.legs, day);
      route.legs.push_back(
          RoadLeg(choice.end.edge, entry_chainage, choice.end.chainage_m, mode, day));
    }
    route.legs.push_back(out);
    route.effective_km = choice.km;
    return route;
  }

  const RoadGraph& Graph() const override { return graph_; }

  float OffRoadWeight(TravelMode mode) const override {
    const auto index = static_cast<std::size_t>(mode);
    return index < rules_.off_road_weight.size() ? rules_.off_road_weight[index] : 1.0F;
  }

 private:
  /// The way chosen, as numbers: what EffectiveKm answers and Way unfolds.
  struct Choice {
    enum Kind : std::uint8_t { kStraight, kNoRoad, kSameEdge, kByNodes };

    Kind kind = kStraight;
    float km = kNoWay;
    Access start;
    Access end;
    bool start_forward = true;
    bool end_forward = true;
  };

  /// An open-ground leg's cost a km: the mode's weight, lowered by a trail for a walker
  /// (#2072), divided by the open ground's factor - a snow trail's as wet ground (roads.md §3).
  float LegCost(TravelMode mode, Vec2 from, Vec2 to, const DayWeights* day) const {
    const auto mode_index = static_cast<std::size_t>(mode);
    const float weight = rules_.off_road_weight[mode_index];
    if (day == nullptr) {
      return weight;
    }
    if (day->trails == nullptr || mode != TravelMode::kWalk || day->trails->rows.empty()) {
      return weight * day->open_cost[mode_index];
    }
    // THE QUERY DOOR (#2072 (A)): the straight line weighted by the lattice edges it crosses.
    const float length = Distance(from, to);
    if (!(length > 0.0F)) {
      return weight * day->open_cost[mode_index];
    }
    return lattice::StraightKm(*day->trails,
                               from,
                               to,
                               weight,
                               day->open_cost[mode_index],
                               day->snow_trail_cost[mode_index]) /
           (length / 1000.0F);
  }

 public:
  /// THE WALKER'S DOOR (#2072 (A)): the walking way's km with every open-ground leg routed
  /// over the lattice (lattice::RouteKm), its steps into `edges`; the network legs as Way's.
  float WalkRouteKmWith(Vec2 from,
                        Vec2 to,
                        const DayWeights* day,
                        std::vector<std::pair<TrailEnd, TrailEnd>>* edges) const {
    const Route way = WayWith(TravelMode::kWalk, from, to, day);
    if (day == nullptr || day->trails == nullptr) {
      return way.effective_km;
    }
    const auto walk = static_cast<std::size_t>(TravelMode::kWalk);
    float km = 0.0F;
    for (const RouteLeg& leg : way.legs) {
      if (leg.on_road) {
        km += leg.effective_km;
        continue;
      }
      km += lattice::RouteKm(*day->trails,
                             leg.from,
                             leg.to,
                             rules_.off_road_weight[walk],
                             day->open_cost[walk],
                             day->snow_trail_cost[walk],
                             edges);
    }
    return km;
  }

 private:
  /// A road piece's cost multiplier: its ground's (DayWeights::edge_cost), 1 bare.
  static float EdgeCost(TravelMode mode, RoadEdgeIndex edge, const DayWeights* day) {
    if (day == nullptr) {
      return 1.0F;
    }
    const std::vector<float>& costs = day->edge_cost[static_cast<std::size_t>(mode)];
    return edge < costs.size() ? costs[edge] : 1.0F;
  }

  /// Node-to-node km for a mode: the day's own table when the ground touches it.
  float Between(std::size_t mode_index, std::size_t offset, const DayWeights* day) const {
    if (day != nullptr && day->distance[mode_index] != nullptr) {
      return (*day->distance[mode_index])[offset];
    }
    return distance_[mode_index][offset];
  }

  Choice Choose(TravelMode mode,
                Vec2 from,
                Vec2 to,
                std::span<const Access> starts,
                std::span<const Access> ends,
                const DayWeights* day) const {
    const float weight = rules_.off_road_weight[static_cast<std::size_t>(mode)];
    const auto mode_index = static_cast<std::size_t>(mode);
    Choice best;
    if (MayGoAcross(mode)) {
      best.kind = Choice::kStraight;
      best.km = Distance(from, to) / 1000.0F * LegCost(mode, from, to, day);
    }
    const std::size_t nodes = graph_.nodes.size();
    // Each end's open-ground leg priced ONCE (static review of 23 Nov 2026: inside the pairs'
    // loop the exit leg's lattice walk was repeated for every start).
    std::array<float, kAccessCandidates> out_kms{};
    for (std::size_t at = 0; at < ends.size() && at < out_kms.size(); ++at) {
      out_kms[at] = ends[at].distance_m / 1000.0F * LegCost(mode, ends[at].point, to, day);
    }
    for (const Access& start : starts) {
      const float in_km = start.distance_m / 1000.0F * LegCost(mode, from, start.point, day);
      const RoadEdge& first = graph_.edges[start.edge];
      const float first_cost = EdgeCost(mode, start.edge, day);
      for (std::size_t at = 0; at < ends.size() && at < out_kms.size(); ++at) {
        const Access& end = ends[at];
        const float out_km = out_kms[at];
        const float last_cost = EdgeCost(mode, end.edge, day);
        if (start.edge == end.edge) {
          const float km =
              in_km + (std::abs(end.chainage_m - start.chainage_m) / 1000.0F * first_cost) + out_km;
          if (km < best.km) {
            best = Choice{.kind = Choice::kSameEdge, .km = km, .start = start, .end = end};
          }
        }
        const RoadEdge& last = graph_.edges[end.edge];
        for (const bool start_forward : {true, false}) {
          const RoadNodeIndex exit_node = start_forward ? first.to : first.from;
          const float exit_chainage = start_forward ? first.to_chainage_m : first.from_chainage_m;
          const float to_exit = std::abs(exit_chainage - start.chainage_m) / 1000.0F * first_cost;
          for (const bool end_forward : {true, false}) {
            const RoadNodeIndex entry_node = end_forward ? last.from : last.to;
            const float entry_chainage = end_forward ? last.from_chainage_m : last.to_chainage_m;
            const float between = Between(mode_index, (exit_node * nodes) + entry_node, day);
            const float km = in_km + to_exit + between +
                             (std::abs(end.chainage_m - entry_chainage) / 1000.0F * last_cost) +
                             out_km;
            if (km < best.km) {
              best = Choice{.kind = Choice::kByNodes,
                            .km = km,
                            .start = start,
                            .end = end,
                            .start_forward = start_forward,
                            .end_forward = end_forward};
            }
          }
        }
      }
    }
    if (!(best.km < kNoWay)) {
      // A cart with no road to go by: the straight line, said so.
      best.kind = Choice::kNoRoad;
      best.km = Distance(from, to) / 1000.0F * weight;
    }
    return best;
  }

  static Route Straight(Vec2 from, Vec2 to, float weight) {
    Route route;
    RouteLeg leg;
    leg.from = from;
    leg.to = to;
    leg.length_m = Distance(from, to);
    leg.effective_km = leg.length_m / 1000.0F * weight;
    route.effective_km = leg.effective_km;
    route.legs.push_back(leg);
    return route;
  }

  void BuildSegments(const RoadTable& roads) {
    for (RoadEdgeIndex edge_index = 0; edge_index < graph_.edges.size(); ++edge_index) {
      const RoadEdge& edge = graph_.edges[edge_index];
      const std::uint32_t row = FindRow(roads, edge.road);
      if (row == kNoRow) {
        continue;
      }
      const std::vector<RoadPoint>& axis = roads.rows[row].axis;
      float walked = 0.0F;
      for (std::size_t vertex = 1; vertex < axis.size(); ++vertex) {
        const Vec2 a = axis[vertex - 1].position;
        const Vec2 b = axis[vertex].position;
        const float step = Distance(a, b);
        const float from_chainage = walked;
        const float to_chainage = walked + step;
        walked = to_chainage;
        // The part of this axis segment the edge covers.
        const float low = std::max(from_chainage, edge.from_chainage_m);
        const float high = std::min(to_chainage, edge.to_chainage_m);
        if (!(high > low) || !(step > 0.0F)) {
          continue;
        }
        const auto point_at = [&](float chainage) {
          const float share = (chainage - from_chainage) / step;
          return Vec2{.x = a.x + ((b.x - a.x) * share), .y = a.y + ((b.y - a.y) * share)};
        };
        const EdgeSegment segment{.edge = edge_index,
                                  .from = point_at(low),
                                  .to = point_at(high),
                                  .from_chainage_m = low,
                                  .to_chainage_m = high};
        const auto id = static_cast<std::uint32_t>(segments_.size());
        segments_.push_back(segment);
        const auto cell = [](float value) {
          return static_cast<std::int32_t>(std::floor(value / kGridCell));
        };
        for (std::int32_t cx = cell(std::min(segment.from.x, segment.to.x));
             cx <= cell(std::max(segment.from.x, segment.to.x));
             ++cx) {
          for (std::int32_t cy = cell(std::min(segment.from.y, segment.to.y));
               cy <= cell(std::max(segment.from.y, segment.to.y));
               ++cy) {
            grid_[{cx, cy}].push_back(id);
          }
        }
      }
    }
  }

  /// Dijkstra from every node, by mode: the node-to-node effective km and,
  /// for the legs, the edge each shortest way arrives by.
  void BuildDistances() {
    for (std::size_t mode = 0; mode < kTravelModeCountValue; ++mode) {
      ModeDistances(mode, {}, distance_[mode], arrival_[mode]);
    }
  }

  /// One mode's node-to-node tables under per-edge cost multipliers (empty: 1 everywhere).
  void ModeDistances(std::size_t mode,
                     std::span<const float> edge_cost,
                     std::vector<float>& distance_out,
                     std::vector<RoadEdgeIndex>& arrival_out) const {
    const std::size_t nodes = graph_.nodes.size();
    distance_out.assign(nodes * nodes, kNoWay);
    arrival_out.assign(nodes * nodes, kNoEdge);
    for (std::size_t source = 0; source < nodes; ++source) {
      float* distance = &distance_out[source * nodes];
      RoadEdgeIndex* arrival = &arrival_out[source * nodes];
      using Item = std::pair<float, RoadNodeIndex>;
      std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
      distance[source] = 0.0F;
      open.emplace(0.0F, static_cast<RoadNodeIndex>(source));
      while (!open.empty()) {
        const auto [here_km, node] = open.top();
        open.pop();
        if (here_km > distance[node]) {
          continue;
        }
        for (const RoadEdgeIndex edge_index : graph_.node_edges[node]) {
          const RoadEdge& edge = graph_.edges[edge_index];
          if (!UsableBy(static_cast<TravelMode>(mode), edge.kind)) {
            continue;
          }
          const RoadNodeIndex other = edge.from == node ? edge.to : edge.from;
          const float cost = edge_index < edge_cost.size() ? edge_cost[edge_index] : 1.0F;
          const float next = here_km + (edge.length_m / 1000.0F * cost);
          // Ties broken by the edge index, so the way is the same on every
          // compiler.
          if (next < distance[other] || (next == distance[other] && edge_index < arrival[other])) {
            distance[other] = next;
            arrival[other] = edge_index;
            open.emplace(next, other);
          }
        }
      }
    }
  }

 public:
  /// THE DAY (#2072 + roads.md §3): the trails and today's ground on this network. A mode the
  /// ground touches (any edge cost not 1) gets its own node tables - taken from `previous` when
  /// its key matches (the beds unchanged since), recomputed otherwise.
  std::shared_ptr<const DayWeights> MakeDay(const TrailTable* trails,
                                            const RoadBeds& beds,
                                            const DayWeights* previous) const {
    auto day = std::make_shared<DayWeights>();
    day->trails = trails;
    const GroundFactors& factors = trails != nullptr ? trails->ground : GroundFactors{};
    // The key: the three beds' conditions and both modes' factors.
    std::uint64_t key = kFnvOffsetBasis;
    const auto mix = [&key](std::uint32_t value) { key = (key ^ value) * kFnvPrime; };
    for (const RoadCondition condition : beds.condition) {
      mix(static_cast<std::uint32_t>(condition));
    }
    for (std::size_t index = 0; index < kGroundStateCountValue; ++index) {
      mix(std::bit_cast<std::uint32_t>(factors.walk[index]));
      mix(std::bit_cast<std::uint32_t>(factors.ride[index]));
    }
    day->key = key;
    const RoadCondition dirt = beds.condition[static_cast<std::size_t>(RoadBed::kDirt)];
    for (const TravelMode mode : {TravelMode::kWalk, TravelMode::kTeam}) {
      const auto mode_index = static_cast<std::size_t>(mode);
      day->open_cost[mode_index] = CostOf(GroundFactorOf(factors, mode, GroundOffRoad(dirt)));
      // A snow trail walks as wet ground; off snow it is the open ground's own.
      day->snow_trail_cost[mode_index] =
          GroundOffRoad(dirt) == GroundState::kSnow
              ? CostOf(GroundFactorOf(factors, mode, GroundState::kWet))
              : day->open_cost[mode_index];
      std::vector<float> costs(graph_.edges.size(), 1.0F);
      bool touched = false;
      for (std::size_t edge = 0; edge < graph_.edges.size(); ++edge) {
        const RoadBed bed = edge_bed_[edge];
        const GroundState state = GroundOnRoad(beds.condition[static_cast<std::size_t>(bed)], bed);
        costs[edge] = CostOf(GroundFactorOf(factors, mode, state));
        touched = touched || costs[edge] != 1.0F;
      }
      if (!touched) {
        continue;
      }
      if (previous != nullptr && previous->key == key && previous->distance[mode_index]) {
        day->distance[mode_index] = previous->distance[mode_index];
        day->arrival[mode_index] = previous->arrival[mode_index];
      } else {
        auto distance = std::make_shared<std::vector<float>>();
        auto arrival = std::make_shared<std::vector<RoadEdgeIndex>>();
        ModeDistances(mode_index, costs, *distance, *arrival);
        day->distance[mode_index] = std::move(distance);
        day->arrival[mode_index] = std::move(arrival);
        g_day_table_builds.fetch_add(1, std::memory_order_relaxed);
      }
      day->edge_cost[mode_index] = std::move(costs);
    }
    return day;
  }

  /// The ground's state at a point: a road or path piece within kOnRoadMetres takes its bed's
  /// state, open ground the dirt bed's (ISession::GroundStateAt). Never kIce (STUB).
  GroundState GroundAt(Vec2 point, const RoadBeds& beds) const {
    constexpr float kOnRoadMetres = 4.0F;
    const std::vector<Access> near = Accesses(TravelMode::kWalk, point);
    if (!near.empty() && near.front().distance_m <= kOnRoadMetres) {
      const RoadBed bed = edge_bed_[near.front().edge];
      return GroundOnRoad(beds.condition[static_cast<std::size_t>(bed)], bed);
    }
    return GroundOffRoad(beds.condition[static_cast<std::size_t>(RoadBed::kDirt)]);
  }

  /// How many day tables were computed in this process (the run prints the recompute cost);
  /// atomic because a run's villages walk on several threads, and it is counted rarely.
  static inline std::atomic<std::uint64_t> g_day_table_builds{0};

 private:
  /// THE NEAR TABLE (0.36.2): every cell of a dense grid over the network,
  /// out to kNearReach beyond it, holds the kNearEdges edges nearest its
  /// centre and, for each, the segment nearest it — per usability class
  /// (roads only; roads and paths). Built by letting every segment tell the
  /// cells within reach about itself, so the cost is the segments' count
  /// times a few dozen cells, once a network. A place then reads one cell and
  /// walks each listed edge to its own nearest point: a few segments, not
  /// the hundreds a ring search visits.
  void BuildNearTable() {
    if (segments_.empty()) {
      return;
    }
    float low_x = segments_[0].from.x;
    float low_y = segments_[0].from.y;
    float high_x = low_x;
    float high_y = low_y;
    for (const EdgeSegment& segment : segments_) {
      for (const Vec2 point : {segment.from, segment.to}) {
        low_x = std::min(low_x, point.x);
        low_y = std::min(low_y, point.y);
        high_x = std::max(high_x, point.x);
        high_y = std::max(high_y, point.y);
      }
    }
    near_origin_ = Vec2{.x = low_x - kNearReach, .y = low_y - kNearReach};
    near_columns_ =
        static_cast<std::int32_t>(std::ceil((high_x - low_x + (2.0F * kNearReach)) / kGridCell)) +
        1;
    near_rows_ =
        static_cast<std::int32_t>(std::ceil((high_y - low_y + (2.0F * kNearReach)) / kGridCell)) +
        1;
    const auto cells =
        static_cast<std::size_t>(near_columns_) * static_cast<std::size_t>(near_rows_);
    near_table_.assign(2 * cells, NearCell{});
    const auto reach_cells = static_cast<std::int32_t>(std::ceil(kNearReach / kGridCell));
    for (std::uint32_t id = 0; id < segments_.size(); ++id) {
      const EdgeSegment& segment = segments_[id];
      const bool is_road = graph_.edges[segment.edge].kind == RoadKind::kRoad;
      const auto cell_of = [&](float value, float origin) {
        return static_cast<std::int32_t>(std::floor((value - origin) / kGridCell));
      };
      const std::int32_t from_x = std::max(
          0, cell_of(std::min(segment.from.x, segment.to.x), near_origin_.x) - reach_cells);
      const std::int32_t to_x =
          std::min(near_columns_ - 1,
                   cell_of(std::max(segment.from.x, segment.to.x), near_origin_.x) + reach_cells);
      const std::int32_t from_y = std::max(
          0, cell_of(std::min(segment.from.y, segment.to.y), near_origin_.y) - reach_cells);
      const std::int32_t to_y =
          std::min(near_rows_ - 1,
                   cell_of(std::max(segment.from.y, segment.to.y), near_origin_.y) + reach_cells);
      for (std::int32_t cx = from_x; cx <= to_x; ++cx) {
        for (std::int32_t cy = from_y; cy <= to_y; ++cy) {
          const Vec2 centre{.x = near_origin_.x + ((static_cast<float>(cx) + 0.5F) * kGridCell),
                            .y = near_origin_.y + ((static_cast<float>(cy) + 0.5F) * kGridCell)};
          const float distance = SegmentDistance(segment, centre);
          const std::size_t cell =
              (static_cast<std::size_t>(cy) * static_cast<std::size_t>(near_columns_)) +
              static_cast<std::size_t>(cx);
          // Class 1 (roads and paths) hears every segment; class 0 roads only.
          Offer(near_table_[cells + cell], segment.edge, id, distance);
          if (is_road) {
            Offer(near_table_[cell], segment.edge, id, distance);
          }
        }
      }
    }
  }

  /// Keeps a cell's nearest edges: one entry an edge, its nearest segment,
  /// the kNearEdges nearest edges, ties by edge index.
  static void Offer(NearCell& cell, RoadEdgeIndex edge, std::uint32_t segment, float distance) {
    for (std::uint8_t slot = 0; slot < cell.count; ++slot) {
      if (cell.entries[slot].edge == edge) {
        if (distance < cell.entries[slot].distance_m) {
          cell.entries[slot].segment = segment;
          cell.entries[slot].distance_m = distance;
        }
        return;
      }
    }
    NearEntry entry{.edge = edge, .segment = segment, .distance_m = distance};
    if (cell.count < kNearEdges) {
      cell.entries[cell.count] = entry;
      ++cell.count;
      return;
    }
    std::uint8_t worst = 0;
    for (std::uint8_t slot = 1; slot < cell.count; ++slot) {
      const NearEntry& a = cell.entries[slot];
      const NearEntry& b = cell.entries[worst];
      if (a.distance_m > b.distance_m || (a.distance_m == b.distance_m && a.edge > b.edge)) {
        worst = slot;
      }
    }
    const NearEntry& out = cell.entries[worst];
    if (distance < out.distance_m || (distance == out.distance_m && edge < out.edge)) {
      cell.entries[worst] = entry;
    }
  }

  static float SegmentDistance(const EdgeSegment& segment, Vec2 place) {
    return Distance(Foot(segment, place).first, place);
  }

  /// The nearest point of a segment to `place`, and its share along it.
  static std::pair<Vec2, float> Foot(const EdgeSegment& segment, Vec2 place) {
    const float sx = segment.to.x - segment.from.x;
    const float sy = segment.to.y - segment.from.y;
    const float length_sq = (sx * sx) + (sy * sy);
    float share = 0.0F;
    if (length_sq > 0.0F) {
      share = std::clamp(
          (((place.x - segment.from.x) * sx) + ((place.y - segment.from.y) * sy)) / length_sq,
          0.0F,
          1.0F);
    }
    return {Vec2{.x = segment.from.x + (sx * share), .y = segment.from.y + (sy * share)}, share};
  }

  Access AccessAt(std::uint32_t id, Vec2 place) const {
    const EdgeSegment& segment = segments_[id];
    const auto [foot, share] = Foot(segment, place);
    return Access{.edge = segment.edge,
                  .chainage_m = segment.from_chainage_m +
                                ((segment.to_chainage_m - segment.from_chainage_m) * share),
                  .point = foot,
                  .distance_m = Distance(foot, place)};
  }

  /// From a listed segment, along its edge while the distance falls: the
  /// edge's nearest point to `place` near the listed one.
  Access DescendAlongEdge(std::uint32_t id, Vec2 place) const {
    Access best = AccessAt(id, place);
    const RoadEdgeIndex edge = segments_[id].edge;
    for (const int step : {-1, 1}) {
      std::int64_t at = static_cast<std::int64_t>(id) + step;
      while (at >= 0 && at < static_cast<std::int64_t>(segments_.size()) &&
             segments_[static_cast<std::size_t>(at)].edge == edge) {
        const Access next = AccessAt(static_cast<std::uint32_t>(at), place);
        if (!(next.distance_m < best.distance_m)) {
          break;
        }
        best = next;
        at += step;
      }
    }
    return best;
  }

  /// The road pieces a place may reach the network by, nearest first.
  std::vector<Access> Accesses(TravelMode mode, Vec2 place) const {
    std::vector<Access> found;
    if (segments_.empty()) {
      return found;
    }
    // THE NEAR TABLE FIRST; a place beyond its reach falls back to the ring
    // search below.
    const auto near_x =
        static_cast<std::int32_t>(std::floor((place.x - near_origin_.x) / kGridCell));
    const auto near_y =
        static_cast<std::int32_t>(std::floor((place.y - near_origin_.y) / kGridCell));
    if (rules_.near_table && near_x >= 0 && near_y >= 0 && near_x < near_columns_ &&
        near_y < near_rows_) {
      const std::size_t cells =
          static_cast<std::size_t>(near_columns_) * static_cast<std::size_t>(near_rows_);
      const std::size_t cell =
          (static_cast<std::size_t>(near_y) * static_cast<std::size_t>(near_columns_)) +
          static_cast<std::size_t>(near_x);
      const NearCell& near = near_table_[(mode == TravelMode::kWalk ? cells : 0) + cell];
      float nearest = kNoWay;
      for (std::uint8_t slot = 0; slot < near.count; ++slot) {
        const Access access = DescendAlongEdge(near.entries[slot].segment, place);
        nearest = std::min(nearest, access.distance_m);
        found.push_back(access);
      }
      // THE TABLE ANSWERS ONLY WITHIN ITS REACH: an edge within the slack of
      // the nearest must be one the cell could have heard of — kNearReach
      // less the cell's half-diagonal. A place farther out asks the rings
      // (0.36.2: a unit 250 m from the nearest road took a way 23 % longer,
      // the better road lying past the table's 300 m).
      constexpr float kCellHalfDiagonal = kGridCell * 0.7072F;
      if (!(nearest + kAccessSlack <= kNearReach - kCellHalfDiagonal)) {
        found.clear();
      } else {
        std::erase_if(found, [&](const Access& access) {
          return access.distance_m > nearest + kAccessSlack;
        });
        std::ranges::sort(found, [](const Access& left, const Access& right) {
          return left.distance_m != right.distance_m ? left.distance_m < right.distance_m
                                                     : left.edge < right.edge;
        });
        if (found.size() > kAccessCandidates) {
          found.resize(kAccessCandidates);
        }
        return found;
      }
    }
    const auto cx = static_cast<std::int32_t>(std::floor(place.x / kGridCell));
    const auto cy = static_cast<std::int32_t>(std::floor(place.y / kGridCell));
    float nearest = kNoWay;
    std::vector<Access> per_edge;  // one entry an edge, its nearest point
    // Rings of cells outward until the ring is past the nearest piece plus the
    // slack; a place far from every road still finds the nearest.
    for (std::int32_t ring = 0; ring < 100; ++ring) {
      const float ring_near = static_cast<float>(ring > 0 ? ring - 1 : 0) * kGridCell;
      if (ring_near > nearest + kAccessSlack) {
        break;
      }
      for (std::int32_t dx = -ring; dx <= ring; ++dx) {
        for (std::int32_t dy = -ring; dy <= ring; ++dy) {
          if (std::max(std::abs(dx), std::abs(dy)) != ring) {
            continue;
          }
          const auto cell = grid_.find({cx + dx, cy + dy});
          if (cell == grid_.end()) {
            continue;
          }
          for (const std::uint32_t id : cell->second) {
            const EdgeSegment& segment = segments_[id];
            if (!UsableBy(mode, graph_.edges[segment.edge].kind)) {
              continue;
            }
            const float sx = segment.to.x - segment.from.x;
            const float sy = segment.to.y - segment.from.y;
            const float length_sq = (sx * sx) + (sy * sy);
            float share = 0.0F;
            if (length_sq > 0.0F) {
              share = std::clamp(
                  (((place.x - segment.from.x) * sx) + ((place.y - segment.from.y) * sy)) /
                      length_sq,
                  0.0F,
                  1.0F);
            }
            const Vec2 foot{.x = segment.from.x + (sx * share), .y = segment.from.y + (sy * share)};
            const float distance = Distance(foot, place);
            nearest = std::min(nearest, distance);
            const Access access{
                .edge = segment.edge,
                .chainage_m = segment.from_chainage_m +
                              ((segment.to_chainage_m - segment.from_chainage_m) * share),
                .point = foot,
                .distance_m = distance};
            const auto known = std::ranges::find_if(
                per_edge, [&](const Access& seen) { return seen.edge == segment.edge; });
            if (known == per_edge.end()) {
              per_edge.push_back(access);
            } else if (distance < known->distance_m) {
              *known = access;
            }
          }
        }
      }
    }
    for (const Access& access : per_edge) {
      if (access.distance_m <= nearest + kAccessSlack) {
        found.push_back(access);
      }
    }
    std::ranges::sort(found, [](const Access& left, const Access& right) {
      return left.distance_m != right.distance_m ? left.distance_m < right.distance_m
                                                 : left.edge < right.edge;
    });
    if (found.size() > kAccessCandidates) {
      found.resize(kAccessCandidates);
    }
    return found;
  }

  RouteLeg RoadLeg(RoadEdgeIndex edge_index,
                   float from_chainage,
                   float to_chainage,
                   TravelMode mode,
                   const DayWeights* day) const {
    const RoadEdge& edge = graph_.edges[edge_index];
    RouteLeg leg;
    leg.on_road = true;
    leg.road = edge.road;
    leg.from_chainage_m = from_chainage;
    leg.to_chainage_m = to_chainage;
    leg.length_m = std::abs(to_chainage - from_chainage);
    leg.effective_km = leg.length_m / 1000.0F * EdgeCost(mode, edge_index, day);
    return leg;
  }

  RouteLeg OpenLeg(Vec2 from, Vec2 to, float weight) const {
    RouteLeg leg;
    leg.from = from;
    leg.to = to;
    leg.length_m = Distance(from, to);
    leg.effective_km = leg.length_m / 1000.0F * weight;
    return leg;
  }

  /// The edges of the shortest way from `source` to `target`, in order.
  void AppendNodeWay(std::size_t mode,
                     RoadNodeIndex source,
                     RoadNodeIndex target,
                     std::vector<RouteLeg>& legs,
                     const DayWeights* day) const {
    const std::size_t nodes = graph_.nodes.size();
    const auto travel = static_cast<TravelMode>(mode);
    const std::vector<RoadEdgeIndex>& arrival =
        day != nullptr && day->arrival[mode] != nullptr ? *day->arrival[mode] : arrival_[mode];
    std::vector<RouteLeg> backwards;
    RoadNodeIndex node = target;
    while (node != source) {
      const RoadEdgeIndex edge_index = arrival[(source * nodes) + node];
      if (edge_index == kNoEdge) {
        return;  // unreachable; the caller has already refused the way
      }
      const RoadEdge& edge = graph_.edges[edge_index];
      const bool forward = edge.to == node;
      backwards.push_back(
          forward ? RoadLeg(edge_index, edge.from_chainage_m, edge.to_chainage_m, travel, day)
                  : RoadLeg(edge_index, edge.to_chainage_m, edge.from_chainage_m, travel, day));
      node = forward ? edge.from : edge.to;
    }
    legs.insert(legs.end(), backwards.rbegin(), backwards.rend());
  }

  static constexpr RoadEdgeIndex kNoEdge = 0xFFFFFFFFU;

  RoadTravelRules rules_;
  RoadGraph graph_;
  std::vector<EdgeSegment> segments_;
  std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::uint32_t>> grid_;
  Vec2 near_origin_;
  std::int32_t near_columns_ = 0;
  std::int32_t near_rows_ = 0;
  /// Two classes, roads only then roads and paths, each columns x rows.
  std::vector<NearCell> near_table_;
  std::array<std::vector<float>, kTravelModeCountValue> distance_;
  std::array<std::vector<RoadEdgeIndex>, kTravelModeCountValue> arrival_;
  /// Each edge's bed (its road's surface; a path and dirt are earth): the ground's state.
  std::vector<RoadBed> edge_bed_;
};

/// THE NETWORK ON THIS DAY (#2072's trails, roads.md §3's ground): the bare index shared, a
/// copy of the trails and the day's weights held - immutable like the index, so the double
/// buffer still copies a pointer. Every query is the bare index's own under the day's costs.
class DayRoadIndex final : public RoadIndex {
 public:
  DayRoadIndex(std::shared_ptr<const RoadIndexImpl> base,
               TrailTable trails,
               const RoadBeds& beds,
               const DayWeights* previous)
      : base_(std::move(base)), trails_(std::move(trails)), beds_(beds) {
    day_ = base_->MakeDay(&trails_, beds_, previous);  // after trails_ is in place
    // THE DISTANCE, NOT THE TIME (boss 23 Nov; leisure.md §4: reach is metres of the way):
    // the same trails on dry ground - the ground's factors slow a walk, they do not move a
    // place farther. Neutral, so it computes no node table.
    distance_day_ = base_->MakeDay(&trails_, RoadBeds{}, nullptr);
  }

  const DayWeights* DistanceDay() const { return distance_day_.get(); }

  float EffectiveKm(TravelMode mode, Vec2 from, Vec2 to) const override {
    return base_->EffectiveKmWith(mode, from, to, day_.get());
  }

  NetworkPlace Locate(TravelMode mode, Vec2 position) const override {
    return base_->Locate(mode, position);
  }

  float EffectiveKm(const NetworkPlace& from, const NetworkPlace& to) const override {
    return base_->EffectiveKmWith(from, to, day_.get());
  }

  RouteMeasure Measure(TravelMode mode, Vec2 from, Vec2 to) const override {
    return base_->MeasureWith(mode, from, to, day_.get());
  }

  Route Way(TravelMode mode, Vec2 from, Vec2 to) const override {
    return base_->WayWith(mode, from, to, day_.get());
  }

  const RoadGraph& Graph() const override { return base_->Graph(); }

  float OffRoadWeight(TravelMode mode) const override { return base_->OffRoadWeight(mode); }

  const std::shared_ptr<const RoadIndexImpl>& Base() const { return base_; }

  const DayWeights* Day() const { return day_.get(); }

 private:
  std::shared_ptr<const RoadIndexImpl> base_;
  TrailTable trails_;
  RoadBeds beds_;
  std::shared_ptr<const DayWeights> day_;
  std::shared_ptr<const DayWeights> distance_day_;
};

/// The bare network under any index of ours, and the day it carried (or nullptr).
std::shared_ptr<const RoadIndexImpl> BareOf(const std::shared_ptr<const RoadIndex>& index,
                                            const DayWeights** day) {
  *day = nullptr;
  if (auto bare = std::dynamic_pointer_cast<const RoadIndexImpl>(index)) {
    return bare;
  }
  if (const auto dated = std::dynamic_pointer_cast<const DayRoadIndex>(index)) {
    *day = dated->Day();
    return dated->Base();
  }
  return nullptr;
}

/// True when the ground touches nobody today: every bed and open ground at factor 1.
bool GroundIsNeutral(const GroundFactors& factors, const RoadBeds& beds) {
  for (std::size_t bed = 0; bed < kRoadBedCountValue; ++bed) {
    const auto kind = static_cast<RoadBed>(bed);
    const GroundState on_road = GroundOnRoad(beds.condition[bed], kind);
    for (const TravelMode mode : {TravelMode::kWalk, TravelMode::kTeam}) {
      if (GroundFactorOf(factors, mode, on_road) != 1.0F) {
        return false;
      }
    }
  }
  const GroundState open = GroundOffRoad(beds.condition[static_cast<std::size_t>(RoadBed::kDirt)]);
  return GroundFactorOf(factors, TravelMode::kWalk, open) == 1.0F &&
         GroundFactorOf(factors, TravelMode::kTeam, open) == 1.0F;
}

}  // namespace

std::shared_ptr<const RoadIndex> BuildRoadIndex(const RoadTable& roads,
                                                const RoadTravelRules& rules) {
  return std::make_shared<RoadIndexImpl>(roads, rules);
}

std::shared_ptr<const RoadIndex> WithTrails(const std::shared_ptr<const RoadIndex>& index,
                                            const TrailTable& trails,
                                            const RoadBeds& beds) {
  const DayWeights* previous = nullptr;
  std::shared_ptr<const RoadIndexImpl> base = BareOf(index, &previous);
  if (base == nullptr) {
    return index;  // a foreign index (a test's): nothing to wrap
  }
  if (trails.rows.empty() && GroundIsNeutral(trails.ground, beds)) {
    return base;  // no trail, no ground to slow anyone: the bare network
  }
  return std::make_shared<DayRoadIndex>(std::move(base), trails, beds, previous);
}

std::shared_ptr<const RoadIndex> BuildRoadIndex(const RoadTable& roads,
                                                const TrailTable& trails,
                                                const RoadBeds& beds) {
  return WithTrails(BuildRoadIndex(roads), trails, beds);
}

GroundState GroundStateAt(const WorldState& world, Vec2 point) {
  const DayWeights* day = nullptr;
  const std::shared_ptr<const RoadIndexImpl> base = BareOf(RoadIndexOf(world), &day);
  if (base == nullptr) {
    return GroundState::kDry;
  }
  return base->GroundAt(point, world.weather.road_beds);
}

float WalkRouteKm(const WorldState& world,
                  Vec2 from,
                  Vec2 to,
                  std::vector<std::pair<TrailEnd, TrailEnd>>* edges) {
  const DayWeights* day = nullptr;
  const std::shared_ptr<const RoadIndexImpl> base = BareOf(RoadIndexOf(world), &day);
  if (base == nullptr) {
    return RoadKm(world, TravelMode::kWalk, from, to);
  }
  if (day != nullptr) {
    return base->WalkRouteKmWith(from, to, day, edges);
  }
  // A bare index (no trail in effect this morning, a neutral ground): the lattice still gives
  // the walker his steps, so the first trail can begin; its km is the straight line's by the
  // normalisation. NOT world.trails: today's tread lands there during the day, and the day's
  // costs are frozen at the morning's index (static review of 23 Nov 2026).
  static const TrailTable kNoTrails;
  DayWeights neutral;
  neutral.trails = &kNoTrails;
  return base->WalkRouteKmWith(from, to, &neutral, edges);
}

RouteMeasure ReachMeasure(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to) {
  const std::shared_ptr<const RoadIndex> index = RoadIndexOf(world);
  if (const auto dated = std::dynamic_pointer_cast<const DayRoadIndex>(index)) {
    return dated->Base()->MeasureWith(mode, from, to, dated->DistanceDay());
  }
  return index->Measure(mode, from, to);
}

float ReachKm(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to) {
  return ReachMeasure(world, mode, from, to).effective_km;
}

std::uint64_t DayTableBuilds() {
  return RoadIndexImpl::g_day_table_builds.load(std::memory_order_relaxed);
}

std::shared_ptr<const RoadIndex> RoadIndexOf(const WorldState& world) {
  if (world.road_index != nullptr) {
    return world.road_index;
  }
  return BuildRoadIndex(world.roads);
}

float RoadKm(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to) {
  return RoadIndexOf(world)->EffectiveKm(mode, from, to);
}

RouteMeasure RoadMeasure(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to) {
  return RoadIndexOf(world)->Measure(mode, from, to);
}

float OffRoadWeightOf(const WorldState& world, TravelMode mode) {
  return RoadIndexOf(world)->OffRoadWeight(mode);
}

Vec2 DistrictExitPoint(const WorldState& world) {
  const std::shared_ptr<const RoadIndex> index = RoadIndexOf(world);
  bool found = false;
  Vec2 exit{};
  for (const RoadNode& node : index->Graph().nodes) {
    if (node.border && (!found || node.position.y > exit.y)) {
      exit = node.position;
      found = true;
    }
  }
  if (found) {
    return exit;
  }
  return world.units.rows.empty() ? Vec2{} : world.units.rows.front().position;
}

}  // namespace core
