/// @file
/// @brief A piece of road under work: gravel laid on it, or gravel taken up
///        (roads design §2, §9; construction design §13; delivery 7e).
/// @threading SINGLE_THREADED
/// Plain data. Opened by core_construction when an order is consumed, its
/// labour drained by core_labor's working hour (the seam, WorkSeamOf), and
/// closed by core_construction the day the labour is done — in the
/// sequential decisions slot (phase 3).
///
/// WHY A TABLE OF ITS OWN. A road is not a unit: it has no plot, no level
/// and no site phase, and a piece of one is only a stretch along its axis.
/// The work on it has what a site has — materials, labour, a crew — and
/// lives here until the piece takes its new surface or leaves the network.
///
/// THE MATERIALS ARE TAKEN AT THE ORDER, whole: the order is refused unless
/// the village holds them in full (kStartBuild's rule), and they come off
/// the standing stores then and there (core_construction/site_supply.h,
/// TakeFromStores) — no cart to the road, which is transport §12's
/// logistics, and no phase of waiting for them. So a work stands only for
/// its labour.
///
/// A PIECE UNDER WORK IS NOT SELECTED AGAIN (road_pieces.h, kUnderWork): the
/// work names it by metres along its road's axis, and a second work or a cut
/// on it would move them. A cut of ANOTHER piece of the same road — a work
/// done, a dirt piece demolished — moves the other works of that road to the
/// remnant they then lie on (SettleRoadWorks, and the demolition with it).

#ifndef CORE_COMMON_ROAD_WORK_STATE_H_
#define CORE_COMMON_ROAD_WORK_STATE_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/road_state.h"
#include "core_common/state_table.h"

namespace core {

/// @brief What is done to the piece.
enum class RoadWorkKind : std::uint8_t {
  /// A surface laid on it: gravel in Epoch I, its `road` level's materials
  /// and labour per 100 m (boss-core-epoch1-resume [81]). At the end the
  /// piece is cut out of its road and stands as a row of its own at
  /// `surface`, its bed's wear kept.
  kPave = 0,

  /// A paved surface taken up: labour alone, a share of the laying's (STUB
  /// 1/3, boss [81]: gravel trodden into the bed is not gathered back). At
  /// the end the piece leaves the network as a dirt road's does
  /// (road_cut.h), the land keeping its wear.
  kTakeUp,

  kRoadWorkKindCount,  ///< NOT A KIND: the count, for mirrors and codecs.
};

/// @brief One piece of road under work.
struct RoadWorkRow {
  /// The road the piece lies on.
  RoadId road;

  /// The piece, in metres along the road's axis from its first point.
  float from_m = 0.0F;
  float to_m = 0.0F;

  RoadWorkKind kind = RoadWorkKind::kPave;

  /// kPave: the surface the piece takes (kGravel in Epoch I). kTakeUp: the
  /// piece's surface as the work was opened.
  RoadSurface surface = RoadSurface::kGravel;

  /// Man-days of labour still to put in (norm-days, as a site's).
  float labor_days_remaining = 0.0F;

  /// The piece's middle, on the axis: where the crew walks to and where the
  /// accountant measures the road from.
  Vec2 place;

  /// The most hands at once (unit_levels.csv `road` `max_crew` of the
  /// surface laid or taken up).
  std::uint8_t max_crew = 0;

  /// 0/1: whether the work goes on in winter (the surface's `road` level
  /// `winter_works`; construction design §8). 0 — nobody is sent in the
  /// core's winter season, and the labour done stays.
  std::uint8_t winter_works = 0;
};

using RoadWorkTable = StateTable<RoadWorkId, RoadWorkRow>;

}  // namespace core

#endif  // CORE_COMMON_ROAD_WORK_STATE_H_
