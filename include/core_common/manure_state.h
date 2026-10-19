/// @file
/// @brief Manure carted: the yard -> heap removal and the heap -> field dose,
///        the state half (#1056 piece C2; econ's stage C contract
///        logistics-stage-c-contract-2026-10-18.md «Piece C2»; boss's four
///        answers, the #1056 thread after [20]).
/// @threading SINGLE_THREADED
/// Written by production's decisions sub-step (phase 3): the price at the
/// day's last tick, the settle every hour - as the milk leg and the rick's
/// supply (milk_cart.h, rick_supply.h). Drained by the labour hour through
/// the work seam (work_seam.h); read by logistics for its tasks.
///
/// WHAT MOVES WHERE, under farming.csv `manure_carted` (STUB 0):
///   - a kolkhoz herd's manure lies in its YARD's stock (the herd's unit), not
///     in the heap as before C2. The yard holds livestock.csv
///     `manure_yard_buffer_days` (STUB 6) of its own herds' manure; what does
///     not fit is lost (YearLedger::manure_lost);
///   - yard -> heap: a removal task (logistics_state.h, kManureOut, by the
///     yard), level 3, level 2 from two thirds full; on stretchers where the
///     walk holds the day's trips (leg_loads.cpp), else by cart;
///   - heap -> field: PlanManure BOOKS the year's doses as it chose them
///     (poorest field first, farming §9 closed) and moves no grams; each booked
///     field is a task (kManureField, by the field), level 3, LEVEL 0 FROM THE
///     FIRST DAY OF ITS PLOUGH SLOT'S MONTH;
///   - a field whose ploughing opens (its phase leaves kIdle) with part of its
///     dose not delivered: that part is CANCELLED - it stays in the heap, the
///     booking is undone, and it is counted late (YearLedger::manure_late, gate
///     C2-b); the furrow pays the bonus by what arrived (FieldRow::
///     manure_applied), once, as before.
///
/// STUB (C2; econ's stage C contract §C2.1, 5f7f1a6; boss, the #1056 thread
/// after [20]; transport design §12, stage C's row): MANURE MOVES AS ON MAIN -
/// the yard's manure into the heap with no haul, PlanManure spreading the
/// year's doses with no haul. Re-priced in game hours the carting costs ≈ 1 700
/// winter cart-hours a village-year on the reference layout, unfed, and every
/// gate was predicted red, so NOTHING HERE IS DEFINED, CALLED, ADDED TO A ROW
/// OR SAVED: this file holds the rule and the seams for the day it returns.
/// RETURNS WHEN any one of:
///   1. the reference layout puts the heaps within ~0.8 km road of the fields
///      they feed (≤ ~2.5 game hours a trip), AND a run prints the village's
///      free winter horse-hours after logs, the team fed by the ladder, at
///      ≥ 1.5 × the manure cart-hours - both priced before the build;
///   2. the winter's own pace enters the core (sledges, road_rules.h's STUB) at
///      a measured pace and load - and this is re-priced in game hours first;
///   3. Epoch II's lorries or tractor-drawn spreaders, where horse-hours stop
///      being the limit.

#ifndef CORE_COMMON_MANURE_STATE_H_
#define CORE_COMMON_MANURE_STATE_H_

#include <cstdint>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief A YARD'S REMOVAL TO THE HEAP (kManureOut): on the yard's UnitRow.
///        All nought and `to` invalid when the yard holds nothing to remove.
struct ManureOutSeam {
  /// The heap the manure goes to (the `manure_pile` unit).
  UnitId to;
  /// Game man-days of carrying left today, and what the last settle wrote.
  float days_remaining = 0.0F;
  float days_written = 0.0F;
  /// The manure the evening priced to remove, grams; less what was carried.
  Grams grams = 0;
  /// 1 when priced on foot (stretchers; the walk holds labor.csv
  /// `walker_min_trips_per_day`), 0 by cart.
  std::uint8_t on_foot = 0;
};

/// @brief A FIELD'S DOSE FROM THE HEAP (kManureField): on the FieldRow.
///        All nought and `from` invalid when nothing is booked.
struct ManureDoseSeam {
  /// The heap the dose is carted from.
  UnitId from;
  /// The dose PlanManure booked for this field, grams, and what is still to
  /// cart of it; delivered = booked - due.
  Grams booked = 0;
  Grams due = 0;
  /// Game man-days of carting left today, and what the last settle wrote.
  float days_remaining = 0.0F;
  float days_written = 0.0F;
  /// The month (0-11) its plough slot opens in this year - the task's level
  /// 0 from its first day (boss's answer (3)); written with the booking.
  std::uint8_t due_month = 0;
};

}  // namespace core

#endif  // CORE_COMMON_MANURE_STATE_H_
