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

#endif  // TESTS_UNIT_CORE_PRODUCTION_FEED_HOME_CHECKS_H_
