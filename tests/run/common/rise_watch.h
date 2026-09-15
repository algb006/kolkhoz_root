/// @file
/// @brief The row of a unit waiting to take its next step, for the policies
/// that supply building materials.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// A STEP REFUSED IS NOT IN THE QUEUE. The core takes an upgrade only with its
/// whole recipe in the village, so the chairman's yard waiting to rise to its
/// stable is no marked site — and every policy that brings materials for the
/// marked sites alone left it waiting. The saw was the first (seed 1929,
/// 2026-09-15: 1 t of boards short for three years); the stone, the logs and
/// the glass were the same knot on the same seed the day the bread norm moved
/// the trajectory: 2 t of stone of 12 for years, and the stable never stood.

#ifndef TESTS_RUN_COMMON_RISE_WATCH_H_
#define TESTS_RUN_COMMON_RISE_WATCH_H_

#include <cstdint>
#include <functional>

#include "core_common/world_state.h"

namespace run {

/// @brief Which row waits to rise with its step not yet taken, or
/// core::kNoRow. The step it waits for is `level + 1`.
using RiseWatch = std::function<std::uint32_t(const core::WorldState&)>;

}  // namespace run

#endif  // TESTS_RUN_COMMON_RISE_WATCH_H_
