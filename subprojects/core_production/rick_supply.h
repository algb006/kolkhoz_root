/// @file
/// @brief A rick's supply from another rick (#1056 C1.2; econ's stage C
/// contract 574c366, «Порог стога»; logistics_state.h, kRickSupply).
/// @threading SINGLE_THREADED
/// Both calls run in the production decisions sub-step (phase 3) on the sim
/// thread: the price at the day's last tick, the settle every hour. They
/// write the ricks' stock and their RickSupplySeam (unit_state.h).
///
/// THE STACK RULE STAYS THE MAIN SENDER (decision 320; boss ruling 1): a
/// meadow's stack goes to the rick that needs it (feed_home.h,
/// RickForLoadFrom). This adds the rick that has no stack to come: its herds'
/// hay short within its THRESHOLD - the urgent check's horizon
/// (UrgentHayHorizonDays) plus farming.csv `rick_threshold_add_days`, STUB 7 -
/// is carted hay from the rick with the most to spare, rick -> rick (gate
/// C1-0: far-rick days > 2 in the base, so it is built). A sender keeps its
/// own herds' hay for TWICE the threshold; a rick no herd is bound to spares
/// all it holds. Priced in carting, not in buffer days (ruling 1).

#ifndef CORE_PRODUCTION_RICK_SUPPLY_H_
#define CORE_PRODUCTION_RICK_SUPPLY_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief THE RICK'S SUPPLY PRICE, at the day's last tick for the next
///        morning's placement (the milk leg's lesson, milk_cart.h): every
///        rick's seam is cleared, then under FarmingConfig::rick_supply each
///        rick below its threshold (by the ricks' drain, RickHayWithin) -
///        neither emptied nor a works' receiver - gets hay from the rick
///        with the most to spare: its herds' lack over the threshold's days,
///        at least one cart load, at most the sender's spare. Earliest-short
///        first; ties by row.
/// @note The drain counts from today, whose feeding is already done: the
///       lack is the larger by up to a day's ration (named, not corrected).
void PriceRickSupply(const ProductionConfig& config, WorldState& current);

/// @brief THE RICK'S SUPPLY SETTLE: what the hauling drained off a rick's
///        supply seam since the last settle becomes hay carried from its
///        sender into it, by the share drained, hourly and at the last tick
///        (before PriceRickSupply), as the milk leg settles (milk_cart.h).
void SettleRickSupply(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_RICK_SUPPLY_H_
