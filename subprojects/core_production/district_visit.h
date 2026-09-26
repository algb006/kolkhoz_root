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
/// THE ORDER OF ONE DAY. Announcements first, then arrivals: with a notice of
/// zero days a regular visit is announced and arrives on the same tick, and
/// the announcement must come first. A visit arriving today that finds
/// something calls its senior for tomorrow, never for today.

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
/// @pre Called once per day, at the day's first tick, BEFORE
///      ArriveDistrictVisits.
void AnnounceRegularVisits(const ProductionConfig& config, WorldState& current);

/// @brief Right after the district's verdict at the year's turn, when it is
///        kFailed: calls Korenev for tomorrow, an extraordinary visit on
///        DistrictVisitCause::kPlanFailed, unannounced. A Korenev visit
///        already on its way is not doubled.
void CallPlanFailedVisit(WorldState& current);

/// @brief What a visit finds, computed on the day it arrives: the outcome the
///        event carries. A finance face (Polushkina, Zhernova) finds
///        kDiscrepancy when any plannable produce stands in the stores above
///        PlanState::accumulation_limit (district §9, 2026-09-18). Every other
///        finding is STUB kNone — the accounts' wait for the books (characters
///        §2) — and gift is kNone, as no gift visit is raised.
DistrictVisitOutcome InspectVisit(const WorldState& current, const DistrictVisitRow& visit);

/// @brief «Не сдал и попался» (district §9): takes every gram standing above
///        the accumulation limit out of the stores. What the year's plan
///        position of that produce still owes is taken FIRST and booked as
///        delivered (district §9 amended; 0.36.21), up to the debt and no
///        further; only the rest is booked in YearLedger::seized, and the
///        raikom's reputation falls by the catalog's seizure loss once if any
///        such rest was taken.
/// @return Grams seized beyond the debt, all produce together — nought when
///         the whole surplus went to the plan.
Grams SeizeAboveLimit(const ProductionConfig& config, WorldState& current);

/// @brief At the day's first tick: every visit whose day has come is
///        inspected (InspectVisit), a finance face's finding is taken on the
///        spot (SeizeAboveLimit) — and when the year's unmet position took
///        the whole surplus, the finding is dropped to kNone (0.36.21): no
///        summons, no senior. The visit is raised as kDistrictVisit —
///        kInterrupting when extraordinary, kNotable otherwise — and leaves
///        the table. A regular visit that still found anything calls the
///        senior of its channel for tomorrow on
///        DistrictVisitCause::kJuniorSignal, and a discrepancy summons the
///        chairman «на ковёр».
void ArriveDistrictVisits(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_VISIT_H_
