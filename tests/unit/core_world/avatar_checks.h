/// @file
/// @brief The checks of the avatar key door (#634; world.h, ApplyStartAvatar):
/// every key of the shipped avatars.csv gives its start reputation and
/// factors, the empty key changes nothing, an unknown key is refused by name,
/// and the assembly refuses it too.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_WORLD_AVATAR_CHECKS_H_
#define TESTS_UNIT_CORE_WORLD_AVATAR_CHECKS_H_

/// @brief Runs every check of the avatar key door.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheAvatarDoor();

#endif  // TESTS_UNIT_CORE_WORLD_AVATAR_CHECKS_H_
