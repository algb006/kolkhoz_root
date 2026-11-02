/// @file
/// @brief The checks of the first build's avatar paths on the limit's door
/// (#1417, #1421): the ex-chairman's carry, the promoted's arrival, the dealer's
/// grain payment. Kept out of the module's main.cpp, which is far over the
/// file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_AVATAR_PATHS_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_AVATAR_PATHS_CHECKS_H_

/// @brief Runs every check of the avatar paths on the limit's door.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheAvatarPathsOnTheLimit();

#endif  // TESTS_UNIT_CORE_PRODUCTION_AVATAR_PATHS_CHECKS_H_
