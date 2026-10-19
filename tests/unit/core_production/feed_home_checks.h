/// @file
/// @brief The checks of a farm's own rick (core_production's feed_home.h;
///        routing stage В3, step two): two farms and two ricks on the shipped
///        tables - which rick a herd eats from, the counted day it ate from
///        the far one, which rick is short, and where the door sends a load.
///        Kept out of the module's main.cpp, which is far over the file-size
///        limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_FEED_HOME_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_FEED_HOME_CHECKS_H_

/// @brief Runs every check of the farm's own rick.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckEachFarmEatsFromItsOwnRick();

/// @brief Runs the checks of a rick's supply from another rick (#1056 C1.2;
///        core_production's rick_supply.h): the same two farms - the price
///        under the switch and without it, the sender, the threshold, and
///        the settle moving the drained share.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckARickIsSuppliedFromAnother();

#endif  // TESTS_UNIT_CORE_PRODUCTION_FEED_HOME_CHECKS_H_
