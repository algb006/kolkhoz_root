/// @file
/// @brief The district's milk cart (district §9, «Молоко — в плане с первого
/// года, и его увозит телега района»; register 231; boss seq 98 and 113).
/// @threading SINGLE_THREADED
/// Both calls run in the production decisions sub-step (phase 3) on the sim
/// thread, around the herd day: they take from the stores and write the
/// plan's books.
///
/// THE MILK DAY, as the design orders it — «сперва району, как с колёс»:
///   milking → the cart takes the position's share of the day → the issue
///   by the norm from what is left → the rest by the same cart over the plan
///   → nothing stays overnight.
/// In the core's slot the residents' issue runs BEFORE the herd day, so the
/// day is cut at the issue: the share leaves at tonight's milking
/// (ShipMilkShare), the family issue takes its milk next morning, and what
/// the issue left goes before the next milking (ShipMilkLeftover). The
/// district's horse and hands: the kolkhoz pays nothing for the cart. All
/// year: before the spring names a figure, what is left goes as delivered
/// outside any position — over the plan by construction.

#ifndef CORE_PRODUCTION_MILK_CART_H_
#define CORE_PRODUCTION_MILK_CART_H_

#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The first day of the year the milk position runs from: the first
/// day of spring. The plan's letter comes in January (boss seq 210), and the
/// milk still counts from the spring and the cart still comes from it (boss
/// seq 213) — before this day the position does not stand.
std::uint32_t MilkSeasonFirstDay();

/// @brief Whether the milk position stands today, and so whether the
/// district's cart comes every day: a plan named with milk in it, and the
/// spring reached. Also the ride home a patient from the district's hospital
/// takes (district_car.h) — out of it he walks in from the border.
bool MilkPositionStands(const ProductionConfig& config, const WorldState& current);

/// @brief Before the day's milking: every gram of milk the stores still hold
/// — what the morning issue left — leaves for the district OVER THE PLAN
/// (PlanState::delivered_outside), with a position standing or not: it pays
/// no debt and makes up no short day (the milk with debt, 0.34.46). Booked in
/// the ledger's delivered.
void ShipMilkLeftover(const ProductionConfig& config, WorldState& current);

/// @brief After the day's milking: the position's share of the day
/// (PlanState::milk_daily_share) plus the debt of earlier short days
/// (PlanState::milk_debt) leaves against the milk position, or as much of it
/// as the stores hold. What they cannot give becomes the new debt. Nothing
/// before the spring's figure.
void ShipMilkShare(const ProductionConfig& config, WorldState& current);

/// @brief kMilkAllToDebt while the milk debt stands and the position with it:
/// the cart took every litre at the last milking and the yards' morning
/// issue has no milk (boss, boss-core-epoch1-5 seq 7). A pure read.
void CollectMilkDebtAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms);

}  // namespace core

#endif  // CORE_PRODUCTION_MILK_CART_H_
