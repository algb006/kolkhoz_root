/// @file
/// @brief The checks of the yard's herds an ended family leaves to its heir
/// (#1374; demography.cpp DropFamilyIfEmpty): a herd the heir has no flock of
/// moves marked `inherited`, and so does one merged into an heir's EMPTY flock
/// of its kind; one merged into a flock with heads is the heir's own kind.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_RESIDENTS_INHERITANCE_CHECKS_H_
#define TESTS_UNIT_CORE_RESIDENTS_INHERITANCE_CHECKS_H_

/// @brief Runs every check of the yard's inheritance.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheYardsInheritance();

#endif  // TESTS_UNIT_CORE_RESIDENTS_INHERITANCE_CHECKS_H_
