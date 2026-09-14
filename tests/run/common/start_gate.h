/// @file
/// @brief The question a building policy asks before it starts a site: may a
/// site of this type start this level today?
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// One type for every policy that starts sites, so BuildingChairman can hand
/// them all the same answer — today, whether the boards the sawmill is built
/// of would survive the start (SawmillPolicy::SparesBoardsFor, parcel 305).

#ifndef TESTS_RUN_COMMON_START_GATE_H_
#define TESTS_RUN_COMMON_START_GATE_H_

#include <cstdint>
#include <functional>
#include <utility>

#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace run {

/// @brief True when a site of `type` may start `level` today. An empty gate
/// lets everything start.
using StartGate = std::function<bool(const core::WorldState&, core::UnitTypeId, std::uint8_t)>;

/// @brief Asks `gate`, treating an empty gate as an open one.
inline bool GateOpen(const StartGate& gate,
                     const core::WorldState& world,
                     core::UnitTypeId type,
                     std::uint8_t level) {
  return !gate || gate(world, type, level);
}

}  // namespace run

#endif  // TESTS_RUN_COMMON_START_GATE_H_
