/// @file
/// @brief The checks of the perennial's cut (production_system.cpp,
/// PerennialCutDue; 0.37.167): one cut a calendar year on any day of the
/// window — grass sown on day 1 of its harvest month is cut in the same
/// window once it has grown its days; an old stand is cut on the window's
/// first day as before; a stand cut this year is not cut again.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_PERENNIAL_CUT_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_PERENNIAL_CUT_CHECKS_H_

/// @brief Runs every check of the perennial's cut.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckThePerennialsCut();

#endif  // TESTS_UNIT_CORE_PRODUCTION_PERENNIAL_CUT_CHECKS_H_
