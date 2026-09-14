/// @file
/// @brief The district's visits in the simulation: announcing the regular
/// ones, calling the extraordinary ones, and computing a visit on the day it
/// arrives (characters design §2, "Эпоха I числами"; boss, parcels 188 and
/// 324).
/// @threading SINGLE_THREADED
/// Every entry point runs from the production sub-step of the decisions slot
/// (phase 3) on the sim thread — the arrivals and the announcements at the
/// day's first tick, the plan's call right after the district's verdict at
/// the year's turn. They write WorldState::district_visits and the step's
/// events, so they can only live in a sequential slot.
///
/// THE ORDER OF ONE DAY. Arrivals first, then announcements: a visit that
/// arrives today and finds something calls its senior for tomorrow, and an
/// announcement made today cannot be for a visit arriving today unless the
/// notice is zero days — in which case the announcement and the arrival are
/// the same tick, announced first by construction of AnnounceRegularVisits.

#ifndef CORE_PRODUCTION_DISTRICT_VISIT_H_
#define CORE_PRODUCTION_DISTRICT_VISIT_H_

#include "core_common/district_visit_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The senior of a junior's channel: Karasev → Stozharov, Polushkina →
/// Zhernova (characters design §7, "Симметрия").
/// @return The face itself for a face that is not a junior.
DistrictFace SeniorOfChannel(DistrictFace junior);

/// @brief At the day's first tick: for each junior whose regular visit
///        arrives `notice_days` from today — the first day of their month —
///        adds a row and raises kDistrictVisitAnnounced. Once per junior per
///        year; a row already standing for that face and day is not doubled.
/// @pre Called once per day, at the day's first tick, AFTER
///      ArriveDistrictVisits.
void AnnounceRegularVisits(const ProductionConfig& config, WorldState& current);

/// @brief Right after the district's verdict at the year's turn, when it is
///        kFailed: calls Korenev for tomorrow, an extraordinary visit on
///        DistrictVisitCause::kPlanFailed, unannounced. A Korenev visit
///        already on its way is not doubled.
void CallPlanFailedVisit(WorldState& current);

/// @brief What a visit finds, computed on the day it arrives: the outcome the
///        event carries. STUB: found is always kNone — the core keeps no books
///        for a discrepancy to be in (boss, parcel 324) — and gift is kNone,
///        as no gift visit is raised.
DistrictVisitOutcome InspectVisit(const WorldState& current, const DistrictVisitRow& visit);

/// @brief At the day's first tick: every visit whose day has come is
///        inspected (InspectVisit), raised as kDistrictVisit — kInterrupting
///        when extraordinary, kNotable otherwise — and leaves the table. A
///        regular visit that found anything calls the senior of its channel
///        for tomorrow on DistrictVisitCause::kJuniorSignal.
void ArriveDistrictVisits(WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_VISIT_H_
