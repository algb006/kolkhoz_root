/// @file
/// @brief Manure carted heap -> field on sledges: the state half (#1056 piece
///        C2; econ logistics-all-on-2026-10-19.md «C2 — manure: yard → field
///        on sledges, the near poor fields first»; boss, the #1056 thread
///        [66] item 5; farming design «Навоз — важная петля»).
/// @threading SINGLE_THREADED
/// Written by production's decisions sub-step (phase 3): the booking at the
/// year's turn, the price at the day's last tick, the settle every hour - as
/// the rick's supply (rick_supply.h). Drained by the labour hour through the
/// work seam (work_seam.h); read by logistics for its task (kManureField).
///
/// WHAT MOVES WHERE:
///   - a kolkhoz herd's manure goes into the heap, as before C2
///     (herd_system.cpp): the heap stands «на отшибе» by the yard (the
///     reference layout's suggest_manure_pile, 90 m off), so there is no
///     yard -> heap leg. The old contract's yard buffer and its removal task
///     are withdrawn with the old form (econ: «the tonne-km is conserved»);
///   - from the first snow after the harvest (November-December too; boss,
///     the #1056 thread [140]) the doses are BOOKED, not spread: the fields
///     below the fertility mark, NEAREST BY ROAD FROM THE HEAP FIRST, each up
///     to its full norm dose (farming §9's dose unchanged), while the heap's
///     unbooked manure lasts - every sledge day, so the winter's own manure is
///     booked too. A booking stands until its field's ploughing. No gram moves
///     at the booking;
///   - a booked dose is carted heap -> field ON SLEDGE DAYS ONLY (snow lies,
///     cart_gear.h SledgeDay): level 3, its deadline the field's ploughing;
///   - a field whose ploughing opens (its phase leaves kIdle) with part of its
///     dose not delivered: that part is CANCELLED - it stays in the heap, the
///     booking is undone, and YearLedger::manure_late counts it; the furrow
///     pays the bonus by what arrived (FieldRow::manure_applied), once;
///   - what the winter did not carry stays in the heap for the next winter:
///     not lost and not spread free.
///
/// THE MARK (farming.csv `manure_fertility_mark`, STUB 70 until the balance
/// pass): every start field (65) sits below it and a field dunged once (+10)
/// leaves the queue until it falls back, so the turn passes outward. While
/// any field below it is short of its dose, fields at or above it get none;
/// equal road -> poorest first (boss, the #1056 thread [134], [136]; farming
/// design's manure row).

#ifndef CORE_COMMON_MANURE_STATE_H_
#define CORE_COMMON_MANURE_STATE_H_

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief A FIELD'S DOSE FROM THE HEAP (LogisticsLoadKind::kManureField): on
///        the FieldRow. All nought and `from` invalid when nothing is booked.
///        Saved (save 176).
struct ManureDoseSeam {
  /// The heap the dose is carted from (the `manure_pile` unit).
  UnitId from;
  /// The dose booked at the year's turn, grams, and what is still to cart of
  /// it; delivered = booked - due.
  Grams booked = 0;
  Grams due = 0;
  /// Game man-days of carting left today, and what the last settle wrote
  /// (the rick's supply's pair of numbers, unit_state.h RickSupplySeam).
  float days_remaining = 0.0F;
  float days_written = 0.0F;
};

}  // namespace core

#endif  // CORE_COMMON_MANURE_STATE_H_
