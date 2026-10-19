/// @file
/// @brief The checks of loose hay weathering where it lies (#1015; farming.csv
/// hay_weathering_*; SpoilFieldHeaps): a field's heap of hay loses the
/// season's share a month over the month's days and books it as spoiled; a
/// rick's hay keeps.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_HAY_WEATHERING_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_HAY_WEATHERING_CHECKS_H_

/// @brief Runs every check of the hay's weathering.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHayWeathersOnTheField();

#endif  // TESTS_UNIT_CORE_PRODUCTION_HAY_WEATHERING_CHECKS_H_
