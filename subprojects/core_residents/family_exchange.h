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

#include <utility>
#include <vector>

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

/// @brief The SEALED FUNDS, grams per resource, dense by ResourceId: the seed
/// fund, the plan reserve with what will rot before the delivery and the
/// fodder claim, less the chairman's unsealings (resources design §6) — the
/// reserve the distribution, the ration and the ration alarm stay above, and
/// the night theft too (boss seq 18: «опечатанное не крадётся»). NOT the
/// planned crop whole: 0.34.39 sealed that against the thief too and was
/// withdrawn (boss, boss-core-epoch1-4 seq 2), and since 0.34.42 the
/// distribution does not hold it either — the plan rung holds what is owed,
/// carry-over included (seq 9 and 10).
/// @return Sized to the resource roster; empty when there is none.
std::vector<Grams> SealedFunds(const FoodConfig& config, const WorldState& world);

/// @brief The ration positions whose FREE stock is nought while the sealed
/// funds (seed, plan reserve, fodder — the distribution's own reserve) hold
/// some: the position and the grams in the funds. The half of
/// kReserveFullNothingToEat that is about the stores; the other half — a
/// family at the threshold — is the caller's.
std::vector<std::pair<ResourceId, Grams>> LockedRationFood(const FoodConfig& config,
                                                           const WorldState& world);

/// @brief Settles every pending kSetRation (order_state.h): the village-wide
/// checkbox when the order names no family, the yard's decision when it
/// names one. Settled in the step it is read, like the night trader's.
/// Refusals: kNoSuchSubject (the family is gone), kRuleForbids (already so).
/// Side effects: ChairmanState::ration_auto, FamilyRow::ration_granted.
void ConsumeRationOrders(WorldState& current);

/// @brief Settles every pending kSetIssueNorm (order_state.h): the position's
/// grams per trudoden from the next distribution on. The first such order
/// copies the whole bundle out of `config` into WorldState::issue_norms.
/// Refusal: kNotEligible (not food). Side effect: WorldState::issue_norms.
void ConsumeIssueNormOrders(const FoodConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_FAMILY_EXCHANGE_H_
