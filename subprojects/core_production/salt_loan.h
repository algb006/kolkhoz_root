/// @file
/// @brief The salt's production half of «Без соли» (#1630 (C); econ's
/// salt-ending-c §3-§5; health §4): the village's salt read once
/// (SaltViewOf, the door SaltToday answers by), the lamp «Соли на столе на N
/// суток» and the elder's loan line, the district's letter, the salt loan and
/// its points taken off the next year's grant.
/// @threading SINGLE_THREADED
/// The lamp and the line are collected with production's alarms (a pure read);
/// the letter (RunSaltLetterDay) and the loan's order (TakeSaltLoan) write in
/// the decisions slot; TakeSaltLoanPointsAtTurn at the limit's year turn,
/// after the grant.

#ifndef CORE_PRODUCTION_SALT_LOAN_H_
#define CORE_PRODUCTION_SALT_LOAN_H_

#include <cstdint>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/order_state.h"
#include "core_common/salt_view.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The village's salt today: held salt and substitutes (stores and
///        pantries), the table's days at the norm (a substitute's kilogram an
///        eater-day), the families by step, the ending's run. `measured`
///        false with the need off.
SaltView SaltViewOf(const ProductionConfig& config, const WorldState& world);

/// @brief The salt lot of the catalogue (the lot carrying salt alone), or an
///        invalid id.
LimitLotId SaltLotOf(const ProductionConfig& config);

/// @brief The salt loan's price in points: the lot's x (1 + goods_loan_markup),
///        rounded; 0 with no salt lot.
std::int32_t SaltLoanPoints(const ProductionConfig& config);

/// @brief The salt loan's lots today (econ's (a), econ-boss-1630-c-loan [1]-[2];
///        health §4): a year of every eater's table - ceil(residents x
///        salt_norm_g_per_eater_day x the year's days ÷ the lot's salt) - at
///        least one; 0 with no salt lot. The loan's price is this x
///        SaltLoanPoints.
std::uint32_t SaltLoanLots(const ProductionConfig& config, const WorldState& world);

/// @brief The lamp kSaltTableShort while the table's days fall under twice
///        salt_step1_days or a family stands on a step; the elder's
///        kSaltLoanAdvice while that lamp would be red, no loan was taken this
///        year and the points do not buy the lot. Need off: nothing.
void CollectSaltAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms);

/// @brief The district's letter kSaltLoanOffered: once a year, on the first
///        day the lamp is lit while the points do not buy the salt lot and no
///        loan was taken this year. Call once a day.
/// Side effects: SaltState::letter_year, the event.
void RunSaltLetterDay(const ProductionConfig& config, WorldState& current);

/// @brief Answers kTakeSaltLoan: one salt lot on the district's cart, its
///        price owed against the next year's grant, once a year.
/// Side effects: limit_deliveries, SaltState::loan_year / loan_points_owed,
///        YearLedger::salt_loans.
OrderRefusal TakeSaltLoan(const ProductionConfig& config,
                          WorldState& current,
                          const OrderRow& order);

/// @brief At the limit's year turn, after the year's grant: the salt loans'
///        points owed come off it (never below nought), and the debt is 0.
void TakeSaltLoanPointsAtTurn(WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_SALT_LOAN_H_
