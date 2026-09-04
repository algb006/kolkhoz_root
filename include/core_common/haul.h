/// @file
/// @brief The arithmetic of carrying a load: what one trip takes, how long
/// the trip is, and how much of a load a day of hauling moves.
/// @threading PARALLEL_READONLY
/// Pure functions of their arguments — no state, static or global — so they
/// are safe from any phase and any thread.
///
/// TODAY ONLY core_production CALLS THEM, and the header used to claim
/// otherwise. It sizes the demand it writes into a field's seam and converts
/// the drained seam back into grain; labor drains that seam with real people
/// through the ordinary machinery and never does the arithmetic itself. The
/// file lives in core_common anyway, and on purpose: the moment labor needs
/// to know what a trip costs — to place a crew by what it can actually
/// carry, say — it must get the SAME answer, or the load would move by an
/// amount nobody asked for. A shared rule with one caller is cheap; a rule
/// copied into a second module when the need arrives is not.
///
/// Model: transport design §1 (speeds), §2 (what a person carries), §9 (how
/// goods move between stores); task A4. Decision 155 keeps transport inside
/// the work orders, so nothing here knows about vehicles, routes or queues —
/// a haul is a distance, a load and a pair of legs.

#ifndef CORE_COMMON_HAUL_H_
#define CORE_COMMON_HAUL_H_

#include "core_common/geometry.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One carrier's terms: what he takes in a trip and how long the trip
/// there and back costs him.
struct HaulRate {
  /// The load of a single trip. A cart's 750 kg, or what a person carries.
  Grams load = 0;

  /// There AND BACK, in game hours. An empty return leg is still a leg: it
  /// is what makes a far field cost more than a near one at the same hands,
  /// which is the whole point of the shoulder.
  float round_trip_hours = 0.0F;
};

/// @brief The terms of a trip between two places.
/// @param hours_per_km Game hours per kilometre for this carrier — walking
///        or harnessed; the caller picks, because the caller knows whether a
///        horse was spared for this job.
/// @param load Grams one trip takes.
/// @return A rate whose round trip is never zero: two places at the same
///         point still cost the loading and unloading of a trip, and a zero
///         would divide the day by nothing.
HaulRate RateBetween(Vec2 from, Vec2 to, float hours_per_km, Grams load);

/// @brief Grams one worker moves in `hours` of hauling at this rate.
/// Trips are NOT rounded down to whole ones: half a trip at the end of the
/// day is a load half carried, and the next day finishes it. Rounding here
/// would quantize the whole village's logistics to the length of one trip.
Grams HaulGrams(float hours, const HaulRate& rate);

/// @brief Norm man-days of hauling that `waiting` grams want at this rate.
/// @param standard_day_hours Hours behind one norm man-day (labor.csv).
/// @return 0 when nothing waits or the rate cannot move anything, so a
///         caller may write it straight into a work seam.
float HaulDaysFor(Grams waiting, const HaulRate& rate, float standard_day_hours);

}  // namespace core

#endif  // CORE_COMMON_HAUL_H_
