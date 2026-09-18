/// @file
/// @brief The district's plan in the simulation: the spring norm, the year's
/// delivery and the verdict at the turn (district design §9; epochs design
/// §8; boss's decisions of 2026-09-12 and 2026-09-13).
/// @threading SINGLE_THREADED
/// Every entry point runs from the production sub-step of the decisions slot
/// (phase 3) on the sim thread — the norm on the first day of spring, the
/// delivery and the verdict at the year's turn. They write the plan, the
/// stores, the ledger and the reputation, so they can only live in a
/// sequential slot.
///
/// WHY ITS OWN FILE. production_system.cpp had reached 1362 lines against
/// the 1000 limit (boss, parcel 332); the plan is one subject with one
/// state block (PlanState) and moved out whole, unchanged.

#ifndef CORE_PRODUCTION_DISTRICT_PLAN_H_
#define CORE_PRODUCTION_DISTRICT_PLAN_H_

#include <string>

#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "production_config.h"

namespace core {

/// @brief The year's delivery at the turn: what the plan asked for and is
/// STILL OWED leaves the stores and is recorded as delivered and in the
/// ledger. What the chairman shipped earlier (DeliverPlanNow) is not asked
/// for twice.
void DeliverPlan(const ProductionConfig& config, WorldState& current);

/// @brief «Сдать сейчас» (kDeliverPlan; econ's audit M2, Л1): what is still
/// owed of one position — or of every position when `only` is invalid —
/// leaves the stores now, as much as the stores hold, and is added to
/// `delivered`. A partial shipment is a shipment; the rest waits for the
/// turn or for the next order.
/// @param amount Grams of `only` to ship, over the debt as readily as under
///        it (district §1: the surplus is overfulfilment); 0 ships the debt.
///        Positive only with a valid `only` (the boundary's shape).
/// @return kNoPlanYet before the spring's figure; kRuleForbids when nothing
///         left the stores (nothing owed, none of it there, or `only` is no
///         position of the plan); kNone else.
OrderRefusal DeliverPlanNow(const ProductionConfig& config,
                            WorldState& current,
                            ResourceId only,
                            Grams amount);

/// @brief Whether every position was delivered in full, 100 % — the limit's
/// premium. A plan of nothing is not delivered in full.
bool PlanFullyDelivered(const WorldState& current);

/// @brief The year's overfulfilment, in TONNES OF GRAIN EQUIVALENT (district
/// §1, «За перевыполнение»; boss seq 70, 2026-09-18): every position's
/// delivery over its due, each tonne weighed by its food.csv kcal_per_gram
/// against the catalog's grain, summed — and 0 unless EVERY position was
/// delivered in full: «сверху» is above a plan met, so a surplus of rye does
/// not cover a potato short.
///
/// TONNES AND NOT PERCENT, and it was percent for one evening. The first
/// year's potato plan is seven tonnes against a surplus of 88-119 (host's
/// measure, econ-host-lever-pass3 seq 31-32): priced by the percent, the
/// scale's cap came at four tonnes and the lever went silent in the first
/// year it could be pulled. Tonnes are summed across positions only because
/// they are brought to grain first — the reason PlanWasMet refuses a raw sum.
/// A resource with no kcal row weighs nothing (it feeds nobody).
/// @return 0 or more.
float PlanOverfulfilGrainTonnes(const ProductionConfig& config, const WorldState& current);

/// @brief Whether every position was delivered to the share that counts as
/// met (`plan_met_share`), position by position and not by the total.
bool PlanWasMet(const ProductionConfig& config, const WorldState& current);

/// @brief The spring norm: a share of what last year's worked arable gives
/// at a normal yield, by the district's positions; marks the plan announced.
/// In the campaign's first year a share of the start stock of each
/// position's produce instead, and a position with no start stock asks
/// nothing (ProductionConfig::first_plan_start_stock_share).
void AnnouncePlan(const ProductionConfig& config, WorldState& current);

/// @brief The accumulation limit, named with the plan (district §9; register
/// 234): for every produce of the district's positions, `accumulation_share`
/// × (next year's seed of the fields whose next slot grows it + this year's
/// figure + last year's eaten and fed of it, off the closed book), into
/// PlanState::accumulation_limit. Cleared first; left empty in the first
/// year, which has no book of a year gone. Called by AnnouncePlan after the
/// figure is set.
void NameAccumulationLimit(const ProductionConfig& config, WorldState& current, bool first_year);

/// @brief Reads the first plan's knobs: campaign.csv
///        `first_plan_start_stock_percent` and the start stock by resource
///        (start_stock.csv, amount x kg_per_unit, summed over places).
/// @return false with `error` naming the table for a value out of range.
bool ParseFirstPlan(const ITableSet& tables, ProductionConfig& config, std::string& error);

/// @brief The arable worked this year (fields with a chain), in hectares.
float WorkedArableHa(const WorldState& current);

/// @brief The district's verdict on the year just shipped: the counters, the
/// reputation step, kPlanMet or kPlanFailed, kPlanTrialDue on the day the
/// threshold is reached, Korenev called on a failure; then the old figure
/// and the year's unsealings are cleared.
void JudgePlan(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_PLAN_H_
