/// @file
/// @brief The checks of the passengers on the goods carts' first leg
/// (core_labor/cart_passengers.h; routing stage A): a walker whose work lies
/// along a cart's way rides when that is quicker, seats go to whoever walks
/// farthest, a walker whose work lies the other way walks, and no yard or no
/// seats seat nobody.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_CART_PASSENGERS_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_CART_PASSENGERS_CHECKS_H_

/// @brief Runs every check of the carts' passengers.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheCartsPassengers();

#endif  // TESTS_UNIT_CORE_LABOR_CART_PASSENGERS_CHECKS_H_
