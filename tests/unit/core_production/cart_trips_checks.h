/// @file
/// @brief The checks of whole trips and the routes (core_production's
///        cart_trips.h; routing stage В4): the one price door under the fluid
///        and under whole trips, the added part's price, and the works'
///        supply tours - who rides together and who does not.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_CART_TRIPS_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_CART_TRIPS_CHECKS_H_

/// @brief Runs every check of whole trips and the routes.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckWholeTripsAndTheRoutes();

#endif  // TESTS_UNIT_CORE_PRODUCTION_CART_TRIPS_CHECKS_H_
