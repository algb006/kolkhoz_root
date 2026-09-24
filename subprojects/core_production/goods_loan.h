/// @file
/// @brief The district's goods loan (wage design §6; district design,
/// «Товарный заём»; boss, boss-core-epoch1-5 seq 15 and 30).
/// @threading SINGLE_THREADED
/// All three run on the sim thread in the production decisions sub-step
/// (phase 3): the order where the order book is read, the repayment at the
/// year's turn, the alarm with the others.
///
/// THE LOAN, as boss settled it: one GENERAL loan by resource, the seed loan
/// its first use and food the same mechanism later.
///   the order → the district's cart, the same road and days as a limit lot
///   → owed × (1 + goods_loan_markup) → paid at the turn after the plan, in
///   kind, from what the plan could take above the held seed → the unpaid
///   rest carries on and takes the markup again, with no ceiling.
/// The reputation does not suffer from a loan: so the design says.

#ifndef CORE_PRODUCTION_GOODS_LOAN_H_
#define CORE_PRODUCTION_GOODS_LOAN_H_

#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The most of `resource` the district lends this year: the seed need
///        of the resource's next sowing (SeedNeedByResource — the number the
///        seed booking reads), grams. 0 for a resource that is no crop's
///        seed: other goods are a STUB, not this stage (boss seq 15).
Grams GoodsLoanCeiling(const ProductionConfig& config,
                       const WorldState& current,
                       ResourceId resource);

/// @brief Reads a kTakeGoodsLoan: `order.amount` grams of `order.resource`,
///        0 for the whole ceiling, capped at it. Puts the goods on the
///        district's cart (LimitDeliveryRow with no lot, due on
///        LimitCartArriveDay) and books the loan: PlanState::goods_loan_owed
///        gains the grams × (1 + goods_loan_markup), goods_loan_taken and the
///        year's book the grams.
/// @return kRuleForbids for a ceiling of nought (not a seed, or no sowing to
///         come), a loan of this resource taken this year already, or an
///         order read in the year's turning hour (the books are closing);
///         kNowhereToStore when no store of the village takes the resource;
///         kNone when lent.
OrderRefusal TakeGoodsLoan(const ProductionConfig& config,
                           WorldState& current,
                           const OrderRow& order);

/// @brief The year's turn, AFTER the plan's delivery (production_system.cpp,
///        RunYearStart): every resource owed is paid from the stores, as much
///        as the plan could take above the held seed (DeliverableAboveSeed)
///        less what of that lies in the fields' heaps, which the stores do
///        not hold,
///        booked in the closing year's goods_loan_repaid. What is left takes
///        the markup again and carries on. The year's taken marks clear.
/// @note Paid from above the seed on purpose: a repayment that took the held
///       seed would open the chain «семена 0 → пустое поле» the loan exists
///       to close.
void RepayGoodsLoans(const ProductionConfig& config, WorldState& current);

/// @brief kGoodsLoanOwed for every resource PlanState::goods_loan_owed holds
///        above nought. A pure read.
void CollectGoodsLoanAlarms(const WorldState& world, std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_GOODS_LOAN_H_
