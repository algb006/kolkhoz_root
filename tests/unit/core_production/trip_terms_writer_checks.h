/// @file
/// @brief The checks of the trip's facts written by the pricers (core_production's
///        trip_terms_writer.h; #1056 C3 body 2/6): the loaded half beside the
///        round trip, the facts written only for a cart's whole-trip rate under
///        the planned model, and the task found by its kind as well as its load.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_TRIP_TERMS_WRITER_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_TRIP_TERMS_WRITER_CHECKS_H_

/// @brief Runs every check of the trip's facts.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheTripsFacts();

#endif  // TESTS_UNIT_CORE_PRODUCTION_TRIP_TERMS_WRITER_CHECKS_H_
