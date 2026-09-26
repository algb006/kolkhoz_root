/// @file
/// @brief The way from one place to another over the road network: how far
///        it is for a walker, a team, a cart or a log cart, and by which legs
///        (roads design §11-§13; transport §12's routing stands on it).
/// @threading PARALLEL_READONLY
/// A RoadIndex is immutable once built and is shared by the worlds that copy
/// it; every query is const and touches nothing else. Built in the
/// sequential slots that change the network (genesis, load, and the road
/// work to come), never inside a parallel phase.
///
/// WHAT A QUERY ANSWERS: an EFFECTIVE distance in kilometres — the length of
/// the way taken, each piece weighed by how much slower it is than the
/// traveller's own pace on a good road: a leg across open ground weighs more
/// than its length (roads design §11: «Скорость резко ниже»). Every place in
/// the core that measured a trip as a straight line times a pace keeps its
/// pace and takes this in place of the line (0.36.1). Weather and the state
/// of the road are delivery 3's; until then a road piece weighs its length.
///
/// WHO GOES WHERE (roads design §11, §12):
///   - kWalk: people — roads, paths and open ground alike.
///   - kTeam: a horse team to its field work, a mower to its meadow — the
///     field is its work, not off-road driving (§11 «Кому не разрешено»:
///     «Техника на полевых работах … это её работа»); roads and open ground.
///   - kCart: carting produce — ROADS ONLY. It leaves the road only for the
///     last piece to the place itself, the field's heap or the unit's gate,
///     at the off-road weight; no road near, and that piece is long.
///   - kLogCart: carting logs — the one cart allowed off the road (§11:
///     «с зерном нет, с лесом да»); roads and open ground.
///
/// ONE NETWORK, MANY STOPS (boss, core-boss-epoch1-6 [24]): the index keeps
/// the network's node-to-node distances whole, so a round through several
/// places — feed there, milk back — is a sum of these queries, not a second
/// router.

#ifndef CORE_COMMON_ROAD_ROUTE_H_
#define CORE_COMMON_ROAD_ROUTE_H_

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/road_graph.h"
#include "core_common/road_state.h"

namespace core {

struct WorldState;

/// @brief Who is travelling (see @file).
enum class TravelMode : std::uint8_t {
  kWalk = 0,
  kTeam,
  kCart,
  kLogCart,
  kTravelModeCount,
};

inline constexpr std::size_t kTravelModeCountValue =
    static_cast<std::size_t>(TravelMode::kTravelModeCount);

/// @brief How much a kilometre of open ground weighs against a kilometre of
///        good road, by mode (roads design §11: «Скорость резко ниже —
///        медленнее самой плохой грунтовки»). STUB, each with its reason:
///   - walk 1.2: across a meadow or a stubble field a man walks, but slower
///     than on a trodden way, and the village's paths exist because of it;
///   - team 1.5: a horse in harness over a field, not on a road;
///   - cart 2.5: a loaded cart where there is no road — the heap's last
///     metres and nothing more; «медленнее самой плохой грунтовки»;
///   - log cart 2.5: the same cart with logs, over the felling's ground.
struct RoadTravelRules {
  std::array<float, kTravelModeCountValue> off_road_weight = {1.2F, 1.5F, 2.5F, 2.5F};

  /// Find places through the precomputed near table (fast); false searches
  /// the grid ring by ring every time. The answers are the same by
  /// construction and the test proves it on a grid over the map; the switch
  /// exists for that proof, not for play.
  bool near_table = true;
};

/// @brief One piece of a way: on the network (a piece of one road between
///        two chainages) or across open ground (between two points).
struct RouteLeg {
  bool on_road = false;

  /// On the network: which road, and from where to where along its axis.
  RoadId road;
  float from_chainage_m = 0.0F;
  float to_chainage_m = 0.0F;

  /// Across open ground: the two points.
  Vec2 from;
  Vec2 to;

  /// The leg's own length, metres, and its effective kilometres.
  float length_m = 0.0F;
  float effective_km = 0.0F;
};

/// @brief A way, in order, with its total.
struct Route {
  std::vector<RouteLeg> legs;

  float effective_km = 0.0F;

  /// A cart found no road at all to go by: the way is the straight line at
  /// the off-road weight, and it is said so (roads design §17's question,
  /// delivery 6).
  bool cart_without_road = false;
};

/// @brief A way's two numbers, without its legs (RoadIndex::Measure).
struct RouteMeasure {
  /// What EffectiveKm answers for the same query.
  float effective_km = 0.0F;

  /// Metres of the way that lie OFF the network: the piece from the start
  /// to the road and the piece from the road to the end, or the whole
  /// straight line when the way is taken across open ground (or a cart
  /// found no road at all). Plain metres, not weighed. Always the sum of the
  /// two below.
  float off_road_m = 0.0F;

  /// The same, BY END (0.36.15): the piece at the start and the piece at the
  /// end. A way taken across open ground, or with no road at all, is the
  /// start's whole — it never joined the network to reach the end by it.
  /// Told apart because the produce cart's two ends are different questions
  /// (boss, boss-core-epoch1-resume [31]): driving a field from the load is
  /// what question 268 forbids; the store's gate off the road is a unit's
  /// road access (unit rules §12).
  float off_road_start_m = 0.0F;
  float off_road_end_m = 0.0F;
};

/// @brief Where a place meets a road piece a mode may use.
struct RoadAccess {
  RoadEdgeIndex edge = 0;

  /// Metres along the ROAD (not the edge) where the place meets it.
  float chainage_m = 0.0F;

  Vec2 point;

  /// From the place to `point`, metres.
  float distance_m = 0.0F;
};

/// @brief A place already found on the network for one mode: the road
///        pieces it may join the network by, nearest first (at most three).
///
/// FINDING IS THE COSTLY HALF OF A QUERY and depends on the place alone, so a
/// caller whose places repeat — the accountant's homes and jobs, every
/// morning — finds each once and asks between the found places; that half
/// is a table's reading (0.36.1: ~80 us a query in the debug build against
/// ~1 us between found places).
struct NetworkPlace {
  Vec2 position;

  TravelMode mode = TravelMode::kWalk;

  std::array<RoadAccess, 3> accesses{};

  std::uint8_t access_count = 0;
};

/// @brief The network made queryable: the graph, its node-to-node effective
///        distances by mode, and a grid for finding the nearest road piece.
class RoadIndex {
 public:
  /// @brief The effective kilometres from `from` to `to` for `mode`.
  virtual float EffectiveKm(TravelMode mode, Vec2 from, Vec2 to) const = 0;

  /// @brief Finds `position` on the network for `mode` (see NetworkPlace).
  virtual NetworkPlace Locate(TravelMode mode, Vec2 position) const = 0;

  /// @brief The effective kilometres between two places found for ONE mode.
  /// @pre from.mode == to.mode; otherwise the answer is the from's mode's.
  virtual float EffectiveKm(const NetworkPlace& from, const NetworkPlace& to) const = 0;

  /// @brief The same way as EffectiveKm, with how much of it is off the
  ///        road (0.36.9, the produce cart's instrument) — one choice of way,
  ///        arithmetic only; Way below builds the legs, this does not.
  virtual RouteMeasure Measure(TravelMode mode, Vec2 from, Vec2 to) const = 0;

  /// @brief The same way, leg by leg (for the trodden paths of delivery 5,
  ///        and for whoever must show a route).
  virtual Route Way(TravelMode mode, Vec2 from, Vec2 to) const = 0;

  /// @brief The graph the index was built on.
  virtual const RoadGraph& Graph() const = 0;

  /// @brief What a kilometre of open ground weighs for `mode` in this index:
  ///        its rules' off_road_weight, or 1 in an index with no network
  ///        (0.36.20; the carry to a field's heap is priced by it, across the
  ///        field, where no road runs).
  virtual float OffRoadWeight(TravelMode mode) const = 0;

  virtual ~RoadIndex() = default;
};

/// @brief Builds the index of `roads` under `rules`. An EMPTY table gives an
///        index with no network: every mode goes the straight line at its own
///        pace — weight 1, the model before roads, since there is no road for
///        open ground to be slower than — and a cart says it had no road.
///        Only a hand-built world has no network; the game's has the map's.
std::shared_ptr<const RoadIndex> BuildRoadIndex(const RoadTable& roads,
                                                const RoadTravelRules& rules = {});

/// @brief `world.road_index`, or — for a world nobody indexed (a hand-built
///        test world) — an index built on the spot from `world.roads` and
///        the default rules. Slower, never wrong.
std::shared_ptr<const RoadIndex> RoadIndexOf(const WorldState& world);

/// @brief EffectiveKm through RoadIndexOf: the one call the travelling
///        places make.
float RoadKm(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to);

/// @brief Measure through RoadIndexOf: RoadKm's way with its metres off the
///        road.
RouteMeasure RoadMeasure(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to);

/// @brief OffRoadWeight through RoadIndexOf.
float OffRoadWeightOf(const WorldState& world, TravelMode mode);

/// @brief Where the village's way to the district leaves the map: the
///        network's northernmost border end (terrain design §1а, «Райцентр на
///        севере»; the player arrives on a cart from the district's side). A
///        world with no border end (hand-built): the first unit's ground, or
///        the origin with no unit. The timber lots' carters go there
///        (decision 279, 0.36.17), and the carting is measured from it.
Vec2 DistrictExitPoint(const WorldState& world);

}  // namespace core

#endif  // CORE_COMMON_ROAD_ROUTE_H_
