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

#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/geometry.h"
#include "core_common/order_state.h"
#include "core_common/road_route.h"
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

/// @brief THE DEADLINE OF A FELLING MARK NO HAND CAN BE SENT TO (0.37.208; the
///        human's rule of 2 October 2026: every wait has a deadline). Counts,
///        for every stand marked with work left, the dawns in a row its
///        felling would not be offered (home_reach.h, FellingCanBeMannedToday
///        — the offering's own predicate) in TimberStandRow::unreached_days;
///        a dawn it would be offered zeroes the count. At the table's
///        timber_mark_release_days the mark is released: the share the crew
///        felled leaves the stock and lies on the stand as logs, the rest is
///        stock again, and kFellingMarkReleased says it with the stand and
///        the litres left unfelled. 0 days in the table releases nothing.
/// @note Called ONCE A DAY, at the dawn tick, in production's sequential
///       slot. Side effects: the stand rows and one event a release. A mark
///       nobody is FREE for is not this function's: it counts reach, not hands.
void ReleaseUnreachableMarks(const ProductionConfig& config, WorldState& current);

/// @brief The year's turn for the old forest: every old-forest stand gains
///        the year's fallen trunks, and holds no more than
///        timber_fallen_vanish_years of them — a trunk lies that long and is
///        gone (timber design §8a).
void GrowOldForest(const ProductionConfig& config, WorldState& current);

/// @brief Appends kFellingUnreachable for every stand marked for felling with
///        work left whose ride from the nearest lived-in house is past the
///        accountant's road rule: longer than travel_limit_hours, or leaving
///        less than min_usable_hours of the daylight after the ride there and
///        back (the same test as kSiteUnreachable) — and kPlantingUnreachable,
///        the same test at walking speed, for every planting zone not yet
///        planted. SINCE 0.37.208 BOTH ALSO ASK WHETHER A HAND CAN BE SENT
///        (home_reach.h, HandReachToday): a felling is named as well when its
///        fellers can neither walk nor be given a ride, and a planting zone
///        past the walk is NOT named while a ride reaches it and the kolkhoz
///        has a horse. Each stand at most once, in row order. A pure read.
/// @param alarms Appended to; never cleared.
void CollectTimberAlarms(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_TIMBER_FELLING_H_
