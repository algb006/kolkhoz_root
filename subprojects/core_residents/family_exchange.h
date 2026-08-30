/// @file
/// @brief The family <-> kolkhoz exchange: distribution, ration, seed fund.
/// @threading SINGLE_THREADED
/// Runs in the residents sub-step of the decisions phase (slot 3), once per
/// day boundary, from the sim thread. It writes family pantries and unit
/// stocks across whole tables, so it can never be a parallel phase — and it
/// does not need to be: the work is a few multiplications per household.
///
/// Model: manual/66-food-model.md §2. Design sources: labor-payment §3
/// (the trudodni account and its yearly burn), §5 (the minimum ration), §7
/// (the once-a-month auto-rule), resources design §2 (the seed fund is
/// off-limits to automatic issue), household design §2 (net fishing).
///
/// Where goods come from: the exchange takes food out of UNIT STOCKS in row
/// order and never asks which unit type stores what. It does not need to —
/// food only ever lies in a store, and knowing storage capacities would mean
/// reading another module's parsed configuration.

#ifndef CORE_RESIDENTS_FAMILY_EXCHANGE_H_
#define CORE_RESIDENTS_FAMILY_EXCHANGE_H_

#include "core_common/world_state.h"
#include "food_config.h"

namespace core {

/// @brief Runs one day of the exchange: the monthly distribution against
/// outstanding trudodni, the minimum ration for the hungry, the nets, and —
/// on the first day of the economic year — the burning of both counters.
/// @param life_speedup Biological years per game year (life.csv), needed to
///        tell an eater from a babe in arms.
/// @pre Called once per day boundary, from the sequential decisions slot,
///      after demography has settled the day's births and deaths.
/// @note A config without a resource roster (a table-less test world) makes
///       every step below a no-op rather than an error.
void RunFamilyExchange(const FoodConfig& config, float life_speedup, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_FAMILY_EXCHANGE_H_
