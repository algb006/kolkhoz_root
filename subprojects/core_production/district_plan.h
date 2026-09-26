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

/// @brief The district's cart for the debt, on the day the settled snow
/// takes the fields' heaps (district design, «Долг плана телега района берёт
/// и с поля»; register 242, boss seq 180): before the snow, every position
/// still owed is taken from the reaped heaps of its resource on the fields,
/// in row order — the WHOLE debt, first, before the stores, and never more
/// than the debt. Recorded as delivered, in the plan and in the ledger, with
/// one kDistrictTookFromField per resource taken.
/// @pre The caller has seen the snow settle and has not yet let it take the
///      heaps; before the spring's figure (announced == 0) nothing is owed.
/// @note kDeliverPlan does not come here: the chairman ships from the stores.
/// Never below the next sowing's seed, heaps and stores together
/// (DeliverableAboveSeed) — the seed fund opens only to its unsealing.
/// @param seed_as_of The day the seed is held as of (SeedHeldToSowing): today
///        on the snow's day, SeedDayAtTheTurn at the turn.
void TakePlanDebtFromFields(const ProductionConfig& config, WorldState& current, SimDay seed_as_of);

/// @brief The day the seed is held as of AT THE YEAR'S TURN: the closing
///        year's last day. The turn runs on the new year's first tick,
///        BEFORE the rotation turns (production_system.cpp, RunYearStart), so
///        the calendar says January while the slots still describe the year
///        that closes; read as of January, a field that reaped its rye in July
///        reads as a lost winter slot and this spring's potato seed is let go
///        (static review of 0.36.21).
SimDay SeedDayAtTheTurn(const WorldState& current);

/// @brief Grams of `resource` the district's delivery may take from the heaps
/// and the stores together, leaving the next sowing's seed (resources design
/// §6, the ladder fills the seed first; boss seq 5, item 8). The seed fund
/// opens only to the chairman's unsealing of it (FundKind::kSeed).
/// @param seed_as_of See SeedHeldToSowing.
Grams DeliverableAboveSeed(const ProductionConfig& config,
                           const WorldState& current,
                           ResourceId resource,
                           SimDay seed_as_of);

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

/// @brief Whether every position was delivered (PositionDelivered, the met
/// share; 100 % until boss seq 103) — the limit's premium. A plan of nothing
/// is not delivered.
bool PlanFullyDelivered(const ProductionConfig& config, const WorldState& current);

/// @brief Whether a position counts as DELIVERED: `delivered` stands at
/// campaign.csv's `plan_met_share` of `due` or above (0.99 since 2026-09-19,
/// boss seq 89: six kilograms of rye short of 1.116 t must not fail a year).
/// The one rule the verdict (PlanWasMet), the overfulfilment's deduction and
/// the +150 premium (PlanFullyDelivered, since boss seq 103) ask.
bool PositionDelivered(const ProductionConfig& config, Grams due, Grams delivered);

/// @brief Whether the year failed ONLY BY THE WEATHER (boss seq 26, econ's
/// forgiving start): at least one position was not delivered, and every one
/// that was not had at least `weather_year_snow_share` of its shortfall taken
/// by the snow — the ledger's `lost_to_snow` of that same resource this year
/// (by share since boss seq 5, item 7). Such a year is kept out of the trial's
/// "three in a row" — the series neither grows nor breaks (JudgePlan).
/// @pre `current.ledger.current` is still the closing year's book — true at
/// the year start, before the ledger turns (world.cpp, RotateLedger).
bool FailedOnlyBySnow(const ProductionConfig& config, const WorldState& current);

/// @brief The year's overfulfilment, in TONNES OF GRAIN EQUIVALENT (district
/// §1, «За перевыполнение»; boss seq 70, 2026-09-18): every position's
/// delivery over its due, each tonne weighed by its food.csv kcal_per_gram
/// against the catalog's grain, summed; less `overfulfil_shortfall_factor`
/// times the tonnes short on every position NOT delivered (PositionDelivered),
/// weighed the same way; never below 0. A deduction since 2026-09-19 (boss
/// seq 88/89); before it a gate — 0 unless every position was delivered in
/// full — and seed 5 lost 339 points to six kilograms of rye.
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

/// @brief The arable in the plan's circulation for the year beginning on
///        `year_start`, in hectares: fields with a chain that were sown in
///        that year, or that carry a crop the core never saw sown (genesis).
///        Land enters the plan by its first sowing (district design, «В
///        оборот земля входит первым севом»; 0.35.8).
/// @param year_start The year's first campaign day: today's year for the
///        daily running maximum, the closing year at its turn.
float WorkedArableHa(const WorldState& current, SimDay year_start);

/// @brief The district's verdict on the year just shipped: the counters, the
/// reputation step, kPlanMet or kPlanFailed, kPlanTrialDue on the day the
/// threshold is reached, Korenev called on a failure; then the old figure
/// and the year's unsealings are cleared.
void JudgePlan(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_DISTRICT_PLAN_H_
