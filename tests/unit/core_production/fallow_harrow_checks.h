/// @file
/// @brief The checks of #1837 (farming design §94; boss 17 Nov): a black fallow whose own bare
/// harrow ended within fallow_fresh_harrow_days opens its winter crop's sowing without a second
/// harrow; further off, or with the knob at 0, the harrow reopens at its norm as before.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_FALLOW_HARROW_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_FALLOW_HARROW_CHECKS_H_

/// @brief Runs every check of the fresh fallow's harrow on the shipped tables.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheFreshFallowIsNotHarrowedTwice();

#endif  // TESTS_UNIT_CORE_PRODUCTION_FALLOW_HARROW_CHECKS_H_
