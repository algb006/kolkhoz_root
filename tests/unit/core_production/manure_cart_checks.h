/// @file
/// @brief The checks of manure carted heap -> field on sledges (#1056 C2;
///        core_production's manure_cart.h; gate M-h): the order nearest of
///        the poor first, the carry-over in the heap, the price on a sledge
///        day only, the settle, the late dose cancelled into the heap, and
///        the reaped heap asked before the dose. Kept out of the module's
///        main.cpp, which is far over the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_MANURE_CART_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_MANURE_CART_CHECKS_H_

/// @brief Runs every check of the winter's manure carting.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckManureIsCartedOnSledges();

#endif  // TESTS_UNIT_CORE_PRODUCTION_MANURE_CART_CHECKS_H_
