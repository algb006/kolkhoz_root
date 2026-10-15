/// @file
/// @brief The door «Нет ресурса — нет транспорта» read off the completed
///        world (chairman_transport_view.h; task 957).
/// @threading SINGLE_THREADED
/// Called between steps, on the completed state only.
#ifndef CORE_WORLD_CHAIRMAN_TRANSPORT_H_
#define CORE_WORLD_CHAIRMAN_TRANSPORT_H_

#include "core_common/chairman_transport_view.h"
#include "core_common/ids.h"

namespace core {

struct WorldState;

/// @brief Whether the chairman's horse stands at the hitching post today.
/// @param horse_kind livestock.csv "horse"; an invalid kind finds no herd and
///        answers kNoStableYet.
/// @return Present while any kolkhoz horse herd with heads at a unit got a
///         share of today's ration; absent when every one got nothing; present
///         with kNoStableYet when no such herd stands at a unit.
ChairmanTransportView ChairmanTransportOf(const WorldState& world, LivestockKindId horse_kind);

}  // namespace core

#endif  // CORE_WORLD_CHAIRMAN_TRANSPORT_H_
