/// @file
/// @brief The checks of step 4 «край» and «Без соли» (salt_edge.h; #1630 (C)):
/// the run and its condition, the deaths by their own cause, the leaving.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_RESIDENTS_SALT_EDGE_CHECKS_H_
#define TESTS_UNIT_CORE_RESIDENTS_SALT_EDGE_CHECKS_H_

/// @brief Runs every check of step 4 and the ending.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSaltEdge();

#endif  // TESTS_UNIT_CORE_RESIDENTS_SALT_EDGE_CHECKS_H_
