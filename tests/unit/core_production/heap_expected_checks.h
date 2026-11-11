/// @file
/// @brief The checks of #1606's cure (ExpectedHeapHayGrams; world_params
/// hay_heaps_expected_value): the hay forecast counts a field heap at what
/// reaches a store, hauled at the measured pace while it rots.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_HEAP_EXPECTED_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_HEAP_EXPECTED_CHECKS_H_

/// @brief Runs every check of the heaps' expected value.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheHeapsCountAtExpectedValue();

#endif  // TESTS_UNIT_CORE_PRODUCTION_HEAP_EXPECTED_CHECKS_H_
