/// @file
/// @brief The checks of the chairman's transport door (core_world/
/// chairman_transport.h; task 957, camera-and-visuals.md «Нет ресурса — нет
/// транспорта»): which herds count as his stable and when the horse leaves
/// the hitching post. Kept out of the module's main.cpp, which is past the
/// file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_WORLD_CHAIRMAN_TRANSPORT_CHECKS_H_
#define TESTS_UNIT_CORE_WORLD_CHAIRMAN_TRANSPORT_CHECKS_H_

/// @brief Runs every check of the chairman's transport door.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheChairmansTransportDoor();

#endif  // TESTS_UNIT_CORE_WORLD_CHAIRMAN_TRANSPORT_CHECKS_H_
