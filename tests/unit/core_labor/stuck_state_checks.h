/// @file
/// @brief The checks of labour's states that stood for ever (architecture
/// §7ж³; the inventory of 2 October 2026 and the outside reader's): a
/// standing work order on a target with no work pinned its man there every
/// morning; a post whose unit was gone kept its holder out of the day's
/// candidates for good; a module being taken down with no sound parent got
/// no crew, so its demolition never ended — kept out of the module's
/// main.cpp, which is four times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_LABOR_STUCK_STATE_CHECKS_H_
#define TESTS_UNIT_CORE_LABOR_STUCK_STATE_CHECKS_H_

/// @brief Runs every check of the states that stood for ever.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckStuckStates();

#endif  // TESTS_UNIT_CORE_LABOR_STUCK_STATE_CHECKS_H_
