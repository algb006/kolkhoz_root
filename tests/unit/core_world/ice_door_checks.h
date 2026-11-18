/// @file
/// @brief The checks of the ice door (ISimulation::IceAtToday; core_common/ice_view.h;
/// #1859) on the shipped tables: the lake is frozen and holds a man on the campaign's
/// first day, a lake's ice is thicker than the river's on the same day, and dry land
/// carries no ice. Kept out of the module's main.cpp, which is past the file-size limit.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_WORLD_ICE_DOOR_CHECKS_H_
#define TESTS_UNIT_CORE_WORLD_ICE_DOOR_CHECKS_H_

/// @brief Runs every check of the ice door.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheIceDoor();

#endif  // TESTS_UNIT_CORE_WORLD_ICE_DOOR_CHECKS_H_
