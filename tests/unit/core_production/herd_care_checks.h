/// @file
/// @brief The checks of «недоуход» (#1443; herd_care.h): the care curve, the
/// cow's milk under it through YieldFactor, and the horses' draught.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_HERD_CARE_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_HERD_CARE_CHECKS_H_

/// @brief Runs every check of undone herd care.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckUndoneHerdCare();

#endif  // TESTS_UNIT_CORE_PRODUCTION_HERD_CARE_CHECKS_H_
