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
/// THE TEAM'S OATS (C2.2; the human, «Сани по пайку»; econ logistics-all-on
/// §C2.2, 5a9c77c; boss, the #1056 thread [143]; farming design's manure
/// row): manure carting is the TOP rung of the team's oat ladder - seed and
/// plan, the plough's oats, next year's hold, THE WINTER'S HOLD, then manure.
/// The winter's hold looks FORWARD to the first ploughing day: the team's
/// work oats for every winter task but manure on each day left, sized from
/// the closed book's snow-day horse-days less its manure cart-days (year 1:
/// every adult horse at the full work share). A day's manure is priced only
/// as far as the oats the herds may eat today, less that hold, cover its
/// cart-days at a horse-day's work oats. No new holder: the hold is a
/// reading, as the plough's. When the hold stops a priced dose the team's
/// window and the heap's carry the line «Навоз ждёт в куче: овёс бережём
/// упряжке до пахоты» with the two oat numbers (TractionWatch; the string's
/// key `manure_waits_for_oats` is a STUB until rpg writes it).
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
  /// The grams tonight's price carries - all of `due`, or the part the team's
  /// oats above the winter's hold cover (C2.2; save 177); what the day's
  /// carting moves is this by the share drained.
  Grams priced = 0;
  /// Game man-days of carting left today, and what the last settle wrote
  /// (the rick's supply's pair of numbers, unit_state.h RickSupplySeam).
  float days_remaining = 0.0F;
  float days_written = 0.0F;
};

}  // namespace core

#endif  // CORE_COMMON_MANURE_STATE_H_
