/// @file
/// @brief The selection of a laid road's pieces for an upgrade or a
///        demolition (construction design §13, «Инструмент удаления: крупными
///        кусками»; roads design §9, the upgrade tools; §17, «Транспортная
///        связность»; delivery 7d): a drag along a road in, the pieces it
///        takes out, each in or out and why, and the estimate.
/// @threading PARALLEL_READONLY
/// A pure function of the site and the selection.
///
/// A PIECE IS JUNCTION TO JUNCTION (or to a road's end), as the derived
/// graph cuts the road (road_graph.h). The drag's two ends are projected
/// onto the road; every piece they overlap is taken, snapped to its joints —
/// a piece is cut mid-way only when the remnant on that side is at least
/// kMinRemnantMetres (STUB, look confirms it by frame), otherwise the
/// selection runs on to the joint. A branch comes off at the junction's own
/// axis: no stub is left.
///
/// WHAT A DEMOLITION MAY NOT TAKE (construction design §13): a road the map
/// marks not removable (roads.csv `removable` 0 — the trunk, its bridge, the
/// ways to the neighbours), and the only road to something (§17). The second
/// is asked of the network: the pieces are taken one by one in drag order,
/// and a piece is refused kOnlyRoad when taking it too would cut from the
/// district's network a unit that reached it (a road within road_access_m of
/// the unit's centre plus half the bed), a way out to a neighbour, or the
/// dead end of a map road — the place it leads to (a settlement inside the
/// map, a hayfield, the forest), STUB until the places are exported: the
/// core knows them by no name. The unit or the map road is named. The
/// district's network is the part holding the roads that may not be
/// removed; a world with none takes the part holding most of what is
/// reached. STUB, named: fields are not asked yet (§17 names them), nor
/// winter roads and fords' seasons.
///
/// WHAT AN UPGRADE MAY NOT TAKE (roads design §9, the chain): a piece
/// already of that surface or past it (kAlreadyThat), asphalt over dirt — the
/// chain goes through gravel — or any upgrade of a path, which no upgrade
/// makes a road (kNotThisStep), a target the epoch has not opened
/// (kClosedByEpoch), a dirt piece on the floodplain (kFloodplain, §11а), and
/// walks away from the village (kOutsideVillage). Work standing on a piece
/// (kUnderWork) comes with road work, 7e.

#ifndef CORE_COMMON_ROAD_PIECES_H_
#define CORE_COMMON_ROAD_PIECES_H_

#include <span>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/obstacle_raster.h"
#include "core_common/road_draft.h"
#include "core_common/road_state.h"
#include "core_common/road_trace.h"

namespace core {

/// @brief The shortest remnant a cut may leave on either side, metres. STUB
///        (construction design §13: «не короче 50 м (`STUB`)»).
inline constexpr float kMinRemnantMetres = 50.0F;

/// @brief A unit as connectivity asks it: its id and where it stands.
struct RoadAnchorUnit {
  UnitId unit;
  Vec2 position;
};

/// @brief The world as the selection reads it.
struct RoadPieceSite {
  const RoadTable* roads = nullptr;

  /// Every unit; the ones with no road in reach take no part.
  std::span<const RoadAnchorUnit> units;

  /// How near a road must come to a unit, metres from the bed's edge
  /// (world_params `road_access_m`, units rules §12).
  float road_access_m = 10.0F;

  /// For a dirt piece on the floodplain; may be null (then none is).
  const ObstacleRaster* raster = nullptr;

  /// The village's contours, for walks.
  std::span<const std::vector<Vec2>> village;

  /// By RoadSurface: open by the epoch, and the price of 100 m.
  std::array<RoadSurfaceCost, kRoadSurfaceSlots> costs{};

  /// The bed's and the path's half-widths, as the tracer takes them.
  float road_half_width_m = 4.0F;
  float path_half_width_m = 0.5F;
};

/// @brief The pieces `selection` takes for `operation` (see @file).
/// @return No piece when the road is not in the table or has no axis. The
///         estimate counts the pieces that are in: an upgrade the target's
///         100 m price times their length (STUB basis, road_cost_catalog.h);
///         a demolition nothing — dirt and a path are free, and a paved
///         road's demolition is road work (7e).
RoadPieces SelectRoadPiecesOn(const RoadPieceSite& site,
                              const RoadSelection& selection,
                              RoadOperation operation);

}  // namespace core

#endif  // CORE_COMMON_ROAD_PIECES_H_
