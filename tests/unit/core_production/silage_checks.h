/// @file silage_checks.h
/// @brief The checks of the autumn's silage laying (silage_laying.h; livestock
/// design §8; 0.37.248): the order and its refusals, the nearest meadow with
/// an empty heap turned and its neighbour left to the hay, the rain that does
/// not stop the cut, green mass laid, carted to the trench and not rotting
/// there, the trench sealed and ripe — kept out of the module's main.cpp,
/// which is twelve times the file-size limit already.
/// @threading SINGLE_THREADED

#ifndef TESTS_UNIT_CORE_PRODUCTION_SILAGE_CHECKS_H_
#define TESTS_UNIT_CORE_PRODUCTION_SILAGE_CHECKS_H_

/// @brief Runs every check of the silage laying.
/// @return The number of failed assertions; each prints a FAIL line.
int CheckTheSilageLaying();

#endif  // TESTS_UNIT_CORE_PRODUCTION_SILAGE_CHECKS_H_
