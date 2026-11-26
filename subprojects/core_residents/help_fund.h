/// @file
/// @brief #2199 «фонд помощи» (core_common/help_fund_state.h): the turn's top-up of the fund,
///        the month's issue from it to the families that ate below the line, the share's order.
/// @threading SINGLE_THREADED
/// Runs in the residents' decisions sub-step (phase 3) on the sim thread, beside the ration
/// (family_exchange.h): it writes HelpFundState, the stores' stock, the families' pantries,
/// the ledger's help_issued and the step's events, so it can only live in a sequential slot.
#ifndef CORE_RESIDENTS_HELP_FUND_H_
#define CORE_RESIDENTS_HELP_FUND_H_

#include <vector>

#include "core_common/quantities.h"
#include "core_common/world_state.h"

namespace core {

struct FoodConfig;

/// @brief The fund's target by its share: share × the closed year's harvest, of each grain
///        and of potato (config.help_fund_foods), grams by ResourceId. Empty in year 1.
ResourceAmounts HelpFundTarget(const FoodConfig& config, const WorldState& world);

/// @brief On the year's first day at kHelpFundRules.turn_hour: the fund set to its target -
///        topped up, not added to (econ: the leftover counts toward the new share).
void TopUpHelpFund(const FoodConfig& config, WorldState& current);

/// @brief On a month's first day at hour 0 (the closed month's table still stands): every
///        family whose month_eaten_kcal fell below kHelpFundRules.line of its month_need_kcal
///        gets the gap to the line from the fund, food by food in config.help_fund_foods'
///        order, at each food's kcal_per_gram, as far as the fund holds and the stores still
///        have it; in id order. Books help_issued, raises kHelpFundIssued, and kHelpFundEmpty
///        once when it ran dry with a family still short.
/// @param reserve IssueReserve WITHOUT the help fund (the fund draws from its own part).
void IssueFromHelpFund(const FoodConfig& config,
                       const std::vector<Grams>& reserve,
                       WorldState& current);

/// @brief Consumes the step's kSetHelpFundShare orders: the share written, the fund set to the
///        new target at once. Refusals: kNotEligible (an amount outside 0..max_permille).
void ConsumeHelpFundOrders(const FoodConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_HELP_FUND_H_
