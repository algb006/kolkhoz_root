/// @file
/// @brief The shops of epoch I at work: the sauerkraut shop, the smokehouse
/// and the workshops' cooperage (production units §8а; registers 239-240; the
/// human's word of 2026-09-19 relayed by econ), and the barrels they share.
/// @threading SINGLE_THREADED
/// Runs from the production sub-step of the decisions slot (phase 3) on the
/// sim thread: the shops at the day's last tick after the carting and the
/// sawmill and before the night's spoiling — after the day's issue, so the
/// village eats first and the shop takes what is left; the barrels' wear at
/// the year's turn. It takes from and delivers to the shared stores, which
/// only a sequential slot may do (stock_ops.h).
///
/// THE SAWMILL'S SEAM (unit_production.h): production writes tomorrow's
/// demand in man-days to the shop's production_days, labor drains it with the
/// parent's post holders (no more than ProcessingPlaces), and production turns
/// what was drained into batches the next evening. A shop works its recipes
/// in production.csv order; the worked days go to the first that can use them.
///
/// THE BARRELS ARE ROOM, NOT TARE (register 240): a barrel is 15 kg in the
/// stores and holds barrel_capacity_kg of what lives in barrels. Occupied =
/// every in-barrel resource held ÷ capacity; free = held − occupied. Goods
/// leaving by any door free their barrels by themselves. Nothing spills when
/// the barrels wear below what they hold (boss seq 184, question 4): the shop
/// stands until new ones come. Nothing arriving is refused for want of them
/// (question 3): only a shop stands.

#ifndef CORE_PRODUCTION_PROCESSING_SHOPS_H_
#define CORE_PRODUCTION_PROCESSING_SHOPS_H_

#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Whole barrels in the village's stores (the barrel resource ÷ its
/// mass per piece). 0 in a world with no barrel resource.
std::int64_t BarrelsHeld(const ProductionConfig& config, const WorldState& world);

/// @brief Grams of in-barrel goods the free barrels could still take:
/// BarrelsHeld × capacity less every in-barrel resource held, never below 0.
Grams BarrelRoomFree(const ProductionConfig& config, const WorldState& world);

/// @brief Settles every shop's day and writes its demand for tomorrow: the
/// man-days drained since last night become batches (× the shop's wear on
/// the output), taken out of the stores and put through the door, booked as
/// `processed` and `made`. Tomorrow's demand is what the stores, the room and
/// the barrels allow — zero at a shop that cannot work at all.
/// @pre The day's last tick, after labor and the day's issue, before
///      SpoilStores.
void SettleProcessing(const ProductionConfig& config, WorldState& current);

/// @brief THE SAME-DAY SHOPS' MORNING (boss, host-econ-shops seq 31 on econ
/// seq 30): a shop whose main input spoils very fast (WorksTheSameDay — the
/// smokehouse's meat and fish, two days) writes its demand now, from what
/// the day's slaughter and cull have just put in the stores, so its master
/// is placed this morning and the meat is worked before tonight's rot.
/// Until 0.34.11 it waited for the evening, like every shop, and worked
/// tomorrow: host's seed 9 slaughtered 490 kg and smoked 245 — the night
/// between had taken the half.
/// @pre The day's first tick, after the herd day, before sunrise — nothing
///      of today is drained yet, so the seam is rewritten whole.
void OpenSameDayShops(const ProductionConfig& config, WorldState& current);

/// @brief One kProcessingStopped per shop that has something to work and
/// stands for want of a barrel or a second input (alarm_state.h).
void CollectProcessingAlarms(const ProductionConfig& config,
                             const WorldState& world,
                             std::vector<Alarm>& alarms);

/// @brief The barrels' year (register 240): round(held × barrel_wear_per_year)
/// whole barrels leave the stores, booked as `spoiled`.
/// @pre The year's turn.
void WearBarrels(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_PROCESSING_SHOPS_H_
