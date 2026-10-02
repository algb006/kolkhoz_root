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

/// THE CART OF THE COMPRESSED YEAR (0.37.125; transport design §1; the human,
/// 2 October 2026: «Делай K = 2,4»). A cart's load in the core is the table's
/// tonnes TIMES ITS SCALE — transport.csv, row `cart_loaded`, `load_tonnes` x
/// `load_scale` — read by labour and by production from the same two cells.
///
/// WHY A SCALE AT ALL. The game holds three scales and only one had been
/// brought to its year: labour (real man-days / 7 = game days). A man's road
/// to work is twelve times the life's on purpose (real speeds, a clock x12 —
/// time design §6). CARTING WAS x84 AND NOBODY HAD DECIDED IT: the clock's
/// twelve, and a real year's tonnage carted in 48 game days for 336. The
/// world stood because its heaviest load was not carted — the meadows' hay
/// delivered itself; carted (0.37.119), it took 40 % of a village's
/// horse-days and the timber's carts with them.
///
/// WHAT THE SCALE DOES NOT TOUCH: the cart's speed, a walker's carry
/// (labor.csv, carry_kg_adult) and — when carts carry people — a rider's
/// weight. The bounds: below one is not a cart of the compressed year but a
/// typo; the ceiling is far above any K the design names (2.4; econ's ladder
/// of return 3, then 4; 7 is labour's own divisor).
inline constexpr float kCartLoadScaleMin = 1.0F;
inline constexpr float kCartLoadScaleMax = 20.0F;

/// @brief One carrier's terms: what he takes in a trip and how long the trip
/// there and back costs him.
struct HaulRate {
  /// The load of a single trip: a cart's (transport.csv, load_tonnes x
  /// load_scale — 750 kg x 2.4 as shipped), or what a person carries.
  Grams load = 0;

  /// There AND BACK, in game hours. An empty return leg is still a leg: it
  /// is what makes a far field cost more than a near one at the same hands,
  /// which is the whole point of the shoulder.
  float round_trip_hours = 0.0F;

  /// THE PRODUCE CART'S WAY OFF THE ROAD (0.36.9): true when the load goes
  /// by a harnessed cart with produce (TravelMode::kCart) — the one carrier
  /// the design keeps to the roads (roads design §11) — and then the metres
  /// of ONE loaded way that lie off the network, BY END since 0.36.15: from
  /// the load to the road (driving the field, question 268) and from the road
  /// to the store (the store's gate, unit rules §12). Filled by the caller
  /// that measured the way (field_haul); RateBetween and RateOverKm leave
  /// them false and nought. Read only by the year's book
  /// (YearLedger::cart_trips and its siblings).
  bool produce_cart = false;
  float off_road_load_m = 0.0F;
  float off_road_store_m = 0.0F;
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

/// @brief The same terms over a way already measured: `one_way_km` is the
///        effective kilometres of the way (core_common/road_route.h,
///        RoadKm) rather than a straight line (0.36.2).
HaulRate RateOverKm(float one_way_km, float hours_per_km, Grams load);

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

/// @brief The part of a CART's norm-day of hauling that a carrier ON FOOT
///        does in a norm-day of his own, on a load whose seam was written in
///        cart-days: (carry / cart load) x (walking pace / harness pace).
///
/// WHY (0.37.105; manual/75-logistics.md §9, the finding of 2 October 2026):
/// production writes a load's seam ONCE, at the cart's rate whenever the
/// settlement owns a draught horse, and labour drains it by every carter's
/// norm-days — the one on a horse and the one the placement left on foot
/// alike. A walker wrote off a cart's work: on the canon's nine villages,
/// years 1 to 10, the carters with no horse did 47 % of the arable's hauling
/// norm-days, 64 % of the stands' and 32 % of the pits', and a log of 200 kg
/// «rode» on a back at 440-460 kg a man a day. A man carries carry_kg_adult.
///
/// PRICED ON THE CART'S WAY, AND SAID SO: the two are taken over one way, the
/// cart's. A walker's own way is the shorter of the two (he crosses open
/// ground, the produce cart keeps to the roads), so this never gives him more
/// than he carries and may give him less, by the ways' ratio — up to 2.3 on
/// the start layout of 0.37.99. A STUB of exactness, not of direction.
///
/// WHEN IT APPLIES: only while the seam is in cart-days, which is while
/// SettlementHasCarts says so — the one predicate production rates by and
/// labour drains by. With no horse in the settlement the seam is written at
/// the walker's own rate, and his norm-day is a whole one.
///
/// A LOG IS NOT CARRIED ON FOOT AT ALL: the carting of a stand's logs stops
/// for want of a horse, as the district's lot does (assignment.h,
/// StopsWithoutHorse). A settlement that lost every horse moves no log; the
/// livestock window is its way out.
/// @param carry_grams What a grown carrier takes in a trip (labor.csv,
///        carry_kg_adult).
/// @param cart_grams A cart's load (transport.csv).
/// @param walk_hours_per_km, harness_hours_per_km Game hours a kilometre, on
///        foot and in harness.
/// @return A share in (0, 1]; 1 when any argument is not above nought, so a
///         table with a number missing changes nothing rather than stopping
///         every cart.
float WalkerShareOfCartDay(Grams carry_grams,
                           Grams cart_grams,
                           float walk_hours_per_km,
                           float harness_hours_per_km);

}  // namespace core

#endif  // CORE_COMMON_HAUL_H_
