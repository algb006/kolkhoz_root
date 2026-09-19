/// @file
/// @brief Work at a producing unit: the sawmill saws logs into boards (timber
/// design §8б).
/// @threading SINGLE_THREADED
/// Runs from the production sub-step of the decisions slot (phase 3) on the
/// sim thread, at the day's last tick, after labor has drained the seam. It
/// takes from and delivers to the shared stores, which only a sequential slot
/// may do (stock_ops.h).
///
/// THE FORM IS A UNIT'S, NOT AN ORDER'S. A unit turns out its position while
/// workers stand at it, raw material is there and it is not paused (unit
/// rules §4, §5); nothing is marked on the map, so nothing is ordered. The
/// seam has the carting's shape: production writes tomorrow's demand at the
/// day's last tick, labor drains it with real people, and production turns
/// what was drained into goods the next evening.

#ifndef CORE_PRODUCTION_UNIT_PRODUCTION_H_
#define CORE_PRODUCTION_UNIT_PRODUCTION_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Settles every sawmill's day and writes its demand for tomorrow.
///
/// What was sawn: the man-days drained since last night
/// (`production_days_written − production_days_remaining`) ÷
/// `timber_sawing_days_per_m3` cubic metres of boards, out of the logs that
/// make them at `timber_board_yield`. The logs are taken from the stores and
/// the boards go through the door; the slab wood has no holder (STUB, timber
/// design §8б) and is not laid down.
///
/// Tomorrow's demand is the man-days the logs in the stores could still
/// give, capped by the room the boards could go into — zero at a unit that
/// cannot saw at all: not built, dead, paused, or its parent not sound.
/// Nobody is sent to saw into a closed door, for the reason SettleHauling
/// gives at length.
void SettleUnitProduction(const ProductionConfig& config, WorldState& current);

/// @brief What a unit at its wear still turns out, as a share of a new one:
/// 1 at nought, 1 − `wear_output_loss_at_full` at the top of the scale, and
/// straight between them (unit rules §15). One rule for the sawmill and the
/// shops (processing_shops.h).
float WearOutputFactor(const ProductionConfig& config, const UnitRow& unit);

/// @brief Whether a producing unit can work at all today: built, standing,
/// not paused, and its parent sound (module rules).
bool UnitCanWork(const WorldState& world, const UnitRow& unit);

}  // namespace core

#endif  // CORE_PRODUCTION_UNIT_PRODUCTION_H_
