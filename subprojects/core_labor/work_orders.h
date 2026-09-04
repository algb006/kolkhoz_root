/// @file
/// @brief The chairman's standing work orders: kAssignWork and kReleaseWork.
/// @threading SINGLE_THREADED
/// Read and applied in the labor sub-step of the decisions slot (phase 3),
/// on the sim thread, like everything else this module does.
///
/// WHAT A STANDING ORDER IS, AND WHY IT NEEDS NO FIELD OF ITS OWN. The
/// chairman puts a man on a job and the man stays there — "the order holds
/// until kDone or a kReleaseWork, not for one day" (order_state.h, boss's
/// decision of 2026-08-31 out of delegation design §7, management by
/// exception). That is a fact which must survive the day, the save and the
/// determinism check, and the core already has exactly one home for such
/// facts: THE ORDER BOOK ITSELF.
///
/// So the row stays at kAccepted for as long as the order stands. It is
/// visible to the presentation ("status: accepted"), it is cancellable, it
/// is saved with the world, and it says who, what and where in the fields it
/// already has. A `standing_work` block on ResidentRow would have been a
/// second copy of a row that exists — and a second copy is the defect this
/// project has caught four times in two days.
///
/// The price is a walk over the book each morning. The book holds a handful
/// of rows: the sweep in core_world removes every terminal row in the step
/// that settled it, so what remains is what still stands.

#ifndef CORE_LABOR_WORK_ORDERS_H_
#define CORE_LABOR_WORK_ORDERS_H_

#include <cstdint>

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "labor_config.h"

namespace core {

/// @brief Validates a kAssignWork against the world as it stands.
/// @return kNone when the order may stand; otherwise the refusal to show.
/// @note A resident who holds a POST is refused (kConflictsWithActive): a
///       post is already an answer to "what does this man do", and two
///       answers are one too many (manual/74-posts.md §1).
OrderRefusal CheckAssignWork(const LaborConfig& config,
                             const WorldState& world,
                             const OrderRow& order);

/// @brief The row index of the standing kAssignWork for `resident`, or
/// kNoRow. A standing order is one at kAccepted: accepted and not yet ended.
/// @param self An order row to ignore — the one being validated.
std::uint32_t StandingWorkRow(const WorldState& world, ResidentId resident, std::uint32_t self);

/// @brief Whether the book holds a kReleaseWork for `resident` that has not
/// been refused — that is, whether the chairman is ending his standing
/// order in this very batch.
///
/// It exists because the two work verbs are read AFTER the two post verbs
/// in the tick, so "release him, then appoint him" — one gesture in the
/// office, two rows in one batch — reaches CheckAppointment while the
/// standing order is still standing. Without this the appointment would be
/// refused against an order that is settled kDone a few statements later,
/// and the man would end the step with neither post nor work.
bool ReleaseIsInTheBook(const WorldState& world, ResidentId resident);

/// @brief Reads the book's two work verbs and moves them.
///
/// kAssignWork goes to kAccepted and STAYS there — that is the standing
/// order. A second one for the same man SUPERSEDES the first, which is
/// cancelled rather than completed: the chairman took it back with his own
/// newer order, and kDone would claim work that never happened.
///
/// kReleaseWork ends the standing order (kDone — it ran as ordered and was
/// stood down) and completes itself.
///
/// @pre Called at the END of the tick, after the day has been placed and
///      closed: an order read today takes effect from the NEXT working day
///      (time design §11), and reading last is what makes that true without
///      a second flag to remember it by.
void ReadWorkOrders(const LaborConfig& config, WorldState& current);

/// @brief Overrides this morning's placement with the standing orders.
/// @pre Called after the accountant has placed the day, from StartDay.
/// @note A standing order whose target has no work today leaves its man
///       IDLE rather than returning him to the pool. That is what "outranks
///       the morning placement" means, and it is the chairman's to notice —
///       management by exception cuts both ways.
/// @param day_off True on a rest day, when only herd care is worked — the
///        same line CollectJobs draws, and drawn here from the same rule
///        rather than from an empty job list (task A8 delivery cycle).
void ApplyStandingWork(const WorldState& world, WorldState& current, bool day_off);

}  // namespace core

#endif  // CORE_LABOR_WORK_ORDERS_H_
