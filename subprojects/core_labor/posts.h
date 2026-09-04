/// @file
/// @brief The post rules of core_labor: who may be appointed to what, where
/// a post exists at all, and what refuses an appointment.
/// @threading SINGLE_THREADED
/// Pure readings of the world, called from the labor sub-step of the
/// decisions slot (phase 3) on the sim thread — the same slot and the same
/// thread as the rest of the module. Nothing here writes, so nothing here
/// needs synchronizing; it is single-threaded because its only caller is.
//
// A post is a PLACE a resident holds until he is dismissed, not the work he
// is given each morning (manual/74-posts.md §1). It lives on his own row
// (ResidentRow::post) and survives the day, the save and everything except
// his death; two orders of the book, kAppoint and kDismiss, are the only
// things that move it, and both are applied at the day's close because a
// man's post cannot change while he is working it (time design §11).
//
// This file holds the RULES; labor_system.cpp holds the day they happen on.
// Nothing here writes an order's status or emits an event: the caller does
// that, so that the whole life of an order is visible in one place.

#ifndef CORE_LABOR_POSTS_H_
#define CORE_LABOR_POSTS_H_

#include <cstdint>

#include "core_common/order_state.h"
#include "core_common/world_state.h"
#include "labor_config.h"

namespace core {

/// @brief The staff line of `profession` at a unit of `type` standing at
/// `level`, or nullptr when that unit carries no such post.
/// @param level The unit's ladder step. A level-0 row is a marked site and
///             carries nothing, whatever the table says.
const StaffSlot* FindStaffSlot(const LaborConfig& config,
                               UnitTypeId type,
                               std::uint8_t level,
                               ProfessionId profession);

/// @brief Whether `resident` may hold `profession` at all — the age band,
/// the sex and the education threshold of professions.csv, every one of
/// them a column (manual/74-posts.md §6).
/// @param age Biological years, as the rest of the subsystem computes them.
bool IsEligible(const LaborConfig& config,
                const ResidentRow& resident,
                ProfessionId profession,
                float age);

/// @brief Validates a kAppoint against the world as it stands.
/// @return kNone when the appointment may go ahead; otherwise the refusal
///         the presentation is to show (manual/74-posts.md §3).
/// @note Called twice for every order: once in the step it arrives, to
///       answer the chairman, and once at the day's close, because a day is
///       long enough for the unit to be demolished or the last free slot to
///       be taken by another order accepted the same morning.
OrderRefusal CheckAppointment(const LaborConfig& config,
                              const WorldState& world,
                              const OrderRow& order);

/// @brief Validates a kDismiss: there is such a resident, and he holds a
/// post to be removed from.
OrderRefusal CheckDismissal(const WorldState& world, const OrderRow& order);

/// @brief Whether another order for the same resident is already waiting at
/// kAccepted — one man, one post, and a queue of two nobody has seen the
/// order of is not resolvable (kConflictsWithActive).
/// @param self The row index of the order being validated, which is not
///             itself a conflict.
bool HasWaitingPostOrder(const WorldState& world, ResidentId resident, std::uint32_t self);

/// @brief Whether a post for `resident` is standing at kAccepted — granted
/// and not yet applied, because a post takes effect at the day's close.
///
/// Asked by CheckAssignWork, and this is the half of the post/order conflict
/// that used to be missing. That check read the APPLIED post on the resident
/// row, so between accepting an appointment and applying it there was a
/// window in which a work order sailed through — and the next morning's
/// re-check killed the appointment instead. The rule says the second one
/// yields; reading applied state answered the right question about the wrong
/// moment (boss, 2026-09-04: what a man has already promised has one home,
/// and that home is the book).
bool AppointmentIsWaiting(const WorldState& world, ResidentId resident);

/// @brief Whether a dismissal for `resident` is in the book and has not been
/// refused or cancelled — that is, whether his post is being ended in this
/// very batch.
///
/// The mirror of ReleaseIsInTheBook (work_orders.h): "dismiss him, then put
/// him to work" is one gesture in the office and two rows in one book, and
/// the post verbs are read BEFORE the work verbs in the tick — so without
/// this the work order would be refused against a post that is already on
/// its way out.
bool DismissalIsInTheBook(const WorldState& world, ResidentId resident);

}  // namespace core

#endif  // CORE_LABOR_POSTS_H_
