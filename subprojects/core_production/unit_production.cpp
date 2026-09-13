// Work at a producing unit (unit_production.h).

#include "unit_production.h"

#include <algorithm>
#include <cstdint>

#include "core_catalog/timber_catalog.h"
#include "core_common/ids.h"
#include "core_common/module_rules.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"
#include "field_haul.h"
#include "stock_ops.h"

namespace core {
namespace {

bool CanSaw(const WorldState& world, const UnitRow& unit) {
  return unit.level > 0 && unit.dead == 0 && unit.paused == 0 && ModuleParentSound(world, unit);
}

/// Boards the drained man-days made, taking their logs out of the stores and
/// putting the boards through the door. What the door refuses goes back as
/// logs: a board that fits nowhere was never sawn.
void SawWhatWasWorked(const TimberCatalog& timber,
                      const ProductionConfig& config,
                      WorldState& current,
                      float worked_days) {
  if (!(worked_days > 0.0F) || !(timber.sawing_days_per_board_m3 > 0.0F) ||
      timber.board_grams_per_m3 <= 0) {
    return;
  }
  const float wanted_m3 = worked_days / timber.sawing_days_per_board_m3;
  const Grams logs_taken =
      TakeFromStorage(current, config, timber.log_resource, LogGramsForBoardM3(timber, wanted_m3));
  const float sawn_m3 = std::min(BoardM3FromLogGrams(timber, logs_taken), wanted_m3);
  const Grams boards = GramsFromFloat(sawn_m3 * static_cast<float>(timber.board_grams_per_m3));
  const Grams placed = DeliverToStores(current, config, timber.board_resource, boards);
  if (placed >= boards || boards <= 0) {
    return;
  }
  const float unplaced_share = static_cast<float>(boards - placed) / static_cast<float>(boards);
  const Grams logs_back = GramsFromFloat(static_cast<float>(logs_taken) * unplaced_share);
  DeliverToStores(current, config, timber.log_resource, logs_back);
}

/// Man-days of sawing the stores could give tomorrow: the boards the logs
/// lying there make, no more than the room the boards could go into.
float SawingDemandDays(const TimberCatalog& timber,
                       const ProductionConfig& config,
                       const WorldState& current) {
  if (timber.board_grams_per_m3 <= 0) {
    return 0.0F;
  }
  const float from_logs_m3 =
      BoardM3FromLogGrams(timber, HeldEverywhere(current, timber.log_resource));
  const Grams room = ReceivableRoom(config, current, timber.board_resource);
  const float room_m3 = static_cast<float>(room) / static_cast<float>(timber.board_grams_per_m3);
  return std::min(from_logs_m3, room_m3) * timber.sawing_days_per_board_m3;
}

}  // namespace

void SettleUnitProduction(const ProductionConfig& config, WorldState& current) {
  const TimberCatalog& timber = config.timber;
  if (timber.sawmill_type.value == kInvalidDefIdValue ||
      timber.log_resource.value == kInvalidDefIdValue ||
      timber.board_resource.value == kInvalidDefIdValue) {
    return;
  }
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    if (current.units.rows[row].type.value != timber.sawmill_type.value) {
      continue;
    }
    // By index and re-read: the stores being taken from and delivered to are
    // rows of the same table, and a reference held across the door would be
    // a reference into a row the door has just written.
    const float written = current.units.rows[row].production_days_written;
    const float remaining = current.units.rows[row].production_days_remaining;
    SawWhatWasWorked(timber, config, current, written > remaining ? written - remaining : 0.0F);
    const float demand =
        CanSaw(current, current.units.rows[row]) ? SawingDemandDays(timber, config, current) : 0.0F;
    current.units.rows[row].production_days_remaining = demand;
    current.units.rows[row].production_days_written = demand;
  }
}

}  // namespace core
