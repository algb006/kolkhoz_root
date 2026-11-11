/// @file
/// @brief The checks of the salt (salt_issue.h; #1630 (B)): the month's issue
/// by eaters, the day's norm and its substitutes, the count, its steps and
/// their price.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_RESIDENTS_SALT_CHECKS_H_
#define TESTS_UNIT_CORE_RESIDENTS_SALT_CHECKS_H_

/// @brief Runs every check of the salt.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSalt();

#endif  // TESTS_UNIT_CORE_RESIDENTS_SALT_CHECKS_H_
