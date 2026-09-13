// Felling (timber_felling.h).

#include "timber_felling.h"

#include <algorithm>
#include <cstdint>

#include "core_catalog/timber_catalog.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"

namespace core {
namespace {

/// The stand's catalogue row, or nullptr for a stand whose table row the
/// build does not carry (a save from a different bake).
const TimberStandDef* DefOf(const ProductionConfig& config, const TimberStandRow& stand) {
  return stand.table_row < config.timber.stands.size() ? &config.timber.stands[stand.table_row]
                                                       : nullptr;
}

}  // namespace

OrderRefusal MarkFelling(const ProductionConfig& config,
                         WorldState& current,
                         const OrderRow& order) {
  const std::uint32_t row = FindRow(current.stands, order.stand);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  // ONE FELLING AT A TIME IN THE VILLAGE, not one per stand: "новую порубку
  // назначить нельзя, пока идёт эта" (time design §11). A felling is going
  // while timber is marked anywhere — the logs already lying and waiting for
  // a cart do not count, they are carting's business.
  for (const TimberStandRow& other : current.stands.rows) {
    if (other.marked_m3 > 0.0F) {
      return OrderRefusal::kConflictsWithActive;
    }
  }
  TimberStandRow& stand = current.stands.rows[row];
  const float unmarked = stand.stock_m3 - stand.marked_m3;
  if (!(order.volume_m3 > 0.0F) || order.volume_m3 > unmarked) {
    return OrderRefusal::kRuleForbids;
  }
  stand.marked_m3 = order.volume_m3;
  stand.work_days_remaining = order.volume_m3 * config.timber.felling_days_per_m3;
  return OrderRefusal::kNone;
}

void FellFinishedStands(const ProductionConfig& config, WorldState& current) {
  for (TimberStandRow& stand : current.stands.rows) {
    if (!(stand.marked_m3 > 0.0F) || stand.work_days_remaining > 0.0F) {
      continue;
    }
    const TimberStandDef* const def = DefOf(config, stand);
    const float felled = std::min(stand.marked_m3, stand.stock_m3);
    if (def != nullptr) {
      stand.load_grams += LogGramsFromVolume(config.timber, *def, felled);
    }
    // The firewood the same trees give has no holder yet (STUB, timber
    // design §8a): it is felled and not laid down.
    stand.stock_m3 = std::max(stand.stock_m3 - felled, 0.0F);
    stand.marked_m3 = 0.0F;
    stand.work_days_remaining = 0.0F;
  }
}

void GrowOldForest(const ProductionConfig& config, WorldState& current) {
  for (TimberStandRow& stand : current.stands.rows) {
    if (stand.kind != TimberStandKind::kForestOld) {
      continue;
    }
    const TimberStandDef* const def = DefOf(config, stand);
    if (def == nullptr) {
      continue;
    }
    // A CEILING, NOT A LEDGER OF YEARS. Keeping each year's trunks apart to
    // let the oldest vanish would be a list per stand for a rule the design
    // states in one line; at a steady fall the stock simply never exceeds
    // what that many years drop, and a felling below the ceiling makes room
    // for next year's. What is marked is never taken back by the ceiling.
    const float ceiling = std::max(OldForestCeilingM3(config.timber, *def), stand.marked_m3);
    stand.stock_m3 = std::min(stand.stock_m3 + YearlyOldTrunksM3(config.timber, *def), ceiling);
  }
}

}  // namespace core
