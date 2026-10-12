/// @file
/// @brief WHO CARRIED IT (#671's instrument; transport design §2): the split
///        of every hauled gram between carriers on foot and carts, for the
///        year's book (YearLedger::hauled_on_foot, hauled_by_cart).
/// @threading SINGLE_THREADED
/// Written by the labour hour (NoteHaulDrain, as it drains a load's seam) and
/// by production's settle (TakeFootShare and BookHauled, as it moves the
/// drained grams): both sequential, in row order.
///
/// WHY AT THE SETTLE AND NOT AT THE DRAIN: a seam is days, not grams, and only
/// the settle knows what its days moved - the room a heap may go into, a
/// works' front leg, the evening's conversion. The labour hour notes whose
/// days they were on the load's logistics task (LogisticsTaskRow::
/// drained_on_foot, drained_by_cart); the settle splits the grams it moves by
/// those two and spends what it converted. Every load kind has its task since
/// stage V2 (logistics_state.h), so every hauled gram has a home for its
/// split; a load with none (a hand-built world) books its grams by the
/// settlement's carriers: by cart while it has carts, on foot while not.
/// The split is PRO RATA over the seam's units since the last settle - a
/// walker's unit and a cart's unit are the same unit of the same seam, so the
/// grams follow them; within one settle no finer order is kept.

#ifndef CORE_COMMON_HAULED_BOOK_H_
#define CORE_COMMON_HAULED_BOOK_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"

namespace core {

/// @brief The row of the logistics task whose load `work` hauls, or kNoRow
///        (logistics_rules.h, WorkServesTask; a scan of the tasks).
std::uint32_t TaskRowOfWork(const WorldState& world, const WorkAssignment& work);

/// @brief Notes `drained` seam units taken off the load `work` hauls, by a
///        carrier on foot (`on_foot`) or a cart, on the load's task.
/// @note No task (kNoRow): nothing is noted, and the settle books the grams
///       by the settlement's carriers (TakeFootShare).
void NoteHaulDrain(WorldState& world, const WorkAssignment& work, float drained, bool on_foot);

/// @brief The share of the load's noted units that carriers on foot drained,
///        in [0, 1]. A pure read.
/// @param load A hauling work on the load (HaulingWorkOf's form: its kind
///        and the load's id).
/// @param settlement_has_carts The answer when nothing was noted: 0 while the
///        settlement has carts, 1 while it has none.
float FootShareOf(const WorldState& world, const WorkAssignment& load, bool settlement_has_carts);

/// @brief Spends `fraction` (0..1) of the load's notes: the share of the
///        drained units the settle just turned into grams. A settle that
///        converts all it was drained spends 1 and leaves nought; one the room
///        or the door held back keeps the rest noted, in the same ratio, for
///        the hour that delivers it (field_haul.cpp, DeliverDrained).
void SpendHaulNotes(WorldState& world, const WorkAssignment& load, float fraction);

/// @brief Books `moved` grams of `resource` into the year: `foot_share` of
///        them to hauled_on_foot, the rest to hauled_by_cart.
void BookHauled(YearLedger& book, ResourceId resource, Grams moved, float foot_share);

}  // namespace core

#endif  // CORE_COMMON_HAULED_BOOK_H_
