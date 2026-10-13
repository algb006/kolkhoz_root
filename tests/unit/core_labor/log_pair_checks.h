/// @file
/// @brief The checks of logs by two (#671, the second delivery; transport
/// design §2, «Брёвна вдвоём — только подвоз на стройку»): the morning's
/// pairs, the gate's three terms, the lone hand that never takes a log, and
/// the year's book - kept out of the module's main.cpp, which is far over the
/// file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_LOG_PAIR_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_LOG_PAIR_CHECKS_H_

/// @brief Runs every check of logs by two.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckLogPairs();

#endif  // TESTS_UNIT_CORE_LABOR_LOG_PAIR_CHECKS_H_
