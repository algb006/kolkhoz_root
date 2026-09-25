/// @file
/// @brief The start's road network: the map's roads laid into the world at
///        genesis, with the condition the start layout gives them.
/// @threading SINGLE_THREADED
/// Called once, by genesis, on a world nobody else holds yet.
///
/// UNTIL 0.36.0 A ROAD PLACED NOTHING IN THE CORE (genesis.cpp, the skipped
/// kRoad row): its wear lived in start_layout.csv for the layer alone. The
/// network is the core's now (roads design §13, the human's word of 25
/// September 2026: «пусть ядро делает дороги и роутинг по ним»), and the
/// layout row gives it what it gave the layer — the wear and the word of how
/// often it is driven — for every stretch of the road alike.

#ifndef CORE_WORLD_START_ROADS_H_
#define CORE_WORLD_START_ROADS_H_

#include <string>

#include "start_layout.h"

namespace core {

class ITableSet;
struct WorldState;

/// @brief Lays every road of tables/roads.csv into `world.roads`: a dirt
///        road or a path, the map's origin, its axis, its stretches; wear
///        and traffic word from the start layout's road row of the same key.
/// @return false, with the reason in `error`, when roads.csv is malformed or
///         a road row of the layout names no road of the map. A map road
///         with no layout row starts at wear 0 and "regular" — the table
///         says nothing about it, so nothing is inherited.
bool PlaceMapRoads(const ITableSet& tables,
                   const StartLayout& layout,
                   WorldState& world,
                   std::string& error);

}  // namespace core

#endif  // CORE_WORLD_START_ROADS_H_
