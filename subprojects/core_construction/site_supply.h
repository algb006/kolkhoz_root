/// @file
/// @brief How a site is supplied: what it takes from the standing stores, and
/// the one way an amount is added to a unit's stock.
/// @threading SINGLE_THREADED
/// Called only from inside the construction sub-step of the decisions slot
/// (phase 3), on the sim thread.
///
/// ONE HOME FOR TWO SITES' RULE. These stood as private statics of the
/// construction class until the insulation job (insulation.h) needed the very
/// same draw on the stores; a copy there would have been the rule's second
/// home, and the class's file was already past the project's 1000 lines.

#ifndef CORE_CONSTRUCTION_SITE_SUPPLY_H_
#define CORE_CONSTRUCTION_SITE_SUPPLY_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"

namespace core {

/// @brief Adds `delta` grams of `resource` to `amounts`, growing the dense
///        vector as needed and never leaving a line below zero.
/// @note An invalid resource and a zero delta change nothing.
void AddTo(ResourceAmounts& amounts, ResourceId resource, Grams delta);

/// @brief Takes up to `wanted` grams of `resource` from the standing units, in
///        row order, never from `site_row` itself and never what another
///        works holds back (UnreservedOf). Sites and unbuilt rows never give:
///        a level-0 unit stores nothing for anybody (71-construction.md §2).
/// @return The grams taken — removed from the givers' stock; the caller puts
///         them where they go.
Grams TakeFromStores(WorldState& current,
                     std::uint32_t site_row,
                     ResourceId resource,
                     Grams wanted);

}  // namespace core

#endif  // CORE_CONSTRUCTION_SITE_SUPPLY_H_
