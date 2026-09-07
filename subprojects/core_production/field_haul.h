/// @file
/// @brief Carrying the harvest off a field, and the going-bad of everything
/// that lies in a store.
/// @threading SINGLE_THREADED
/// Both entry points run once a day, at the day's LAST tick, from the
/// production sub-step of the decisions slot (phase 3) on the sim thread.
/// They change stored masses and write the year's ledger, so they can only
/// live in a sequential slot (buffer-law rules 5 and 6).
///
/// Model: manual/75-logistics.md; transport design §9 (how goods move
/// between stores) and §10 (shelf life). Split out of production_system.cpp
/// when that file crossed the thousand-line limit: the day of a load and the
/// day of a crop are different days, and only one of them is about growing.
#ifndef CORE_PRODUCTION_FIELD_HAUL_H_
#define CORE_PRODUCTION_FIELD_HAUL_H_

#include "core_common/haul.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Room a load of THIS RESOURCE could actually be delivered into —
/// unlike the alarm's own free-room sum, this one answers "has this got
/// anywhere to go at all", so a store bounded by an outline (a heap under
/// the open sky) makes it unbounded rather than contributing nothing.
/// @param resource What is being hauled. It is a parameter because the door
///        it has to agree with asks the same question: an outline takes a
///        load only when that resource ALREADY LIES IN IT (DeliverToStores,
///        second pass). A haystack is unbounded room for hay and no room at
///        all for rye.
/// @return The sum over numbered stores, or Grams max when an outline that
///         already holds this resource stands built.
Grams ReceivableRoom(const ProductionConfig& config, const WorldState& world, ResourceId resource);

/// @brief What one carrier is worth on this field's shoulder today: a cart's
/// load at harness speed when the settlement has a draught horse to spare, a
/// person's load on foot when it has not.
/// @note The REFERENCE carrier, deliberately — a cart takes its 750 kg
///       whoever leads the horse, and on foot the spread between a strong man
///       and a frail one is paid out in trudodni rather than in tonnage.
HaulRate FieldHaulRate(const ProductionConfig& config,
                       const WorldState& world,
                       const FieldRow& field);

/// @brief Turns the hauling delivered today into grain that actually moved,
/// and re-sizes tomorrow's demand for what is still lying out.
/// @pre The day's last tick, sequential slot, after labor has run.
void SettleHauling(const ProductionConfig& config, WorldState& current);

/// @brief A day in the life of everything lying in a unit's store.
/// @pre Called AFTER the village has eaten: the meal is the needs slot,
///      phase 2, and this is phase 3 of the same tick. Eaten food cannot
///      rot, and the other way round the settlement starves beside a full
///      store with both halves looking correct.
void SpoilStores(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_FIELD_HAUL_H_
