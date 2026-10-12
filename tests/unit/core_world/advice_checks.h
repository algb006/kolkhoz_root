/// @file
/// @brief Every lamp's advice is a move the doors take (#715; boss, 12 October
/// 2026): for each AlarmAdvice kind with an order door, a world is set up on
/// its lamp's own trigger (the shipped tables, the canon's genesis, edited and
/// laid in by ISimulation::ResetWorld - the load's door), the lamp is read,
/// the order its advice names is staged, and the step must not refuse it. A
/// lamp whose advice the world refuses teaches the player to ignore lamps -
/// found on the cold lamp's «утеплить соломой» over a pen whose clay warm
/// rung stood open (refused in every village until 0.37.239). Kept out of the
/// module's main.cpp, which is past the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_WORLD_ADVICE_CHECKS_H_
#define TESTS_UNIT_CORE_WORLD_ADVICE_CHECKS_H_

/// @brief Runs every advice's fixture.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckEveryAdviceIsTaken();

#endif  // TESTS_UNIT_CORE_WORLD_ADVICE_CHECKS_H_
