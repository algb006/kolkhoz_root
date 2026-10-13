/// @file
/// @brief The checks of the new household's advance (#170; labor-payment
///        design, «Аванс новосёлу»; household_advance.h and the store's half
///        in family_exchange.cpp): who is advanced, how much, out of what,
///        and how it is paid back.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_RESIDENTS_ADVANCE_CHECKS_H_
#define TESTS_UNIT_CORE_RESIDENTS_ADVANCE_CHECKS_H_

/// @brief Runs every check of the advance.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheNewHouseholdsAdvance();

#endif  // TESTS_UNIT_CORE_RESIDENTS_ADVANCE_CHECKS_H_
