// Whole trips and the routes (cart_trips.h): the price door; the tours come
// with the routes' piece.

#include "cart_trips.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "core_common/calendar.h"
#include "core_common/haul.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/work_seam.h"
#include "field_haul.h"

namespace core {
namespace {

/// The form of a resource's load (resources.csv `cargo_form`); «в таре» for a
/// table set with no column, as every load on foot was a sack.
CargoForm FormOf(const ProductionConfig& config, std::size_t resource) {
  return resource < config.porter.cargo_form.size() ? config.porter.cargo_form[resource]
                                                    : CargoForm::kPacked;
}

/// One body's worth of a stop's load: the resources of the stop's front leg
/// that mix with each other (CargoFormsMix), their grams and the first form.
struct Parcel {
  std::uint32_t unit_row = 0;
  std::size_t first_resource = 0;
  CargoForm form = CargoForm::kNone;
  ResourceAmounts grams;
  Grams total = 0;
};

bool Mixes(const ProductionConfig& config, const Parcel& body, std::size_t resource) {
  const CargoForm added = FormOf(config, resource);
  // FOOD WITH FUEL is not told apart by the core's tables (no fuel in Epoch
  // I's supply): read as «does not happen» - a STUB named.
  return CargoFormsMix(body.form, added, resource == body.first_resource, false) &&
         (body.form != CargoForm::kBulk || resource == body.first_resource);
}

/// The front leg of a works split into parcels that mix (В4 item 2).
std::vector<Parcel> ParcelsOf(const ProductionConfig& config,
                              std::uint32_t unit_row,
                              const SupplyLeg& leg) {
  std::vector<Parcel> parcels;
  for (std::size_t resource = 0; resource < leg.grams.size(); ++resource) {
    if (leg.grams[resource] <= 0) {
      continue;
    }
    Parcel* home = nullptr;
    for (Parcel& parcel : parcels) {
      if (Mixes(config, parcel, resource)) {
        home = &parcel;
        break;
      }
    }
    if (home == nullptr) {
      Parcel parcel;
      parcel.unit_row = unit_row;
      parcel.first_resource = resource;
      parcel.form = FormOf(config, resource);
      parcel.grams.assign(leg.grams.size(), 0);
      parcels.push_back(parcel);
      home = &parcels.back();
    }
    home->grams[resource] += leg.grams[resource];
    home->total += leg.grams[resource];
  }
  return parcels;
}

/// The pace of the cart today, game hours a km, empty and loaded (as
/// haul_ways.cpp, RateToward).
struct CartPace {
  float empty = 0.0F;
  float loaded = 0.0F;
};

CartPace PaceOf(const ProductionConfig& config, const WorldState& world) {
  const float bed = HaulBedFactor(config, world.weather);
  const float empty_kmh = config.harness_speed_kmh * bed;
  const float loaded_kmh = config.loaded_speed_kmh * bed;
  return CartPace{
      .empty = empty_kmh > 0.0F ? static_cast<float>(kClockScale) / empty_kmh : 0.0F,
      .loaded = loaded_kmh > 0.0F ? static_cast<float>(kClockScale) / loaded_kmh : 0.0F};
}

/// One trip's hours over the stops in order: loaded out and between, empty
/// back; one loading pause at the start and an unloading pause at every stop
/// (#844, kLoadingPauseHours).
float TourHours(const WorldState& world,
                TravelMode mode,
                const CartPace& pace,
                Vec2 from,
                const std::vector<Vec2>& stops,
                float* empty_km) {
  float loaded_km = 0.0F;
  Vec2 at = from;
  for (const Vec2 stop : stops) {
    loaded_km += RoadKm(world, mode, at, stop);
    at = stop;
  }
  const float back = RoadKm(world, mode, at, from);
  if (empty_km != nullptr) {
    *empty_km = back;
  }
  const float road = loaded_km * pace.loaded + back * pace.empty;
  // THE PAUSES, NOT A FLOOR (#844): until 0.37.302 the tour took haul.cpp's
  // 0.25 h floor; now a loading and an unloading at each stop, on the road.
  const float stops_paused = static_cast<float>(stops.empty() ? 1U : stops.size());
  return (road > 0.0F ? road : 0.0F) + (kLoadingPauseHours * (1.0F + stops_paused));
}

}  // namespace

float CartDaysFor(const ProductionConfig& config, Grams grams, const HaulRate& rate) {
  // THE FLUID, and every carrier on foot: a load pays its fraction of a round
  // trip (haul.h, HaulDaysFor). A walker's trip is the back's own (#671).
  if (config.cart_trip_model == static_cast<std::uint8_t>(CartTripModel::kFluid) ||
      !rate.produce_cart) {
    return HaulDaysFor(grams, rate, config.standard_day_hours);
  }
  if (grams <= 0 || rate.load <= 0 || !(rate.round_trip_hours > 0.0F) ||
      !(config.standard_day_hours > 0.0F)) {
    return 0.0F;
  }
  // WHOLE TRIPS: a cart leaves with what lies, up to its load, and comes back
  // (the round trip holds the empty return).
  const std::int64_t trips = (grams + rate.load - 1) / rate.load;
  return static_cast<float>(trips) * rate.round_trip_hours / config.standard_day_hours;
}

float CartDaysAdded(const ProductionConfig& config,
                    Grams before,
                    Grams added,
                    const HaulRate& rate) {
  if (added <= 0) {
    return 0.0F;
  }
  // THE FLUID PRICES THE ADDED PART ALONE, as it did: the difference of two
  // prices is the same number in arithmetic and not in floats - the F check
  // against 0.37.256 found 0.01 kg of potato moved in one village-year by it.
  if (config.cart_trip_model == static_cast<std::uint8_t>(CartTripModel::kFluid) ||
      !rate.produce_cart) {
    return HaulDaysFor(added, rate, config.standard_day_hours);
  }
  const Grams from = before > 0 ? before : 0;
  return CartDaysFor(config, from + added, rate) - CartDaysFor(config, from, rate);
}

std::vector<CartTour> PlanSupplyTours(const ProductionConfig& config, const WorldState& world) {
  std::vector<CartTour> tours;
  if (config.cart_trip_model != static_cast<std::uint8_t>(CartTripModel::kWholeRouted) ||
      !SettlementHasCarts(world, config.horse_kind)) {
    return tours;
  }

  // THE SENDERS: every works with something on its front leg, not paused,
  // grouped by the leg's sender and way (SupplyLeg::from, log_way), in the
  // order their first works stands in the rows.
  struct Group {
    Vec2 from;
    std::uint8_t log_way = 0;
    std::vector<Parcel> parcels;
  };

  std::vector<Group> groups;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    const ConstructionState& works = unit.construction;
    if (unit.dead != 0 || unit.paused != 0 || works.legs.empty()) {
      continue;
    }
    const SupplyLeg& front = works.legs.front();
    std::vector<Parcel> parcels = ParcelsOf(config, row, front);
    if (parcels.empty()) {
      continue;
    }
    Group* group = nullptr;
    for (Group& known : groups) {
      if (known.from.x == front.from.x && known.from.y == front.from.y &&
          known.log_way == front.log_way) {
        group = &known;
      }
    }
    if (group == nullptr) {
      groups.push_back(Group{.from = front.from, .log_way = front.log_way, .parcels = {}});
      group = &groups.back();
    }
    for (Parcel& parcel : parcels) {
      group->parcels.push_back(std::move(parcel));
    }
  }
  const CartPace pace = PaceOf(config, world);
  for (Group& group : groups) {
    const TravelMode mode = group.log_way != 0 ? TravelMode::kLogCart : TravelMode::kCart;
    std::vector<Parcel> left = std::move(group.parcels);
    while (!left.empty()) {
      // THE NEAREST FIRST (item 3, greedy): from the sender, then from the
      // last stop; ties to the first in the list (the rows' order).
      const auto nearest_from = [&](Vec2 at, const auto& takes) {
        std::size_t best = left.size();
        float best_km = std::numeric_limits<float>::max();
        for (std::size_t index = 0; index < left.size(); ++index) {
          if (!takes(left[index])) {
            continue;
          }
          const float km = RoadKm(world, mode, at, world.units.rows[left[index].unit_row].position);
          if (km < best_km) {
            best_km = km;
            best = index;
          }
        }
        return best;
      };
      const std::size_t first_index = nearest_from(group.from, [](const Parcel&) { return true; });
      const Parcel first = left[first_index];
      left.erase(left.begin() + static_cast<std::ptrdiff_t>(first_index));
      const Grams capacity = CartLoadGrams(
          config.porter,
          config.cart_load_kg,
          DefIdFromIndex<ResourceIdTag>(static_cast<std::uint32_t>(first.first_resource)));
      CartTour tour;
      tour.from = group.from;
      tour.log_way = group.log_way;
      tour.grams = first.total;
      tour.stops.push_back(
          TourStop{.unit_row = first.unit_row, .grams = first.grams, .form = first.form});
      std::vector<Vec2> way{world.units.rows[first.unit_row].position};
      // A LOAD THAT FILLS A CART rides alone, its trips whole; the rest share.
      while (capacity > 0 && tour.grams < capacity) {
        const std::size_t next = nearest_from(way.back(), [&](const Parcel& parcel) {
          return parcel.form == first.form &&
                 Mixes(config,
                       Parcel{.unit_row = first.unit_row,
                              .first_resource = first.first_resource,
                              .form = first.form,
                              .grams = {},
                              .total = 0},
                       parcel.first_resource) &&
                 tour.grams + parcel.total <= capacity;
        });
        if (next == left.size()) {
          break;
        }
        const Parcel& taken = left[next];
        tour.grams += taken.total;
        tour.stops.push_back(
            TourStop{.unit_row = taken.unit_row, .grams = taken.grams, .form = taken.form});
        way.push_back(world.units.rows[taken.unit_row].position);
        left.erase(left.begin() + static_cast<std::ptrdiff_t>(next));
      }
      tour.trips =
          capacity > 0 ? static_cast<std::uint32_t>((tour.grams + capacity - 1) / capacity) : 0U;
      tour.tour_hours = TourHours(world, mode, pace, group.from, way, &tour.empty_km);
      tours.push_back(std::move(tour));
    }
  }
  return tours;
}

float StopShareOfTour(const ProductionConfig& config, const CartTour& tour, std::size_t stop) {
  if (stop >= tour.stops.size() || tour.grams <= 0 || !(config.standard_day_hours > 0.0F)) {
    return 0.0F;
  }
  Grams mine = 0;
  for (const Grams grams : tour.stops[stop].grams) {
    mine += grams;
  }
  const float tour_days =
      static_cast<float>(tour.trips) * tour.tour_hours / config.standard_day_hours;
  return tour_days * static_cast<float>(mine) / static_cast<float>(tour.grams);
}

TourOrderCheck CheckTourOrder(const WorldState& world, const CartTour& tour) {
  TourOrderCheck check;
  const TravelMode mode = tour.log_way != 0 ? TravelMode::kLogCart : TravelMode::kCart;
  std::vector<Vec2> stops;
  for (const TourStop& stop : tour.stops) {
    stops.push_back(world.units.rows[stop.unit_row].position);
  }
  const auto km_of = [&](const std::vector<Vec2>& order) {
    float km = 0.0F;
    Vec2 at = tour.from;
    for (const Vec2 stop : order) {
      km += RoadKm(world, mode, at, stop);
      at = stop;
    }
    return km + RoadKm(world, mode, at, tour.from);
  };
  check.greedy_km = km_of(stops);
  check.best_km = check.greedy_km;
  constexpr std::size_t kMaxStopsTried = 5;
  if (stops.size() <= kMaxStopsTried) {
    std::vector<std::size_t> order(stops.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
      order[index] = index;
    }
    do {
      std::vector<Vec2> tried;
      for (const std::size_t index : order) {
        tried.push_back(stops[index]);
      }
      check.best_km = std::min(check.best_km, km_of(tried));
    } while (std::next_permutation(order.begin(), order.end()));
  }
  return check;
}

}  // namespace core
