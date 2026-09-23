/// @file
/// @brief Planting a forest by zone: the chairman's order, the planting
/// crew's work, and the planted stand growing to logs (timber design §2 and
/// §8a; map design §7; boss, boss-core-epoch1-3 seq 10 and 15; econ,
/// econ/manual/proposals/timber-second-source.md).
/// @threading SINGLE_THREADED
/// Every entry point runs from the production sub-step of the decisions slot
/// (phase 3) on the sim thread: the order when it is read, the planting every
/// tick after labor, the growing at the day's turn. They change stand rows,
/// so they can only live in a sequential slot.
///
/// CONTRACT, 0.34.34 — NO BODIES. The implementation is a task of its own
/// (core/CLAUDE.md §12a). What the implementation adds to the SEAM, named
/// here so boss can enter the words in the same move as the delivery:
///
///   OrderKind::kPlantForest (appended)      seam key `plant_forest`
///   TimberStandKind::kPlanted (appended)    seam key `planted`
///   WorkKind::kPlanting (appended)          seam key `planting`
///   EventKind::kForestPlanted (appended)    seam key `forest_planted` —
///     the crew finished; stand = the planting, amount = hectares × 100
///   EventKind::kPlantingMatured (appended)  seam key `planting_matured` —
///     the planting grew to logs; stand = the planting, amount = its m3
///
///   OrderRow::area_ha (float) — kPlantForest: the zone's hectares. A field
///     of its own and not `volume_m3` borrowed: a seam field read under two
///     meanings is how a layer sends one where the other was meant.
///   OrderRow::species (TreeSpeciesId) — kPlantForest: what is planted.
///   TimberStandRow::species (TreeSpeciesId), ::planted_day (SimDay, the day
///     the crew finished; kNeverPlanted while planting), ::matures_day
///     (SimDay). `area_ha` is the stand table's already. Save format +1,
///     sizes predicted before the fields are added.
///
/// THE DATA (design base; boss adds the columns, core reads them):
///   tree_species.plantable            (exists) — 0 refuses the order
///   tree_species.plant_years_to_logs  STUB 5 game years (design: 3-6)
///   tree_species.plant_m3_per_ha      STUB 30, a grove's stock
///   tree_species.plant_log_share      the EXPORT's figure, from log_yield
///     and the timber_log_share_* knobs (pine full 0.6, birch part 0.25) —
///     the core does not read export knobs (timber_catalog.cpp)
///   world_params.timber_planting_days_per_ha  STUB, game man-days a
///     hectare: econ's one real person-day, ÷ 7
///   world_params.timber_planting_max_ha       STUB 5, one order's zone
///
/// WHAT IS DECIDED (boss seq 15, core's reading accepted): a planting is a
/// STAND ROW, not a field; it lies OUTSIDE the fields; the ground of a grove
/// or belt felled to nothing is the first place for it (the order may name
/// that stand, and the row is reused); pine or birch, the player's choice;
/// when grown it is felled WHOLE like a grove, and the ground is free again.

#ifndef CORE_PRODUCTION_TIMBER_PLANTING_H_
#define CORE_PRODUCTION_TIMBER_PLANTING_H_

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Reads a kPlantForest order: opens a planting of `order.area_ha`
///        hectares of `order.species`, and its planting seam at area ×
///        timber_planting_days_per_ha.
///
/// Two forms, one order. With `order.stand` naming a grove or shelterbelt
/// felled to nothing (stock_m3 == 0, nothing marked, no load lying), THAT row
/// becomes the planting — its ground, its loading point, at most its own
/// area. With no stand, a new planting row is made at `order.position`.
///
/// @return kNoSuchSubject for a species that is not in the roster or a stand
///         that is not there; kNotEligible for a species with plantable = 0,
///         or a named stand that is not a grove or belt felled to nothing;
///         kRuleForbids for an area not positive or above
///         timber_planting_max_ha (or the named stand's own area), or a
///         position off the map; kWrongLand for a new zone whose contour
///         (a circle of the zone's area) touches a field; kTooClose for one
///         that touches a unit's plot or another stand's contour.
///         kNone when the planting is opened.
/// @note Side effects: appends or rewrites one TimberStandRow (kind
///       kPlanted, stock 0, planted_day kNeverPlanted) and writes its
///       work_days_remaining.
OrderRefusal OrderPlantForest(const ProductionConfig& config,
                              WorldState& current,
                              const OrderRow& order);

/// @brief Finishes every planting whose crew has drained its seam: sets
///        planted_day to today and matures_day to today + the species'
///        plant_years_to_logs, and emits kForestPlanted.
/// @note Called every tick, after labor has drained the seam — the same
///       place FellFinishedStands stands (production_system.cpp).
void FinishPlantings(const ProductionConfig& config, WorldState& current);

/// @brief Grows every planting that has reached its matures_day: its stock
///        becomes area × plant_m3_per_ha with the species' plant_log_share,
///        so from that day it is felled like a grove (MarkFelling), and
///        emits kPlantingMatured. A planting felled to nothing is a grove
///        felled to nothing: its ground may be planted again.
/// @note Called once a day, in the daily block. Before its matures_day a
///       planting holds nothing to fell (stock 0): MarkFelling refuses it
///       with kRuleForbids by its own rule.
void GrowPlantings(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_TIMBER_PLANTING_H_
