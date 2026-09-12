// The standing work orders of work_orders.h.

#include "work_orders.h"

#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "labor_day.h"
#include "posts.h"

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
    const std::uint32_t resident_row = FindRow(current.residents, order.resident);
    if (resident_row == kNoRow || !TargetExists(current, order)) {
      order.status = OrderStatus::kRefused;
      order.refusal = OrderRefusal::kNoSuchSubject;
      continue;
    }
    // AND THE ONE ANSWER IS CHECKED HERE, EVERY TICK, not only at admission.
    //
    // A work order is let past a held post when the same batch carries the
    // kDismiss that ends it — but a dismissal does not settle in the tick it
    // is read: it waits at kAccepted until the day's close, and the
    // presentation may CANCEL it in between (ISession::CancelOrder takes
    // kAccepted rows). Cancel it, and the man kept his post while his work
    // order kept standing: two answers to "what does this man do", for the
    // rest of the campaign, with ApplyStandingWork quietly overwriting the
    // post placement every morning — the very state the conflict rules
    // exist to prevent, reached by the one route that goes round them.
    //
    // The post wins, and not by preference: the work order was granted on a
    // dismissal that did not happen, so it is the one standing on nothing.
    // The refusal says which (task A8 delivery cycle).
    // "A dismissal is on the way" is the whole exception, and it has to be
    // re-asked every tick rather than trusted once: the post is still on the
    // row until the day's close, so a check without this would refuse the
    // very order the same batch legitimately granted. Cancel the dismissal
    // and this becomes false — which is exactly when the order must go.
    if (current.residents.rows[resident_row].post.profession.value != kInvalidDefIdValue &&
        !DismissalIsInTheBook(current, order.resident)) {
      order.status = OrderStatus::kRefused;
      order.refusal = OrderRefusal::kConflictsWithActive;
    }
  }
}

/// @brief Can this kind of land EVER carry this kind of work?
///
/// NOT "is there work here today" — that question is asked by KindOfPhase
/// (core_common/work_seam.h) and answered fresh every morning, because a
/// field's phase comes round: the field being ploughed today is harvested in
/// the autumn, and an order that finds no work this morning is not wrong,
/// it is early. THE PHASE HOLDS NOTHING PERMANENT, so nothing permanent can
/// be read out of it, and the core is right to stay silent there.
///
/// The land KIND is the other axis, and it does hold "never": a meadow is
/// mown where it grew and is never ploughed, harrowed or sown — the
/// production day sends it down a branch of its own that opens no such
/// phase. An order of that shape is accepted today and then silently does
/// nothing FOR EVER.
///
/// IT NAMED UNRAISED LAND TOO until 2026-09-12, when LandKind::kDerelict was
/// removed: the design says an overgrown field is a look and not a state,
/// and that raising it is ploughing it at the same norm as any other ground.
///
/// AND THAT LEAVES ONE CASE UNANSWERED, NAMED HERE RATHER THAN PATCHED. Work
/// is opened off the rotation, so a field whose three slots are all empty
/// never has any — and an order sent there now stands for ever with its man
/// beside it, where the old kind would have refused it. Two things have to
/// arrive before that can be closed honestly. The player must be able to SET
/// a rotation at all (OrderKind::kSetRotation has no consumer yet), because
/// until then the refusal would name a condition he cannot change; and it
/// wants a reason of its own rather than kWrongLand, whose whole point is
/// the word NEVER — a field waiting to be told what to grow is not that.
/// A first draft refused it here as kWrongLand and reddened four assertions
/// about how orders legitimately stand: the rule was wider than the case.
///
/// @return kWrongLand, or kNone for work that does not name a field at all.
OrderRefusal LandCarriesWork(const WorldState& world, const OrderRow& order) {
  switch (order.work) {
    case WorkKind::kHerdCare:
    case WorkKind::kConstruction:
    case WorkKind::kNone:
      return OrderRefusal::kNone;  // no field named; nothing to ask about
    case WorkKind::kPlowing:
    case WorkKind::kHarrowing:
    case WorkKind::kSowing:
    case WorkKind::kHarvest:
    // HAULING NAMES A FIELD TOO, and the first draft of this rule said it did
    // not — leaving the one work kind whose land it declined to look at with
    // exactly the defect the rule exists to remove (UB-001, 2026-09-07).
    // TargetExists eight lines up sends everything but herd care and
    // construction to world.fields, and WorkSeamOf drains
    // FieldRow::haul_days_remaining, which only opens while reaped_grams > 0.
    // A meadow never sets it — its hay goes straight through the store door
    // and the overflow is booked to the year's loss, so nothing is ever left
    // lying there to carry (field_work.cpp, DeliverHarvest). "Carry from the
    // meadow" is not empty today; it is empty for ever.
    case WorkKind::kHauling:
      break;
    // Not a kind, and it names no land: handled beside the kinds that name
    // none, so this switch keeps no default and a NEW work kind stays a
    // compile error — the only way this question gets asked about it at all.
    //
    // WRITTEN OUT WITH ITS SCOPE, and that is not style. labor_state.h holds
    // BOTH `WorkKind::kWorkKindCount` and a free `kWorkKindCount` beside it,
    // and a bare name here binds to the free one — a std::uint32_t, which
    // compiles as a comparison against nothing and leaves the enumerator
    // unhandled. The compiler said so twice, in two different ways.
    case WorkKind::kWorkKindCount:
      return OrderRefusal::kNone;
  }
  const std::uint32_t field_row = FindRow(world.fields, order.field);
  if (field_row == kNoRow) {
    return OrderRefusal::kNone;  // TargetExists has the say on a missing field
  }
  // AN OVERGROWN FIELD IS REFUSED NOTHING on account of its look:
  // LandKind::kDerelict went on 2026-09-12 and FieldRow::overgrown carries
  // the look and no rule. The one case that IS still unanswered — ground
  // with no rotation, where work never opens — is named in the @brief above
  // and deliberately not refused here; a draft that refused it reddened four
  // assertions about how orders legitimately stand.
  const LandKind kind = world.fields.rows[field_row].kind;
  if (kind != LandKind::kArable && order.work != WorkKind::kHarvest) {
    // A meadow's "harvest" is the mowing, and that is real work. Ploughing,
    // harrowing and sowing on one are not late — they are impossible.
    return OrderRefusal::kWrongLand;
  }
  return OrderRefusal::kNone;
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

bool WorkOrderCameFirst(const WorldState& world, ResidentId resident, std::uint32_t before_row) {
  const auto rows = static_cast<std::uint32_t>(world.orders.rows.size());
  const std::uint32_t limit = before_row < rows ? before_row : rows;
  for (std::uint32_t row = 0; row < limit; ++row) {
    const OrderRow& order = world.orders.rows[row];
    if (order.kind == OrderKind::kAssignWork && order.status == OrderStatus::kPending &&
        order.resident.value == resident.value) {
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
  // WHAT A MAN HAS ALREADY PROMISED HAS ONE HOME, AND IT IS THE BOOK
  // (boss, 2026-09-04). Both halves of the post/order conflict are read from
  // there, and for the same reason: between accepting a post order and
  // applying it at the day's close the resident row still says yesterday,
  // and anyone who asks it in that window gets the right answer to the wrong
  // question. That window is exactly where this check used to let a work
  // order through — and the next morning's re-check then killed the
  // appointment, so the tie-break came out the reverse of the rule.
  //
  // The rule is: WHICHEVER ARRIVES SECOND IS REFUSED. Two answers to "what
  // does this man do" are one too many, and letting the newer win silently
  // would empty a post nobody dismissed him from.
  if (resident.post.profession.value != kInvalidDefIdValue &&
      !DismissalIsInTheBook(world, order.resident)) {
    return OrderRefusal::kConflictsWithActive;
  }
  if (AppointmentIsWaiting(world, order.resident)) {
    // A post granted this morning and not yet applied. It got here first.
    return OrderRefusal::kConflictsWithActive;
  }
  const float age = BiologicalAgeYears(config, resident.birth_day, world.calendar.day);
  if (age < config.adult_age_years) {
    return OrderRefusal::kNotEligible;  // child labour is deferred (life-cycle §7)
  }
  return LandCarriesWork(world, order);
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
