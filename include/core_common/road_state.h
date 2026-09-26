/// @file
/// @brief The road network as the core's state: one row a road or path, its
///        axis, and its condition along its length (roads design §1-§4, §12,
///        §13).
/// @threading SINGLE_THREADED
/// Written at genesis and, later, by the road work of the decisions slot;
/// read by anyone between steps. Plain data.
///
/// THE POLYLINE LIVES HERE, THE GRAPH IS DERIVED (roads design §13, decision
/// of 17 September 2026: «Полилиния живёт в ядре … Граф ядро ВЫВОДИТ, а не
/// хранит»). core_common/road_graph.h builds nodes and edges out of these
/// rows; nothing of the graph is stored, so it cannot drift from the axes.
///
/// THE STATE LIVES ON STRETCHES, NOT ON GRAPH EDGES. An edge runs from one
/// junction to the next, and a junction appears the day somebody lays a road
/// into the middle of another — so state kept per edge would have to be cut
/// and re-keyed whenever the network grows. A stretch is a fixed length of
/// ONE road's axis counted from its start (kRoadStretchMetres); it never
/// moves, and an edge reads the stretches it covers.
///
/// A MAP ROAD'S AXIS IS NOT SAVED (roads design §2, «Извилистость в
/// сохранении»: the start roads live in the map's geometry, one for every
/// campaign). The save keeps its key and its condition, and the loader puts
/// the axis back from tables/roads.csv. A road the player lays or
/// straightens keeps its axis in the save, as it was confirmed.

#ifndef CORE_COMMON_ROAD_STATE_H_
#define CORE_COMMON_ROAD_STATE_H_

#include <cstdint>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// @brief Metres of axis one stretch covers. Wear, traffic and the rest of a
///        road's condition are kept this finely along it. STUB: fine enough
///        that a village street (≈600 m) is two dozen of them and one worn
///        stretch in front of the office can differ from the rest; coarse
///        enough that the start network is a few thousand, not ten thousand.
inline constexpr float kRoadStretchMetres = 25.0F;

/// @brief The most control points the player's road draft takes: the tool's
///        modes are two, three and four points (the human's word of 25
///        September 2026; road_draft.h). Here rather than there because the
///        order row carries the points and need not see the whole draft.
inline constexpr std::uint8_t kRoadDraftMaxPoints = 4;

/// @brief What the line is for (roads design §12: «Дороги — для транспорта,
///        тропинки — для людей»).
enum class RoadKind : std::uint8_t {
  kRoad = 0,  ///< Carts, and later machines; people walk its verges.
  kPath,      ///< People only; no cart passes (unit rules §12).
  kRoadKindCount,
};

/// @brief What a road is surfaced with (roads design §2): a road's LEVEL is
///        its material, not its size. A path has none.
enum class RoadSurface : std::uint8_t {
  kNone = 0,      ///< A path: trodden ground, no bed, no ruts.
  kDirt,          ///< Грунтовка: the start and the whole of Epoch I.
  kGravel,        ///< Гравийка: Epoch I, by upgrade.
  kAsphalt,       ///< Асфальт: Epoch II.
  kAsphaltWalks,  ///< Асфальт с тротуарами: Epoch II, village only.
  kRoadSurfaceCount,
};

/// @brief Where a road came from — which decides where its axis lives.
enum class RoadOrigin : std::uint8_t {
  kMap = 0,  ///< On tables/roads.csv; the axis comes from there.
  kPlayer,   ///< Laid or straightened in play; the axis is in the save.
  kRoadOriginCount,
};

/// @brief The start's word for how often a road is driven (roads design §4,
///        «Вторая ось — не износ, а ЕЗДЯТ ЛИ»; start_layout.csv `traffic`).
///        The seed of the overgrowth axis until the core counts its own
///        traffic.
enum class RoadTrafficWord : std::uint8_t {
  kRegular = 0,  ///< Ездят регулярно.
  kRare,         ///< Ездят редко.
  kAlmostNone,   ///< Почти не ездят.
  kNone,         ///< Не ездят.
  kRoadTrafficWordCount,
};

/// @brief What stands at one vertex of an axis beside its position.
enum class RoadMark : std::uint8_t {
  kNone = 0,
  kJunction,  ///< A named meeting of roads on the map (junction*).
  kBorder,    ///< Where the road leaves the map (border_*): the way out.
  kBridge,    ///< The bridge deck starts or ends here (bridge*, arch).
  kFord,      ///< The ford: passable always (STUB, boss [23] item 7).
  kOther,     ///< A named place on the road the core has no rule for.
  kRoadMarkCount,
};

/// @brief One vertex of a road's axis.
struct RoadPoint {
  Vec2 position;

  RoadMark mark = RoadMark::kNone;
};

/// @brief The condition of one stretch of a road (kRoadStretchMetres of its
///        axis, counted from its first vertex; the last stretch is shorter).
struct RoadStretch {
  /// 0..100, unit rules §15's scale: 0 a fresh bed, 100 the worst. Seeded
  /// from start_layout.csv `start_wear_pct`; a path keeps 0 (roads design
  /// §4: «Тропы износа не имеют»).
  float wear_pct = 0.0F;
};

/// @brief Typed index into the map's roads (tables/roads.csv, in the order
///        their keys first appear): what a saved map road is remapped by.
struct MapRoadIdTag {};

using MapRoadId = DefId<MapRoadIdTag>;

/// @brief One road or path of the network.
struct RoadRow {
  RoadKind kind = RoadKind::kRoad;

  RoadSurface surface = RoadSurface::kDirt;

  RoadOrigin origin = RoadOrigin::kMap;

  /// For a map road, which one; invalid for a player road.
  MapRoadId map_road;

  /// 0: the player may not take it away — the trunk road, its bridge and the
  /// ways to the neighbours (construction design §13; roads.csv `removable`).
  std::uint8_t removable = 1;

  /// The start's word (RoadTrafficWord); kept until traffic is counted.
  RoadTrafficWord traffic_word = RoadTrafficWord::kRegular;

  /// The axis, in order. For a map road it is put back from the table on
  /// load and never written to the save.
  std::vector<RoadPoint> axis;

  /// The axis cut into kRoadStretchMetres pieces, in order from the start.
  std::vector<RoadStretch> stretches;
};

using RoadTable = StateTable<RoadId, RoadRow>;

}  // namespace core

#endif  // CORE_COMMON_ROAD_STATE_H_
