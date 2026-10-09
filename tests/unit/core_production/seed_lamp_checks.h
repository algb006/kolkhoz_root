/// @file
/// @brief The checks of the seed lamp's two false alarms (0.37.204;
/// core_production/production_alarms.h, CollectFieldAlarms, kSeedShort): a
/// sowing begun and not done on the day its window shuts is not short by a
/// year's rot, and no lamp lights for a window that has shut; and the seed
/// lying reaped in a field's heap counts as held — unless the plan is owed
/// it or the heap itself spoils by tomorrow. Kept out of the module's
/// main.cpp, which is twelve times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_SEED_LAMP_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_SEED_LAMP_CHECKS_H_

/// @brief Runs every check of the seed lamp on the closing day and beside a
///        reaped heap.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSeedLampIsNotAFalseAlarm();

#endif  // TESTS_UNIT_CORE_PRODUCTION_SEED_LAMP_CHECKS_H_
