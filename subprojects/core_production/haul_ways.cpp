// The way a load takes and the cart's run in the year's book (haul_ways.h).
// Split out of field_haul.cpp when the porter's rules (#671) took that file
// past the thousand-line limit: the rate of a way is asked by every load -
// a field's, a pit's, a store's, a works' supply - and belongs to none.

#include "haul_ways.h"

#include <cstddef>

#include "core_common/calendar.h"
#include "core_common/haul.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/road_route.h"
#include "core_common/work_seam.h"
#include "field_haul.h"

namespace core {

// Shared with the supply of a works (haul_ways.h; supply_haul.cpp).
HaulRate RateToward(const ProductionConfig& config,
                    const WorldState& world,
                    Vec2 from,
                    Vec2 destination,
                    TravelMode cart_mode,
                    ResourceId carried) {
  // The one predicate production rates by and labour drains by (work_seam.h).
  const bool harnessed = SettlementHasCarts(world, config.horse_kind);
  // THE BED'S CONDITION slows the cart and the carrier alike (0.36.8; roads
  // design §1, §3) — wet after rain, muddy in the mud season, faster frozen.
  // THE DIRT BED'S, for the whole way: the start's roads are all dirt and
  // open ground is earth too. STUB until a gravel road is laid (delivery 7):
  // then a way over two beds is still priced by dirt's.
  const float bed = HaulBedFactor(config, world.weather);
  // THE CART OR THE SLEDGE (SL; cart_gear.h): on a snow day the sledge's
  // pace both ways and its load.
  const CartGear gear = CartGearToday(world, CartSpecsOf(config));
  const float speed_kmh = (harnessed ? gear.empty_kmh : config.walk_speed_kmh) * bed;
  const float hours_per_km = speed_kmh > 0.0F ? static_cast<float>(kClockScale) / speed_kmh : 0.0F;
  // THE LOADED HALF AT THE LOADED PACE (#570, 2a; 0.37.229): the cart's
  // cart_loaded speed, the bed's condition on it as on the empty half; a
  // carrier on foot walks both halves.
  const float loaded_kmh = (harnessed ? gear.loaded_kmh : config.walk_speed_kmh) * bed;
  const float loaded_hours_per_km =
      loaded_kmh > 0.0F ? static_cast<float>(kClockScale) / loaded_kmh : 0.0F;
  // A BACK'S TRIP BY THE LOAD'S FORM (#671): a bundle of hay is 10 kg, a sack
  // 20 - until 0.37.240 every load on foot was the sack.
  const Grams load = harnessed ? CartLoadGrams(config.porter, gear.load_kg, carried)
                               : WalkerLoadGrams(config.porter, carried);
  // BY THE ROAD, NOT THE STRAIGHT LINE (roads design §11-§13; 0.36.2): the
  // cart keeps to the network, the carrier walks roads, paths and open
  // ground as is shortest.
  const TravelMode mode = harnessed ? cart_mode : TravelMode::kWalk;
  if (mode != TravelMode::kCart) {
    HaulRate walked =
        RateOverKm(RoadKm(world, mode, from, destination), hours_per_km, loaded_hours_per_km, load);
    walked.receiver = destination;
    walked.has_receiver = true;
    // THE LOG CART IS A CART IN THE BOOK (routing stage В4): its trips are
    // booked as the produce cart's, with no off-road metres measured - the way
    // crosses the felling's ground, which is not «off the road» in the
    // question the book asks. Until В4 a stand's logs and a works' logs
    // straight off a stand were carted and booked nowhere.
    walked.produce_cart = mode == TravelMode::kLogCart;
    return walked;
  }
  // THE PRODUCE CART: the same way, and how much of it is off the road, for
  // the year's book (YearLedger::cart_trips; question 268). One choice of
  // way — Measure answers RoadKm's kilometres beside the metres.
  const RouteMeasure way = RoadMeasure(world, mode, from, destination);
  HaulRate rate = RateOverKm(way.effective_km, hours_per_km, loaded_hours_per_km, load);
  rate.produce_cart = true;
  rate.off_road_load_m = way.off_road_start_m;
  rate.off_road_store_m = way.off_road_end_m;
  rate.receiver = destination;
  rate.has_receiver = true;
  return rate;
}

HaulRate WalkRateToward(const ProductionConfig& config,
                        const WorldState& world,
                        Vec2 from,
                        Vec2 destination,
                        ResourceId carried) {
  // RateToward's carrier on foot, with no horse asked about.
  const float walk_kmh = config.walk_speed_kmh * HaulBedFactor(config, world.weather);
  const float hours_per_km = walk_kmh > 0.0F ? static_cast<float>(kClockScale) / walk_kmh : 0.0F;
  HaulRate walked = RateOverKm(RoadKm(world, TravelMode::kWalk, from, destination),
                               hours_per_km,
                               hours_per_km,
                               WalkerLoadGrams(config.porter, carried));
  walked.receiver = destination;
  walked.has_receiver = true;
  return walked;
}

void BookCartRun(const ProductionConfig& config,
                 WorldState& current,
                 CartLoadSource source,
                 const HaulRate& rate,
                 Grams moved) {
  const auto at = static_cast<std::size_t>(source);
  if (!rate.produce_cart || moved <= 0 || rate.load <= 0 || at >= kCartLoadSourceCountValue) {
    return;
  }
  YearLedger& book = current.ledger.current;
  const float trips = static_cast<float>(moved) / static_cast<float>(rate.load);
  book.cart_trips[at] += trips;
  book.cart_grams[at] += moved;
  book.cart_off_road_m[at] += trips * rate.off_road_load_m;
  if (rate.off_road_load_m > book.cart_off_road_worst_m[at]) {
    book.cart_off_road_worst_m[at] = rate.off_road_load_m;
  }
  book.cart_store_off_road_m[at] += trips * rate.off_road_store_m;
  if (rate.off_road_store_m > book.cart_store_off_road_worst_m[at]) {
    book.cart_store_off_road_worst_m[at] = rate.off_road_store_m;
  }
  // THE LOAD'S END ONLY against road_access_m (0.36.15): the store's gate is
  // a unit's road access, not the driving question 268 is about.
  if (rate.off_road_load_m > config.road_access_m) {
    book.cart_trips_off_road[at] += trips;
    book.cart_grams_off_road[at] += moved;
  }
}

}  // namespace core
