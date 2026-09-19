/// @file
/// @brief The chairman's trip to the district (econ/manual/proposals/
/// district-trip.md; the human's words of 2026-09-19; boss seq 187, 205-206):
/// his own trip once a month, the summons «на ковёр», and the plan bargained
/// in March. The village's orders are refused while he is away by the gate
/// in core_common/chairman_away.h; the rest of the trip lives here.
/// @threading SINGLE_THREADED
/// Runs in the production sub-step of the decisions slot (phase 3) on the sim
/// thread: the orders when read, the trip's hours every tick, the summons'
/// causes where they arise (the year's verdict, an auditor's finding, the
/// reputation crossing «на карандаше»).

#ifndef CORE_PRODUCTION_DISTRICT_TRIP_H_
#define CORE_PRODUCTION_DISTRICT_TRIP_H_

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief kTripToDistrict: books his trip for the next `depart_hour`.
/// @return kChairmanAway (a trip or a summons stands), kTripThisMonth (his
///         own trip this month already), kNone booked.
OrderRefusal OrderTripToDistrict(const ProductionConfig& config, WorldState& current);

/// @brief kTradePlan: one position ±plan_trade_percent, or replaced by a
/// crop on the same hectares; costs raikom_reputation (order_state.h).
OrderRefusal OrderTradePlan(const ProductionConfig& config,
                            WorldState& current,
                            const OrderRow& order);

/// @brief The trip's hour: the summons' letter at its day's first tick, the
/// summons' own departure booked on its day, the departure at
/// `depart_hour` (a blizzard cancels his own trip and moves a summons a day),
/// the return. Call every tick.
void RunDistrictTrip(const ProductionConfig& config, WorldState& current);

/// @brief Calls him «на ковёр» for `cause`: the letter after
/// summon_letter_delay_days, the summons summon_after_letter_days later. A
/// summons already standing is not doubled.
void SummonChairman(const ProductionConfig& config, WorldState& current, SummonCause cause);

/// @brief A year that closed with the plan failed calls him «на ковёр».
/// @pre Right after JudgePlan, at the year's turn.
void SummonIfTheYearFailed(const ProductionConfig& config, WorldState& current);

/// @brief The reputation that crossed into «на карандаше» since `previous`
/// calls him. Call in the daily block, after the year's verdict.
void SummonOnThePencil(const ProductionConfig& config,
                       const WorldState& previous,
                       WorldState& current);

/// @brief Whether he is away at any hour of today — a district visit waits
/// for him (boss seq 206, 6).
bool AwayToday(const WorldState& world);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_TRIP_H_
