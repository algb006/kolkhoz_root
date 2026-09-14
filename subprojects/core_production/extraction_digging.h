/// @file
/// @brief Digging clay, stone and sand: the chairman's mark, the dug mass laid
/// on the site as a load, and the exhausted site announced (construction
/// design §3; boss, parcel 270).
/// @threading SINGLE_THREADED
/// Every entry point runs from the production sub-step of the decisions slot
/// (phase 3) on the sim thread: the order when it is read, the digging every
/// tick after labor. They change site rows and emit events, so they can only
/// live in a sequential slot.
///
/// The carting of the load is not here: it is the settlement a field's and a
/// stand's loads go through (field_haul.h, SettleSiteHauling).

#ifndef CORE_PRODUCTION_EXTRACTION_DIGGING_H_
#define CORE_PRODUCTION_EXTRACTION_DIGGING_H_

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Reads a kMarkExtraction order: marks its mass of the site and opens
///        the digging seam at the marked tonnes × the material's labour per
///        tonne.
/// @return kNoSuchSubject for a site that is not there; kConflictsWithActive
///         while the site still has a mark the crew has not finished;
///         kRuleForbids for a mass that is not positive or exceeds the site's
///         unmarked stock — so an exhausted site is never marked again. kNone
///         when marked.
OrderRefusal MarkExtraction(const ProductionConfig& config,
                            WorldState& current,
                            const OrderRow& order);

/// @brief Lays down every mark whose crew has finished: the marked mass leaves
///        the stock and lies on the site as a load. A site whose stock reaches
///        nothing is announced exhausted, once (EventKind::kExtractionSiteExhausted).
/// @note Called every tick, after labor has drained the seam.
void DigFinishedSites(WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_EXTRACTION_DIGGING_H_
