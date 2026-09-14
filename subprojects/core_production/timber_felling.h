/// @file
/// @brief Felling: the chairman's mark, the felled timber laid down as logs,
/// and the old forest's yearly trunks (timber design §8a).
/// @threading SINGLE_THREADED
/// Every entry point runs from the production sub-step of the decisions slot
/// (phase 3) on the sim thread: the order when it is read, the felling every
/// tick after labor, the old forest at the year's turn. They change stand
/// rows, so they can only live in a sequential slot.
///
/// The carting of the logs is not here: it is the same settlement a field's
/// load goes through (field_haul.h, SettleStandHauling).

#ifndef CORE_PRODUCTION_TIMBER_FELLING_H_
#define CORE_PRODUCTION_TIMBER_FELLING_H_

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Reads a kMarkFelling order: marks its volume of the stand and opens
///        the felling seam at volume × timber_felling_days_per_m3.
/// @return kNoSuchSubject for a stand that is not there; kConflictsWithActive
///         while this stand still has timber marked (fellings on other stands
///         go on at once, the human's word of 2026-09-14); kRuleForbids for a
///         volume that is not positive or
///         exceeds the stand's unmarked stock. kNone when marked.
OrderRefusal MarkFelling(const ProductionConfig& config,
                         WorldState& current,
                         const OrderRow& order);

/// @brief Fells every stand whose crew has finished: the marked volume leaves
///        the stock and its logs are laid on the stand as a load.
/// @note Called every tick, after labor has drained the seam, so a felling
///       finished by noon is lying on the ground by noon.
void FellFinishedStands(const ProductionConfig& config, WorldState& current);

/// @brief The year's turn for the old forest: every old-forest stand gains
///        the year's fallen trunks, and holds no more than
///        timber_fallen_vanish_years of them — a trunk lies that long and is
///        gone (timber design §8a).
void GrowOldForest(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_TIMBER_FELLING_H_
