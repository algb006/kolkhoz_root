/// @file
/// @brief The checks of the heap lamp's days to its loss (0.37.201;
/// core_production/production_alarms.h, CollectFieldAlarms,
/// kHarvestWaitingOnField): a heap waiting for the carts is yellow, and red
/// only while the groom's rule says it spoils within a day — its task at level
/// 0 — kept out of the module's main.cpp, which is twelve times the file-size
/// limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_HEAP_LAMP_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_HEAP_LAMP_CHECKS_H_

/// @brief Runs every check of the heap lamp's colour.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHeapLampIsRedOnlyWhenItSpoils();

#endif  // TESTS_UNIT_CORE_PRODUCTION_HEAP_LAMP_CHECKS_H_
