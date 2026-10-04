/// @file
/// @brief The checks of the carted share reaching the store in its hour
/// (routing stage V1; core_production/field_haul.h, DeliverCartedLoads): an
/// hour's carting is in the stores that hour, not at the day's last tick, and
/// an hour nobody carted brings nothing — kept out of the module's main.cpp,
/// which is twelve times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_HOURLY_CARTING_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_HOURLY_CARTING_CHECKS_H_

/// @brief Runs every check of the hourly delivery.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheCartingReachesTheStoreInItsHour();

#endif  // TESTS_UNIT_CORE_PRODUCTION_HOURLY_CARTING_CHECKS_H_
