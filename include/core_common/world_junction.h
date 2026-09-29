/// @file
/// @brief Where the map's roads meet the world beyond it: the junctions the
///        layer draws its arrows at (layers design §15а; register 300).
/// @threading SINGLE_THREADED
/// Plain data, read between steps. The rows come from the table
/// `world_junctions.csv` (boss's export of db/design.db) matched to the
/// border vertices of `roads.csv`; nothing here is state of the world.
///
/// WHY A VIEW AND NOT A MARK ON THE ROAD. The road catalog reduces every
/// border vertex to RoadMark::kBorder — the core's roads need what a vertex
/// IS, not where it faces — and the one link to the outside it kept was the
/// northernmost border, the district's (DistrictExitPoint). The arrows need
/// the four by name: which road, which border, and where it leads.
#ifndef CORE_COMMON_WORLD_JUNCTION_H_
#define CORE_COMMON_WORLD_JUNCTION_H_

#include <cstdint>
#include <string>

#include "core_common/geometry.h"

namespace core {

/// @brief The side of the map a junction's border vertex lies on. Appended,
/// never renumbered; seam words `border` of world_junctions.csv.
enum class BorderSide : std::uint8_t {
  kNorth = 0,  ///< y = the map's side.
  kSouth,      ///< y = 0.
  kEast,       ///< x = the map's side.
  kWest,       ///< x = 0.

  /// NOT A SIDE: the count, for a consumer's mirror.
  kBorderSideCount,
};

/// @brief Where a junction leads (world_junctions.csv `leads_to`). Appended,
/// never renumbered.
enum class JunctionLeadsTo : std::uint8_t {
  kDistrict = 0,  ///< The district centre, beyond the border.
  kNeighbor,      ///< A neighbouring kolkhoz (neighbors design).

  /// NOT A PLACE: the count, for a consumer's mirror.
  kJunctionLeadsToCount,
};

/// @brief A neighbour's role (world_junctions.csv `neighbor_role`;
/// neighbors design §2). kNone for the district. Appended, never renumbered.
enum class NeighborRole : std::uint8_t {
  kNone = 0,  ///< The district's row: no neighbour.
  kStrong,    ///< The strong neighbour (neighbors design §2).
  kPoor,      ///< The poor one.
  kMiddling,  ///< The middling one.

  /// NOT A ROLE: the count, for a consumer's mirror.
  kNeighborRoleCount,
};

/// @brief One junction as the layer draws it. The arrow's label is not
/// here: the district is «Районный центр», a neighbour's kolkhoz name is
/// drawn at the game's start (the table says so) — the layer's words.
struct JunctionView {
  /// The table's key: `district`, `neighbor_west`… — the seam's word.
  std::string key;

  /// The map road it lies on (roads.csv `road`), as RoadView::map_road.
  std::string road_key;

  /// The side of the map the arrow stands on (`border`).
  BorderSide border = BorderSide::kNorth;

  /// The border vertex: map metres from the south-west corner.
  Vec2 position;

  /// Where the road leads beyond the map (`leads_to`).
  JunctionLeadsTo leads_to = JunctionLeadsTo::kDistrict;

  /// The neighbour's role (`neighbor_role`); kNone exactly when leads_to is
  /// the district — the reader refuses anything else.
  NeighborRole neighbor_role = NeighborRole::kNone;
};

}  // namespace core

#endif  // CORE_COMMON_WORLD_JUNCTION_H_
