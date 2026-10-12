// Who carried it: the split of hauled grams between backs and carts
// (core_common/hauled_book.h; #671's instrument).

#include "core_common/hauled_book.h"

#include <cmath>
#include <cstddef>

#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"

namespace core {

std::uint32_t TaskRowOfWork(const WorldState& world, const WorkAssignment& work) {
  for (std::uint32_t row = 0; row < world.logistics_tasks.rows.size(); ++row) {
    if (WorkServesTask(work, world.logistics_tasks.rows[row])) {
      return row;
    }
  }
  return kNoRow;
}

void NoteHaulDrain(WorldState& world, const WorkAssignment& work, float drained, bool on_foot) {
  if (drained <= 0.0F) {
    return;
  }
  const std::uint32_t row = TaskRowOfWork(world, work);
  if (row == kNoRow) {
    return;
  }
  LogisticsTaskRow& task = world.logistics_tasks.rows[row];
  (on_foot ? task.drained_on_foot : task.drained_by_cart) += drained;
}

float FootShareOf(const WorldState& world, const WorkAssignment& load, bool settlement_has_carts) {
  const float unnoted = settlement_has_carts ? 0.0F : 1.0F;
  const std::uint32_t row = TaskRowOfWork(world, load);
  if (row == kNoRow) {
    return unnoted;
  }
  const LogisticsTaskRow& task = world.logistics_tasks.rows[row];
  const float sum = task.drained_on_foot + task.drained_by_cart;
  return sum > 0.0F ? task.drained_on_foot / sum : unnoted;
}

void SpendHaulNotes(WorldState& world, const WorkAssignment& load, float fraction) {
  const std::uint32_t row = TaskRowOfWork(world, load);
  if (row == kNoRow) {
    return;
  }
  LogisticsTaskRow& task = world.logistics_tasks.rows[row];
  const float keep = fraction >= 1.0F ? 0.0F : (fraction <= 0.0F ? 1.0F : 1.0F - fraction);
  task.drained_on_foot *= keep;
  task.drained_by_cart *= keep;
}

void BookHauled(YearLedger& book, ResourceId resource, Grams moved, float foot_share) {
  if (moved <= 0) {
    return;
  }
  const float share = foot_share < 0.0F ? 0.0F : (foot_share > 1.0F ? 1.0F : foot_share);
  // Rounded once, the rest to the cart: the two books sum to `moved` exactly.
  const auto on_foot =
      static_cast<Grams>(std::llround(static_cast<double>(moved) * static_cast<double>(share)));
  AddLedgerAmount(book.hauled_on_foot, resource, on_foot);
  AddLedgerAmount(book.hauled_by_cart, resource, moved - on_foot);
}

}  // namespace core
