// The standing work orders of work_orders.h.

#include "work_orders.h"

#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "labor_day.h"

namespace core {
namespace {

/// @brief Whether the ordered work names a target that exists.
/// The kind decides which target is required — the four field kinds name a
/// field, barn work names a herd (labor_state.h). The boundary already
/// refused the shapeless order; what is checked here is whether the thing
/// named is still there, which only the world can say.
bool TargetExists(const WorldState& world, const OrderRow& order) {
  if (order.work == WorkKind::kHerdCare) {
    return FindRow(world.herds, order.herd) != kNoRow;
  }
  if (order.work == WorkKind::kConstruction) {
    // The site is a UNIT row, not a field (task A2). This used to fall
    // through to the fields table below, so a crew order named its target
    // in one table and had it looked for in another — the seam that decides
    // whether the order stands was reading the wrong book.
    return FindRow(world.units, order.unit) != kNoRow;
  }
  return FindRow(world.fields, order.field) != kNoRow;
}

/// @brief Closes standing orders whose man or whose target is gone.
///
/// MEM-001 fix. A kAssignWork parks at kAccepted and stays there, which is
/// the whole design — but only two rules ever moved it off, and both need a
/// LIVE resident: a newer order for the same man, and kReleaseWork (which
/// refuses for kNoSuchSubject before it ever looks for the standing row).
/// So the day the ordered man died, his order became immortal: the sweep in
/// core_world removes terminal rows only, and this one could no longer
/// reach a terminal status. It was then written into every save and walked
/// by an O(n) scan inside an O(n) loop, on all twenty-four ticks of every
/// day, for the rest of the campaign.
///
/// kRefused and not kCancelled: nobody took the order back. It cannot be
/// carried out because what it names is gone, and `refusal` is the field
/// that says exactly that — the presentation gets kOrderRefused with
/// kNoSuchSubject and can tell the chairman why the man left the job.
void CloseOrphanedWork(WorldState& current) {
  for (OrderRow& order : current.orders.rows) {
    if (order.kind != OrderKind::kAssignWork || order.status != OrderStatus::kAccepted) {
      continue;
    }
    if (FindRow(current.residents, order.resident) != kNoRow && TargetExists(current, order)) {
      continue;
    }
    order.status = OrderStatus::kRefused;
    order.refusal = OrderRefusal::kNoSuchSubject;
  }
}

}  // namespace

std::uint32_t StandingWorkRow(const WorldState& world, ResidentId resident, std::uint32_t self) {
  for (std::uint32_t row = 0; row < world.orders.rows.size(); ++row) {
    if (row == self) {
      continue;
    }
    const OrderRow& other = world.orders.rows[row];
    if (other.kind == OrderKind::kAssignWork && other.status == OrderStatus::kAccepted &&
        other.resident.value == resident.value) {
      return row;
    }
  }
  return kNoRow;
}

bool ReleaseIsInTheBook(const WorldState& world, ResidentId resident) {
  for (const OrderRow& order : world.orders.rows) {
    if (order.kind != OrderKind::kReleaseWork || order.resident.value != resident.value) {
      continue;
    }
    if (order.status != OrderStatus::kRefused && order.status != OrderStatus::kCancelled) {
      return true;
    }
  }
  return false;
}

OrderRefusal CheckAssignWork(const LaborConfig& config,
                             const WorldState& world,
                             const OrderRow& order) {
  const std::uint32_t resident_row = FindRow(world.residents, order.resident);
  if (resident_row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  if (!TargetExists(world, order)) {
    return OrderRefusal::kNoSuchSubject;
  }
  const ResidentRow& resident = world.residents.rows[resident_row];
  if (resident.post.profession.value != kInvalidDefIdValue) {
    // He already has an answer to "what does this man do" (task A7). Two
    // answers are one too many, and silently letting the newer win would
    // empty a post nobody dismissed him from.
    return OrderRefusal::kConflictsWithActive;
  }
  const float age = BiologicalAgeYears(config, resident.birth_day, world.calendar.day);
  if (age < config.adult_age_years) {
    return OrderRefusal::kNotEligible;  // child labour is deferred (life-cycle §7)
  }
  return OrderRefusal::kNone;
}

void ReadWorkOrders(const LaborConfig& config, WorldState& current) {
  for (std::uint32_t row = 0; row < current.orders.rows.size(); ++row) {
    OrderRow& order = current.orders.rows[row];
    if (order.status != OrderStatus::kPending) {
      continue;
    }
    if (order.kind == OrderKind::kAssignWork) {
      const OrderRefusal refusal = CheckAssignWork(config, current, order);
      if (refusal != OrderRefusal::kNone) {
        order.status = OrderStatus::kRefused;
        order.refusal = refusal;
        continue;
      }
      // A newer order for the same man supersedes the older one. CANCELLED
      // and not DONE: the chairman took it back, and kDone would claim work
      // that was never finished.
      const std::uint32_t standing = StandingWorkRow(current, order.resident, row);
      if (standing != kNoRow) {
        current.orders.rows[standing].status = OrderStatus::kCancelled;
        current.orders.rows[standing].refusal = OrderRefusal::kNone;
      }
      order.status = OrderStatus::kAccepted;  // and here it stays, standing
      continue;
    }
    if (order.kind != OrderKind::kReleaseWork) {
      continue;
    }
    if (FindRow(current.residents, order.resident) == kNoRow) {
      order.status = OrderStatus::kRefused;
      order.refusal = OrderRefusal::kNoSuchSubject;
      continue;
    }
    const std::uint32_t standing = StandingWorkRow(current, order.resident, row);
    if (standing == kNoRow) {
      order.status = OrderStatus::kRefused;
      order.refusal = OrderRefusal::kRuleForbids;  // there is nothing to release him from
      continue;
    }
    // The standing order ran as ordered and is stood down: that is kDone.
    current.orders.rows[standing].status = OrderStatus::kDone;
    current.orders.rows[standing].refusal = OrderRefusal::kNone;
    order.status = OrderStatus::kDone;
    order.refusal = OrderRefusal::kNone;
  }
  // LAST, and the order matters. Closing the orphans first would have taken
  // a standing row out from under a kReleaseWork arriving in the same step
  // — the chairman would have been told "there is nothing to release him
  // from" about an order he was looking at. The release settles first and
  // reads kDone; only what nobody could reach is closed here.
  CloseOrphanedWork(current);
}

void ApplyStandingWork(const WorldState& world, WorldState& current, bool day_off) {
  for (const OrderRow& order : world.orders.rows) {
    if (order.kind != OrderKind::kAssignWork || order.status != OrderStatus::kAccepted) {
      continue;
    }
    // THE REST DAY STOPS THE SAME WORK IT STOPS FOR EVERYONE ELSE, and no
    // more. CollectJobs takes field work, hauling and building off the list
    // on a day off and leaves herd care on it, because animals eat on
    // Sunday. A standing order has to obey that same line: a blanket "no
    // orders on a rest day" would drop the ordered cowman back into the
    // accountant's pool every Sunday, while the groom on his post beside
    // him keeps his yard.
    if (day_off && order.work != WorkKind::kHerdCare) {
      continue;
    }
    const std::uint32_t resident_row = FindRow(current.residents, order.resident);
    if (resident_row == kNoRow) {
      continue;  // he died in the night; the sweep will not remove the row, but nobody works it
    }
    WorkAssignment& work = current.residents.rows[resident_row].work;
    work.kind = order.work;
    work.field = order.field;
    work.herd = order.herd;
    work.unit = order.unit;
  }
}

}  // namespace core
