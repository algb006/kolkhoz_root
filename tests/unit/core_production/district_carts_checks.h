/// @file
/// @brief The checks of whose transport carries to the district (core_production's
///        district_carts.h; routing stage В5): the district's calendar and its
///        mud slip, the plan's standing request and its collection, the own
///        delivery's row, and the own-carts bonus.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_DISTRICT_CARTS_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_DISTRICT_CARTS_CHECKS_H_

/// @brief Runs every check of the district's transport.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckWhoseTransportToTheDistrict();

#endif  // TESTS_UNIT_CORE_PRODUCTION_DISTRICT_CARTS_CHECKS_H_
