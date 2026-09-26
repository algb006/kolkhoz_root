/// @file
/// @brief Laying a road or path the player drew (kLayRoad; roads design §9,
///        «Грунтовка ничего не стоит»; delivery 7c), taking pieces out (7d),
///        and road work — gravel laid on a road and taken up (7e).
/// @threading SINGLE_THREADED
/// Called from ConstructionSystem::ConsumeOrders and its day, the sequential
/// decisions slot of phase 3; writes WorldState::roads, road_index,
/// land_strips and road_works, and appends events.
///
/// THE EVENT COMES AFTER THE WRITE (ue's condition, road_draft.h): an event
/// about a road is appended once the rows and the index are written, in the
/// same tick, so Roads() answers it by the time the layer reads it.
///
/// THE LAND KEEPS ITS WEAR (construction design §13; 7d): a stretch of a new
/// road laid along a strip a road was taken off (within half the bed of the
/// strip's axis) begins with the strip's wear, not at nought — «стирали не
/// яму, а имя».
///
/// ROAD WORK (7e; road_work_state.h): gravel laid on a road, a gravel road
/// laid, and gravel taken up are works with materials and labour. One work a
/// piece the selection took, priced by its surface's `road` level per 100 m
/// (the numbers the selection's estimate shows); its materials taken off the
/// standing stores at the order, whole (site_supply.h), kRoadWorkStarted
/// raised then; the accountant sends the crew (core_labor);
/// and the day it is done the piece takes its surface or leaves the network
/// (SettleRoadWorks). Asphalt is Epoch II: the selection and the tracer
/// refuse it.
///
/// WHAT IT DOES NOT DO YET, and who will:
///   * a draft that traced clean in the preview and not on the step's world
///     (a unit raised, a road laid in between) is refused kRuleForbids, and
///     PreviewRoad on the same points says which block; the refusal itself
///     carries no RoadDraftRefusal;
///   * the district paving its own road (kDistrictRoad's STUB): no schedule.

#ifndef CORE_CONSTRUCTION_ROAD_LAYING_H_
#define CORE_CONSTRUCTION_ROAD_LAYING_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core_catalog/road_cost_catalog.h"
#include "core_common/ids.h"
#include "core_common/order_state.h"
#include "core_construction/construction_system.h"

namespace core {

class ITableSet;
struct WorldState;

/// @brief What road work needs to know of the tables, read once at assembly.
///        The PRICE of a piece is the selection's own estimate (road_pieces.h:
///        the target's `road` level per 100 m of bed — roads design,
///        «Полотно целиком»; boss [81]); this holds what the estimate does
///        not.
struct RoadWorkCatalog {
  /// By RoadSurface: the surface's `road` level — its brigade (`max_crew`)
  /// and, for a take-up, the laying's labour the share is taken of.
  RoadSurfaceLevels levels{};

  /// A take-up's labour as a share of the laying's (ConstructionConfig).
  float take_up_labor_share = 0.0F;

  /// Whether the tables were read: false — every road work refused
  /// kNoConsumer (a set with no road levels: unit tests of this module).
  bool read = false;
};

/// @brief Reads the catalogue off the tables (road_cost_catalog.h). An error
///        leaves it unread and says why.
RoadWorkCatalog ReadRoadWorkCatalog(const ITableSet& tables,
                                    float take_up_labor_share,
                                    std::string& error);

/// @brief Lays the road `order` draws, when it traces clean.
/// @param trace The world's tracer; empty in a world assembled without one
///        (unit tests of this module), and then every laying is refused
///        kNoConsumer.
/// @param catalog Road work's (7e): a GRAVEL road is laid as a dirt road at
///        once and a work of gravel opened over its whole length, priced as
///        the draft's estimate (the clearing a dirt road takes, and the
///        gravel's materials and labour; boss [81] p. 3).
/// @param order_id The order's id, for the event.
/// @param order Its `road` is set to the road laid, so the order's own event
///        names it (world.cpp's sweep copies it).
/// @return kNone when laid; kNoConsumer (no tracer; gravel with the catalogue
///         unread); kRuleForbids (the trace was refused); kMaterialsShort
///         (gravel, and the village has not its materials in full at the
///         order — nothing is laid).
OrderRefusal LayRoad(const RoadTracer& trace,
                     const RoadWorkCatalog& catalog,
                     WorldState& current,
                     OrderId order_id,
                     OrderRow& order);

/// @brief Takes out the pieces `order` drags along (kDemolishRoad; 7d),
///        selected again on the step's world by the preview's own selection:
///        the road cut (core_common/road_cut.h) — gone whole, shortened, or
///        split into two roads, the second a new row — the land strips
///        appended with the wear the pieces had, the index rebuilt, and
///        kRoadDemolished raised after the write for the road (its id even
///        when the whole of it went). A PAVED road's pieces are taken up as
///        road work instead (7e): labour alone, a share of the laying's, no
///        material back; the cut comes the day the work is done.
/// @param select The world's selection; empty — refused kNoConsumer.
/// @return kNone when taken (or, paved, its work opened); kRuleForbids when
///         no piece is in (a kept road, the only road to something, a piece
///         under work, or nothing under the drag); kNoConsumer for a paved
///         road with the catalogue unread.
OrderRefusal DemolishRoad(const RoadSelector& select,
                          const RoadWorkCatalog& catalog,
                          WorldState& current,
                          OrderId order_id,
                          OrderRow& order);

/// @brief Opens road work to lay `order.road_surface` on the pieces `order`
///        drags along (kUpgradeRoad; delivery 7e), selected again on the
///        step's world by the preview's own selection: one RoadWorkRow a piece
///        that is in, its materials owed and its labour its share of the
///        selection's estimate.
/// @return kNone when opened; kDistrictRoad when the selection leaves the
///         pieces out as the district's road (RoadPieceRefusal::kDistrictRoad);
///         kGateClosed when the target is closed in this epoch (asphalt:
///         Epoch II); kRuleForbids when no piece is in (already that surface,
///         the floodplain, a piece under work, nothing under the drag);
///         kMaterialsShort when the village has not the pieces' materials in
///         full at the order (kStartBuild's rule), and no work is opened;
///         kNoConsumer with no selection or the catalogue unread.
OrderRefusal UpgradeRoad(const RoadSelector& select,
                         const RoadWorkCatalog& catalog,
                         WorldState& current,
                         OrderId order_id,
                         OrderRow& order);

/// @brief The road works of the day, at its first hour: each work whose
///        labour is done is applied — a
///        paved piece cut out of its road and written as a row of its own at
///        its new surface, its bed's wear kept; a taken-up piece gone as a
///        dirt road's demolition, the land keeping its wear (kRoadDemolished)
///        — the other works of that road moved to the remnant they now lie
///        on, the work removed, the index rebuilt, and kRoadWorkFinished
///        raised after the write.
void SettleRoadWorks(WorldState& current);

}  // namespace core

#endif  // CORE_CONSTRUCTION_ROAD_LAYING_H_
