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

  /// @brief The same way, leg by leg (for the trodden paths of delivery 5,
  ///        and for whoever must show a route).
  virtual Route Way(TravelMode mode, Vec2 from, Vec2 to) const = 0;

  /// @brief The graph the index was built on.
  virtual const RoadGraph& Graph() const = 0;

  virtual ~RoadIndex() = default;
};

/// @brief Builds the index of `roads` under `rules`. An empty table gives an
///        index with no network: every mode goes the straight line at its
///        off-road weight, and a cart says it had no road.
std::shared_ptr<const RoadIndex> BuildRoadIndex(const RoadTable& roads,
                                                const RoadTravelRules& rules = {});

/// @brief `world.road_index`, or — for a world nobody indexed (a hand-built
///        test world) — an index built on the spot from `world.roads` and
///        the default rules. Slower, never wrong.
std::shared_ptr<const RoadIndex> RoadIndexOf(const WorldState& world);

/// @brief EffectiveKm through RoadIndexOf: the one call the travelling
///        places make.
float RoadKm(const WorldState& world, TravelMode mode, Vec2 from, Vec2 to);

}  // namespace core

#endif  // CORE_COMMON_ROAD_ROUTE_H_
