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

#include <cstdint>

#include "core_common/cargo_form.h"
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

  /// THE TRIP'S RECEIVER (stage В, F3 (A)): where the loaded half of the
  /// round trip goes — the destination the caller measured the way to
  /// (RateToward, the district lot's rate), so the load's row can keep it
  /// beside its seam (`haul_to`) and the groom's plan draws the trip the
  /// seam pays for. RateBetween and RateOverKm leave it unset.
  Vec2 receiver;
  bool has_receiver = false;
};

/// @brief The terms of a trip between two places.
/// @param hours_per_km Game hours per kilometre for this carrier EMPTY —
///        walking or harnessed; the caller picks, because the caller knows
///        whether a horse was spared for this job.
/// @param loaded_hours_per_km Game hours per kilometre LOADED, the half to
///        the receiver (#570, 2a; transport design §11: a loaded cart goes
///        9 km/h against the trot's 12 — transport.csv cart_loaded
///        `speed_kmh`, read by nobody until 0.37.229). A carrier on foot
///        passes his walking pace twice. The round trip is the sum of the
///        two halves, not twice the empty one: every seam priced before was
///        a sixth short on the roads.
/// @param load Grams one trip takes.
/// @return A rate whose round trip is never zero: two places at the same
///         point still cost the loading and unloading of a trip, and a zero
///         would divide the day by nothing.
HaulRate RateBetween(Vec2 from, Vec2 to, float hours_per_km, float loaded_hours_per_km, Grams load);

/// @brief The same terms over a way already measured: `one_way_km` is the
///        effective kilometres of the way (core_common/road_route.h,
///        RoadKm) rather than a straight line (0.36.2).
HaulRate RateOverKm(float one_way_km, float hours_per_km, float loaded_hours_per_km, Grams load);

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
///        cart-days: (carry / cart load) x (the cart's round trip / the
///        walker's) — the cart's round is its empty pace out and its loaded
///        pace back (#570, 2a), the walker's twice his walk.
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
                           float harness_hours_per_km,
                           float loaded_hours_per_km);

/// @brief What a carrier on foot takes in a trip, by the load's form (#671;
///        transport design §2, «Как носят»; core_common/porter_leg.h). The
///        labour table's numbers (labor.csv, STUB econ), in grams.
struct WalkerCarry {
  /// carry_kg_adult: bulk, packed, long boards, and manure carried alone.
  Grams adult = 0;
  /// carry_kg_rick: a bundle of hay or straw on the back.
  Grams rick = 0;
  /// carry_kg_stretcher_pair: manure on a stretcher, for the PAIR.
  Grams stretcher_pair = 0;
};

/// @brief How a carrier on foot holds his load (#671): alone, or one of a
///        pair - on a stretcher (manure) or under a log (logs by two).
enum class WalkerHold : std::uint8_t {
  kAlone = 0,
  kStretcher,
  kLogPair,
};

/// @brief ONE carrier's grams in a full trip of `form` held `hold`, the one
///        rule production prices a walker's seam by, the plan cuts his trips
///        by and the porter's record reports (porter_leg.h, load_share).
/// @param log_piece_grams A log's mass as a piece (resources.csv, the log's
///        grams per piece: 200 kg); read for kLogPair only.
/// @return kRick: carry.rick; kManure with kStretcher: half of
///         carry.stretcher_pair; kLong with kLogPair: half of
///         log_piece_grams; every other form, and manure or a log alone:
///         carry.adult. 0 for kNone, kLive and kPeople - nothing a back
///         carries as a resource's grams (the live are led on a rope).
/// @note A log ALONE is never carried: the caller asks kLogPair or nothing
///       (haul.h above, «A LOG IS NOT CARRIED ON FOOT AT ALL» outside the
///       gate, LogPairMayCarry).
Grams WalkerTripGrams(CargoForm form,
                      WalkerHold hold,
                      const WalkerCarry& carry,
                      Grams log_piece_grams);

/// @brief Whether two walkers may carry a works' logs today - the last
///        resort's three terms (#671; transport design §2, «Брёвна вдвоём —
///        только подвоз на стройку»; boss, 12 October 2026).
/// @param delivering_with_logs_awaiting The works is kDelivering and its
///        ConstructionState::awaiting holds logs - the state that puts
///        «Нет материалов» on its builders. NOT kSiteWithoutMaterials: that
///        alarm lights only when the village lacks the logs altogether.
/// @param draught_horse_free DraughtHorsesFree at the morning's placement.
/// @param walk_m The walk from the log leg's sender to the site, metres
///        (RoadKm by TravelMode::kWalk x 1000).
/// @param max_leg_m labor.csv log_pair_max_leg_m (300, STUB econ).
/// @return true only when the works waits for logs, no horse is free and
///         the walk is at most `max_leg_m`. Never asked of a stand's logs, a
///         dig or the district's lot: the caller is a works' supply alone.
bool LogPairMayCarry(bool delivering_with_logs_awaiting,
                     bool draught_horse_free,
                     float walk_m,
                     float max_leg_m);

}  // namespace core

#endif  // CORE_COMMON_HAUL_H_
