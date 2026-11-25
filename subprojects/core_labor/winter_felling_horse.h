/// @file
/// @brief THE WINTER HORSE FOR THE FELLERS (econ's #2072 rule; Alex 25 Nov 2026 «Зимнюю лошадь
///        давать», tasks #2401; econ/manual/proposals/winter-horses-for-fellers-2072-2026-11-25.md
///        §4a-§4b, timber.md §8а): from November to February, while a felling mark stands and
///        the logs in hand are short of a building season's, the fellers' ride gets ONE horse a
///        day ahead of the own-cart loads that can wait a day - a district lot, a store emptied,
///        a dig, the manure's dose, a field's heap, a works' supply. The milk leg and a rick's
///        supply never yield (milk spoils in a day, a herd without fodder starves); a stand's
///        logs are no competitor (they are the goal). WHY: the near forest a winter day's light
///        reaches on foot (~1.4 km) is gone by year 4 on the start map; the riding reach (~3.4
///        km) holds the rest, and on half the winter days the morning left no horse for the
///        ride while 19-25 horses carted loads that could wait (core's WFELL2072, WHORSE2072).
///        Private to core_labor.
/// @threading SINGLE_THREADED
/// Read by the decisions phase's placement (labor_system.cpp CollectJobs, DayParams).

#ifndef CORE_LABOR_WINTER_FELLING_HORSE_H_
#define CORE_LABOR_WINTER_FELLING_HORSE_H_

#include "assignment.h"
#include "core_catalog/timber_catalog.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Whether today is a winter-horse day: November to February, a felling mark with work
///        left, and the logs in hand - the stores' (built units) and those lying felled on the
///        stands - below the season's want, the closed book's logs built into walls and taken
///        in by the shops (the year gone's built_in + processed). A first year, whose closed
///        book is empty, has no want and no winter horse.
bool WinterFellingHorseDay(const WorldState& world, const TimberCatalog& timber);

/// @brief Whether `job` is an own-cart load that yields the winter horse: hauling that is not
///        a stand's logs, not the milk leg (the job names a herd) and not a rick's supply (its
///        unit RickSupplyStands).
bool YieldsTheWinterHorse(const WorldState& world, const AssignmentJob& job);

}  // namespace core

#endif  // CORE_LABOR_WINTER_FELLING_HORSE_H_
