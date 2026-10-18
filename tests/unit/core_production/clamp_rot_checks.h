/// @file
/// @brief The checks of the clamps' rot and its lamp «В буртах сгнило N т»
/// (#1005; clamp_rot_state.h, clamp_rot_lamp.h): the writer books a clamp's
/// rot into `spoiled`, its share and the window, and nobody else's; the
/// window forgets a year back; the lamp lights in its months, on its
/// threshold, with no vegetable store in hand, coloured by the harvest.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_CLAMP_ROT_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_CLAMP_ROT_CHECKS_H_

/// @brief Runs every check of the clamps' rot and its lamp.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheClampRotAndItsLamp();

#endif  // TESTS_UNIT_CORE_PRODUCTION_CLAMP_ROT_CHECKS_H_
