/// @file
/// @brief WHOLE TRIPS AND THE ROUTES (routing stage В4), built and NOT
///        SHIPPED: the switch stands at 0, the fluid (below). A cart's trip
///        whole - it leaves with what lies, up to its load, and comes back -
///        and one sender's loads for several receivers on one cart where their
///        forms mix (transport §11-§12). Private to core_production.
///
/// THE FLUID IS A STATED STUB (boss, core-boss-v4-scope-2026-10-14 [8], by
/// econ's gate set before the run): «carting priced by the gram; a part-load
/// costs its fraction». The arms on nine villages, four years: W cost +12.0 %
/// of the carters' hours (median +12.3 %); WR +5.1 % MORE than W - the tours
/// gave back 0.4 %, the mix rule cost +5 %; gate 3 («WR gives back at least
/// half of W») failed, and W/WR showed 3 underfed kolkhoz herd-evenings
/// against F's 0. RETURN CONDITION: U above 1.10 on a later base AND a
/// consolidation that gives back at least half (pooling two fields' leftovers
/// is the candidate - the field heaps held 52 of 82 thousand fluid
/// cart-hours, and §12's one-sender rule pools none of them), or Epoch II's
/// lorries. The tours and the mix stay here behind the switch, unused.
///
/// WHY. Until this stage the core priced every carting as a FLUID: a seam's
/// cart-days were grams ÷ (cart load × trips a day) (core_common/haul.cpp,
/// HaulDaysFor) and the book counted fractional trips (haul_ways.cpp,
/// BookCartRun) - 200 kg paid 200/1800 of a round trip, as if every cart left
/// full. Transport §12 builds routes so a cart «не гоняет порожняком», and
/// that promise meant nothing while no cart could run half empty. The print
/// that decided it (core-boss-v4-scope-2026-10-14 [4]; econ's rule,
/// econ-boss-v4-whole-trips-lines-2026-10-14 [1]): U = the four years'
/// cart-hours with every leg-day rounded up to whole trips ÷ the fluid's =
/// 1.252 on the nine villages' sum, 1.255 the villages' median (rule: above
/// 1.10 on either → whole trips, built TOGETHER with the routes). By source:
/// the district lot +57 %, the works' supply +39 %, extraction sites +33 %,
/// field heaps +13 %. A LOWER BOUND: logs off the stands are outside the
/// cart book (below, item 5) - U is printed again with them before W is read.
///
/// ONE SWITCH, THREE ARMS (a table cell, not a build - transport.csv, column
/// `trip_model` on the `cart_loaded` row; boss's base; absent reads 0): 0 F -
/// the fluid, as today; 1 W - whole trips, no consolidation; 2 WR - whole
/// trips and the routes. Shipped at 0.
///
/// 1. THE PRICE OF A LOAD (every carting: field heaps, extraction sites, stores
///    emptied, the district lot, the works' supply, the stands' logs, the
///    field's straw): CartDaysFor - the one door. F: HaulDaysFor. W and WR:
///    whole trips - ceil(grams ÷ load) round trips at the rate's hours, the
///    empty return included (the round trip already holds it). PRICED ON THE
///    SEAM's WHOLE LOAD when the seam is written (the evening's settlement, the
///    laying of a heap, the fixing of a works' materials), so a heap of 30 t
///    pays 17 trips and not 17 trips and a fraction each day. A carter's
///    drained hours still land grams by the share of the seam - the price
///    changes, not the conversion.
/// 2. ONE SENDER, SEVERAL RECEIVERS (WR): the works whose FRONT supply leg
///    starts at the same sender (SupplyLeg::from, the same store, stand or pit)
///    are planned into TOURS (PlanSupplyTours): greedy - the nearest receiver
///    by road first, then the nearest to it, while the tour's grams fit the
///    cart's load and every load MIXES with the tour's first by its form
///    (core_common/cargo_form.h, CargoFormsMix: tare with tare and food not
///    with fuel, bulk one resource, a «воз» one resource, the long load only
///    with itself). Each works' seam is priced at ITS SHARE of its tours'
///    whole trips, by grams (StopShareOfTour). A load that mixes with no other
///    is a tour of its own. THE DISTRICT LOT likewise: the lots standing at the
///    district centre on one day for the village's own carts are one sender
///    and - the body found - ONE RECEIVER, the log pile (an own-carts lot is a
///    timber lot): no tour to plan, a POOL - each lot pays its share of the
///    pool's whole trips (field_haul.cpp, SettleDistrictLotHauling). The
///    contract's PlanDistrictTours was dropped with the body for that reason.
///    A NAMED SIMPLIFICATION (boss, core-boss-v4-scope-2026-10-14 [6]): a
///    tour changes the PRICE of each works' seam, NOT WHO CARTS IT - labour
///    still places a carter per works, and the one cart driving the tour is
///    the layer's picture. Do not read the labour placement as the route.
///    The saving is real (shared trips priced once); the route is not a job.
/// 3. THE ORDER OF STOPS IS GREEDY - THE OPTIMUM BY DELIVERY TIME IS A STUB
///    (transport §12 «Выбор варианта»). Its return condition, written here:
///    the pair's probe prints each tour's km by the greedy order against the
///    best order of its own stops (every order, tours of up to five stops); if
///    the greedy order costs more than 5 % of WR's four-year supply and
///    district cart-hours on the nine villages' sum, the optimum is built.
/// 4. THE FIELD's STRAW GETS ITS CART (transport §9's line «пока этап В4 её не
///    даст»): the straw cut with a field's grain lies AT THE FIELD
///    (FieldRow::straw_grams, new, saved) and is carted to the rick the door
///    chooses (feed_home.h, RickForLoadAt) by the same field's carting, after
///    its grain - a «воз» does not mix with bulk grain, so its trips are its
///    own. Until now it landed in the rick by no road the hour it was cut.
/// 5. THE STANDS' LOGS ENTER THE CART BOOK (the hole named in [4]): a new
///    CartLoadSource::kStand, so U and W's price see them; until now
///    BookCartRun was called for them with no source and returned.
///
/// THE SAVE (VERSION_SAVE 162, predicted before the body):
///   * FieldRow::straw_grams, Grams (int64): +8 bytes a field row.
///   * CartLoadSource gains kStand: each of YearLedger's eight arrays by source
///     (cart_trips, cart_off_road_m, cart_off_road_worst_m, cart_trips_off_road,
///     cart_store_off_road_m, cart_store_off_road_worst_m - floats, 4 bytes;
///     cart_grams, cart_grams_off_road - Grams, 8 bytes) grows by one entry:
///     6 × 4 + 2 × 8 = +40 bytes a YearLedger, times the ledgers a save holds.
///   * NOTHING ELSE: the tours are planned afresh every evening from the saved
///     legs and lots; the switch is a table cell; the receivers of a tour are
///     the works' own rows.
///
/// WHAT THE PAIR IS READ BY (econ's lines, set before the run; nine villages,
/// four years, the canon's flags, one run each; host's 27 seeds for the
/// delivery). F | W | WR:
///   1. SAFETY, W and WR against F: kolkhoz herd-evenings underfed stay 0;
///      hunger deaths of herds and people not above F's; the autumn's snow loss
///      not above F's by more than 10 % of F's own; the far-rick days and the
///      urgent ceilings as on 0.37.255.
///   2. THE PRICE, printed, not gated: carting hours and cart-days, carter-hours,
///      houses at year 4 and construction over four years (sum, villages'
///      median, range, count below -10 %), sown ha. W costing houses at year 4
///      more than 5 % on the median is a line to boss (the balance tuned on the
///      fluid is re-read), not a fail.
///   3. THE ROUTES EARN THEIR PLACE, WR against W: WR gives back at least HALF
///      of what W took in cart-days over four years, sum and median; less, and
///      only item 2's multi-drop is kept.
///   4. NOTHING NEW TO THE PLAYER: no order, no number in a window but one line
///      the layer shows - trips and the share of empty kilometres - which the
///      core supplies (YearLedger: the empty-km share by source, from the
///      tours); the switch is not delivered without it (a line for ue).
///   And printed with W and WR: U again with the stands' logs in the book; per
///   village-year the district runs by the village's own carts, their load
///   share and the points for own delivery they earned (district §4) - a run
///   under half a cart that still earns points is a pump, named.
///
/// @threading SINGLE_THREADED
/// Production's evening settlement and hourly deliveries (phase 4, the
/// carting's own slot), as every seam's pricing today.

#ifndef CORE_PRODUCTION_CART_TRIPS_H_
#define CORE_PRODUCTION_CART_TRIPS_H_

#include <cstdint>
#include <vector>

#include "core_common/cargo_form.h"
#include "core_common/haul.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief The cart trip's model, transport.csv `trip_model` on the
///        `cart_loaded` row (ProductionConfig::cart_trip_model).
enum class CartTripModel : std::uint8_t {
  kFluid = 0,        ///< F: grams ÷ load × round trip - today.
  kWhole = 1,        ///< W: whole trips, no consolidation.
  kWholeRouted = 2,  ///< WR: whole trips and the routes.
  /// P (#1056 C3; core_common/trip_terms.h): THE PLAN WRITES THE PRICE.
  /// Production prices a cart's seam at whole trips, as W (the FALLBACK, the
  /// evening's), and writes each task's TaskTripTerms; the groom's plan draws
  /// whole trips from them, pools field heaps and takes back loads, and
  /// rewrites the seam from its drawn trips at the morning and every re-plan.
  /// The tours of WR are not this model's: PlanSupplyTours stays WR's.
  kPlanned = 3,
};

/// @brief THE ONE DOOR OF A LOAD's PRICE, cart-days: `grams` carried at
///        `rate`, by the configured model (item 1). On foot the walker's own
///        trips stand as today (haul.h): this door is the cart's.
/// @return 0 for no grams or no rate, as HaulDaysFor.
float CartDaysFor(const ProductionConfig& config, Grams grams, const HaulRate& rate);

/// @brief What `added` grams laid on a seam already priced for `before`
///        grams adds to its price - CartDaysFor(before + added) less
///        CartDaysFor(before): under whole trips a few kilograms more may cost
///        nothing (they ride the last trip) or a whole trip.
float CartDaysAdded(const ProductionConfig& config,
                    Grams before,
                    Grams added,
                    const HaulRate& rate);

/// @brief One receiver of a tour: the unit (a works, or a store for the
///        district lot) and what it takes off this cart.
struct TourStop {
  std::uint32_t unit_row = 0xFFFFFFFFU;
  ResourceAmounts grams;              ///< Dense by ResourceId, as SupplyLeg::grams.
  CargoForm form = CargoForm::kNone;  ///< The first form of its load.
};

/// @brief One sender's tour: the stops in the order the cart drives them, the
///        whole trips it takes and their hours.
struct CartTour {
  Vec2 from;
  std::uint8_t log_way = 0;  ///< As SupplyLeg::log_way.
  std::vector<TourStop> stops;
  Grams grams = 0;          ///< All the stops' grams.
  std::uint32_t trips = 0;  ///< Whole trips: ceil(grams ÷ the cart's load).
  float tour_hours = 0.0F;  ///< One trip: from, every stop in order, back.
  float empty_km = 0.0F;    ///< The return leg's km a trip, for the book.
};

/// @brief THE WORKS' SUPPLY TOURS (item 2; WR only): every works with
///        something awaiting on its FRONT leg, grouped by the leg's sender,
///        ordered greedily (item 3), split where the forms do not mix or the
///        load is full. In row order of the senders, deterministic.
/// @return Empty under F and W: each works is then its own tour, as today.
std::vector<CartTour> PlanSupplyTours(const ProductionConfig& config, const WorldState& world);

/// @brief A stop's share of its tour's whole trips, cart-days, by grams.
/// @pre `stop` < tour.stops.size().
float StopShareOfTour(const ProductionConfig& config, const CartTour& tour, std::size_t stop);

/// @brief THE GREEDY ORDER's PRICE AGAINST THE BEST ORDER of the same stops
///        (item 3's return condition; tours of up to five stops, every order
///        tried): km a trip, greedy and best. The probe's, not the run's.
struct TourOrderCheck {
  float greedy_km = 0.0F;
  float best_km = 0.0F;
};

TourOrderCheck CheckTourOrder(const WorldState& world, const CartTour& tour);

}  // namespace core

#endif  // CORE_PRODUCTION_CART_TRIPS_H_
