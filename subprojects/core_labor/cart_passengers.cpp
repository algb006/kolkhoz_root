// Passengers on the goods carts' first leg (cart_passengers.h).

#include "cart_passengers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/horse_yard_road.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/resident_state.h"
#include "core_common/road_graph.h"
#include "core_common/road_route.h"
#include "core_common/road_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"
#include "labor_day.h"

namespace core {
namespace {

/// Metres between two points of a cart's way at which a man may board or
/// get off. The cart does not stop for anybody; this is how finely the way
/// is read, not a stop.
constexpr float kSampleMetres = 100.0F;

/// The least a ride must save against walking, game hours, for a man to take
/// it: below it the bench is no quicker, only different.
constexpr float kLeastSavingHours = 0.05F;

/// A point of a cart's way and when the cart is there, game hours after the
/// driver left his house.
struct CartPoint {
  Vec2 position;
  float hours = 0.0F;
};

/// One goods cart out on its first leg today.
struct Cart {
  ResidentId driver;
  std::vector<CartPoint> points;
  std::uint32_t seats_left = 0;
};

/// A walker who may ride.
struct Walker {
  std::uint32_t row = 0;
  Vec2 home;
  Vec2 work;
  float walk_hours = 0.0F;  // his road one way on foot, as measured
};

/// The best ride one walker can take on one cart.
struct Ride {
  float total_hours = 0.0F;
  float wait_hours = 0.0F;
};

float Distance(Vec2 from, Vec2 to) {
  return std::hypot(to.x - from.x, to.y - from.y);
}

/// The way's points every kSampleMetres, each with the cart's time there.
std::vector<CartPoint> SampleWay(const WorldState& world,
                                 const Route& way,
                                 Vec2 start,
                                 float hours_at_start,
                                 float ride_hours_per_km) {
  std::vector<CartPoint> points;
  points.push_back(CartPoint{.position = start, .hours = hours_at_start});
  float hours = hours_at_start;
  for (const RouteLeg& leg : way.legs) {
    const float leg_hours = leg.effective_km * ride_hours_per_km;
    const auto steps =
        static_cast<std::uint32_t>(std::max(1.0F, std::ceil(leg.length_m / kSampleMetres)));
    const std::vector<RoadPoint>* axis = nullptr;
    if (leg.on_road) {
      const std::uint32_t road_row = FindRow(world.roads, leg.road);
      axis = road_row == kNoRow ? nullptr : &world.roads.rows[road_row].axis;
    }
    for (std::uint32_t step = 1; step <= steps; ++step) {
      const float share = static_cast<float>(step) / static_cast<float>(steps);
      Vec2 position = leg.to;
      if (axis != nullptr) {
        position = PointAtChainage(
            *axis, leg.from_chainage_m + ((leg.to_chainage_m - leg.from_chainage_m) * share));
      } else if (!leg.on_road) {
        position = Vec2{.x = leg.from.x + ((leg.to.x - leg.from.x) * share),
                        .y = leg.from.y + ((leg.to.y - leg.from.y) * share)};
      }
      points.push_back(CartPoint{.position = position, .hours = hours + (leg_hours * share)});
    }
    hours += leg_hours;
  }
  return points;
}

/// The quickest way for `walker` by `cart`: walk to a point, wait for the
/// cart if it is not there yet, ride to a later point, walk to the work. For
/// each point he may get off at, the best point to have boarded at is kept
/// as a running minimum — one pass over the way.
Ride BestRide(const Walker& walker, const Cart& cart, float walk_hours_per_metre) {
  Ride best{.total_hours = walker.walk_hours, .wait_hours = 0.0F};
  bool found = false;
  // g(b) = max(walk to b, cart at b) - cart at b: the time lost before the
  // ride begins, counted from the cart's own clock.
  float best_g = 0.0F;
  float best_g_wait = 0.0F;
  bool have_board = false;
  for (const CartPoint& point : cart.points) {
    const float walk_to = Distance(walker.home, point.position) * walk_hours_per_metre;
    const float board = std::max(walk_to, point.hours);
    const float g = board - point.hours;
    if (!have_board || g < best_g) {
      best_g = g;
      best_g_wait = board - walk_to;
      have_board = true;
    }
    const float total =
        best_g + point.hours + (Distance(point.position, walker.work) * walk_hours_per_metre);
    if (!found || total < best.total_hours) {
      best.total_hours = total;
      best.wait_hours = best_g_wait;
      found = true;
    }
  }
  return best;
}

}  // namespace

PassengerTally SeatCartPassengers(const LaborConfig& config, WorldState& current) {
  PassengerTally tally;
  Vec2 yard{};
  if (config.cart_passenger_seats == 0 || !HorseYardPositionOf(current, config.horse_kind, yard)) {
    return tally;
  }
  const float walk_hours_per_km = HoursPerKm(config, WorkKind::kHarvest);
  const float ride_hours_per_km = HoursPerKm(config, WorkKind::kPlowing);
  const float walk_hours_per_metre =
      walk_hours_per_km * OffRoadWeightOf(current, TravelMode::kWalk) / 1000.0F;
  const std::shared_ptr<const RoadIndex> index = RoadIndexOf(current);

  // THE CARTS: a carter with a horse from the standing yard, his empty first
  // leg yard -> load by the cart's own way; the cart is at the yard when he
  // has walked there from his house.
  std::vector<Cart> carts;
  std::vector<std::uint8_t> drives(current.residents.rows.size(), 0);
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const WorkAssignment& work = current.residents.rows[row].work;
    if (work.kind != WorkKind::kHauling || !DayStartsAtHorseYard(current, work) ||
        !WorkRidesOut(current, work)) {
      continue;
    }
    Vec2 home{};
    Vec2 load{};
    if (!HomePositionOf(current, current.residents.rows[row].family, home) ||
        !WorkPlaceOf(current, work, load)) {
      continue;
    }
    const float to_yard = RoadKm(current, TravelMode::kWalk, home, yard) * walk_hours_per_km;
    const Route way = index->Way(WorkTravelMode(current, work), yard, load);
    carts.push_back(Cart{.driver = current.residents.row_ids[row],
                         .points = SampleWay(current, way, yard, to_yard, ride_hours_per_km),
                         .seats_left = config.cart_passenger_seats});
    drives[row] = 1;
  }
  tally.carts = static_cast<std::uint32_t>(carts.size());
  tally.seats = tally.carts * config.cart_passenger_seats;
  if (carts.empty()) {
    return tally;
  }

  // THE WALKERS: placed today, on foot, not carting (a carrier's walk to the
  // heap is his first trip, not a road — haul.h), not driving.
  std::vector<Walker> walkers;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& resident = current.residents.rows[row];
    const WorkAssignment& work = resident.work;
    if (drives[row] != 0 || work.kind == WorkKind::kNone || work.kind == WorkKind::kHauling ||
        WorkRidesOut(current, work)) {
      continue;
    }
    Vec2 home{};
    Vec2 place{};
    if (!HomePositionOf(current, resident.family, home) || !WorkPlaceOf(current, work, place)) {
      continue;
    }
    const float walk = work.travel_hours >= 0.0F ? work.travel_hours
                                                 : WorkRoadHours(current,
                                                                 work,
                                                                 config.horse_kind,
                                                                 home,
                                                                 place,
                                                                 walk_hours_per_km,
                                                                 ride_hours_per_km);
    walkers.push_back(Walker{.row = row, .home = home, .work = place, .walk_hours = walk});
  }
  // SEATS TO WHOEVER WALKS FARTHEST (§11, «Кому место, если желающих
  // больше»), ties by row — the same order every run.
  std::stable_sort(walkers.begin(), walkers.end(), [](const Walker& left, const Walker& right) {
    return left.walk_hours > right.walk_hours;
  });
  for (const Walker& walker : walkers) {
    std::size_t best_cart = carts.size();
    Ride best{.total_hours = walker.walk_hours - kLeastSavingHours, .wait_hours = 0.0F};
    bool quicker_somewhere = false;
    for (std::size_t cart = 0; cart < carts.size(); ++cart) {
      const Ride ride = BestRide(walker, carts[cart], walk_hours_per_metre);
      if (ride.total_hours >= walker.walk_hours - kLeastSavingHours) {
        continue;
      }
      quicker_somewhere = true;
      if (carts[cart].seats_left > 0 && ride.total_hours < best.total_hours) {
        best = ride;
        best_cart = cart;
      }
    }
    if (best_cart == carts.size()) {
      tally.no_seat += quicker_somewhere ? 1U : 0U;
      continue;
    }
    --carts[best_cart].seats_left;
    WorkAssignment& work = current.residents.rows[walker.row].work;
    work.rides_cart_of = carts[best_cart].driver;
    work.travel_hours = best.total_hours;
    ++tally.seated;
    tally.wait_hours += best.wait_hours;
    tally.worst_wait_hours = std::max(tally.worst_wait_hours, best.wait_hours);
    tally.hours_saved += walker.walk_hours - best.total_hours;
  }
  return tally;
}

}  // namespace core
