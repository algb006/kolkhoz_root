/// @file
/// @brief #1568 link 1: the drinker who does not come out (crime §6 «Прогулы —
/// своя дверь… тоже начинается с 50»; econ's spec of 5 Nov 2026).
/// @threading SINGLE_THREADED
/// Asked from the assignment decisions (phase 3) on the sim thread; a pure read
/// of the state, so it may be asked as often as the day's lists are drawn and
/// answers the same: the draw is a counter hash of the seed, the day and the
/// resident, never the world's generator.

#ifndef CORE_LABOR_TRUANCY_H_
#define CORE_LABOR_TRUANCY_H_

#include <cstdint>

#include "core_common/world_state.h"
#include "labor_config.h"

namespace core {

/// @brief The chance today that the man of `row` stays home: 0 below
///        truancy_from_alcoholism, rising linearly to truancy_share_at_cap at
///        Epoch I's alcoholism cap of 60, and 0 for a woman, a boy under 16 or
///        with `crime_truancy_on` 0.
float TruancyChance(const LaborConfig& config, const WorldState& current, std::uint32_t row);

/// @brief Whether the man of `row` truants today: TruancyChance against the
///        day's counter-hash draw for him. The same answer every time it is
///        asked within a day.
bool TruantToday(const LaborConfig& config, const WorldState& current, std::uint32_t row);

}  // namespace core

#endif  // CORE_LABOR_TRUANCY_H_
