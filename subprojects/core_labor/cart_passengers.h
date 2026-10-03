/// @file
/// @brief Passengers on the goods carts' first leg (transport design §11,
///        «Подвода возит и груз, и людей»; routing stage A, 0.37.164).
/// @threading SINGLE_THREADED
/// Called once a day from the labour sub-step's hour 1, after the top-up:
/// the day's orders stand and nobody has set out.
///
/// WHAT IT DOES. A carter from the horse yard drives his empty first leg,
/// yard -> load, by the cart's way (road_route.h). A man walking to a work
/// near that way may sit on the bench: he walks to a point of the way, waits
/// for the cart there if it is not past yet, rides, gets off at the point
/// nearest his work and walks the rest. He rides only if that is quicker
/// than walking straight to the work; seats go to whoever walks farthest.
/// His road one way (WorkAssignment::travel_hours) becomes that whole
/// journey, wait included, and the cart is written on him
/// (WorkAssignment::rides_cart_of). The night is the morning's mirror: the
/// labour hour books the road twice, and the cart's last leg brings him home
/// (§11, «Обратный путь») — no evening way of its own (stage B).
///
/// APPROXIMATIONS, NAMED: a walker's legs to and from the cart are straight
/// lines at the pedestrian's off-road weight (the network's own walk is the
/// shorter of the two and is not asked here); the cart's points are its way
/// sampled every kSampleMetres; the cart does not stop for anybody and its
/// pace does not change with the bench full; the load of the trip the
/// passengers ride is not lowered to 0.6 t (STUB core, transport.csv).

#ifndef CORE_LABOR_CART_PASSENGERS_H_
#define CORE_LABOR_CART_PASSENGERS_H_

#include <cstdint>

#include "labor_config.h"

namespace core {

struct WorldState;

/// @brief What the morning's seating did, for the pair's print.
struct PassengerTally {
  /// Goods carts out from the horse yard on their first leg today.
  std::uint32_t carts = 0;

  /// Bench seats on them (carts × LaborConfig::cart_passenger_seats).
  std::uint32_t seats = 0;

  /// Walkers seated.
  std::uint32_t seated = 0;

  /// Walkers for whom a cart would have been quicker, and no seat was left.
  std::uint32_t no_seat = 0;

  /// The longest wait for a cart at the boarding point among those seated,
  /// game hours — the stop condition of 2 October 2026 reads it: above one
  /// hour the watchdog's wait interface comes first.
  float worst_wait_hours = 0.0F;

  /// The sum of the seated's waits, game hours, for the mean.
  float wait_hours = 0.0F;

  /// Game hours of road the seated saved against walking, one way, summed.
  float hours_saved = 0.0F;
};

/// @brief Seats walkers on the first leg of the goods carts that set out from
///        the horse yard today, and writes each seated man's journey on him.
/// @pre The day's placement and top-up are done (hour 1).
/// @post A seated man's work has rides_cart_of = his driver and travel_hours
///       = walk to the cart + wait + ride + walk to the work, one way; nobody
///       else's work is touched. With no seats (the config's nought) or no
///       standing horse yard, nothing is done.
/// @return The tally of the morning.
PassengerTally SeatCartPassengers(const LaborConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_LABOR_CART_PASSENGERS_H_
