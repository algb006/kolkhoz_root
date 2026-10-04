/// @file
/// @brief The checks of the watchdog (core_world/watchdog.h, wait_rules.h;
/// architecture §7ж³; routing stage B, B6): a wait that hangs is found within
/// its kind's poll, its agent is put where it belongs, and a journal line is
/// written — every kind, by faults: a passenger whose driver is gone or whose
/// term ran out, a horse at its worker's yard past its night. Kept out of the
/// module's main.cpp, which is past the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_WORLD_WATCHDOG_CHECKS_H_
#define TESTS_UNIT_CORE_WORLD_WATCHDOG_CHECKS_H_

/// @brief Runs every check of the watchdog.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheWatchdog();

#endif  // TESTS_UNIT_CORE_WORLD_WATCHDOG_CHECKS_H_
