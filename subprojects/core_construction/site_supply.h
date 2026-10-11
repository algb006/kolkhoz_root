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

/// @brief The largest single give of a taking so far and where it lies.
///        Carried across the lines of one rehearsal (a recipe's materials):
///        the supply estimate asked before a start prices the whole load from
///        this one place (supply_estimate.h — an estimate, the works' own
///        legs are written by the fixing).
struct LargestGiver {
  Grams grams = 0;
  Vec2 position;
};

/// @brief FIXES up to `wanted` grams of `resource` for the works of
///        `site_row` (routing stage V2 with the direct leg). WHAT LIES FELLED
///        AT A STAND OR DUG AT A PIT FIRST — taken off its lying load, its own
///        carting seam shrunk by the same share: it goes straight to the site
///        and is nobody else's from this call (not the saw's, not another
///        works', not the pile's). Then the standing units' unreserved stock,
///        the givers TakeFromStores draws on. Each group the NEAREST first, by
///        the straight line, then by row. All of it is put into the site's
///        ConstructionState::awaiting and into the leg of its sender
///        (ConstructionState::legs), where the carts take it from: the fixing
///        of construction design §6 is this call, the carting is production's.
/// @param log_resource The resource a stand's load is (ConstructionConfig::
///        log_resource); invalid in a set without timber — no stand gives.
/// @param giver Raised to the largest single give of this call when it
///        exceeds what it holds.
/// @return The grams fixed.
/// @note Side effects: stocks of the giving units, `load_grams` and the
///       carting seams of the giving stands and pits, the site's works.
Grams TakeForSite(WorldState& current,
                  std::uint32_t site_row,
                  ResourceId resource,
                  Grams wanted,
                  ResourceId log_resource,
                  LargestGiver& giver);

/// @brief TakeForSite's answer with nothing touched: what a fixing of `wanted`
///        grams would take today and who would give the most of it — the
///        same givers in the same order (one rule, site_supply.cpp). For the
///        supply estimate asked before a start (supply_estimate.h).
Grams RehearseTakeForSite(const WorldState& world,
                          std::uint32_t site_row,
                          ResourceId resource,
                          Grams wanted,
                          ResourceId log_resource,
                          LargestGiver& giver);

/// @brief Writes the works' sender and way from its front leg
///        (ConstructionState::supply_from, supply_log_way). No leg: both keep
///        what they had.
void SyncFrontLeg(ConstructionState& works);

/// @brief Writes the day the works' wait began (ConstructionState::
///        supply_since_day): `today` when a taking gave the works something
///        to await while nothing awaited before it. A taking that added to
///        what already awaited, or found nothing, keeps the day it had — the
///        supply's term runs from the first of the wait, not from its last
///        addition.
void StampTheWait(UnitRow& site, bool awaited_before, SimDay today);

/// @brief What the works hold of `resource` in all: on the site and awaiting.
Grams FixedForSite(const UnitRow& site, ResourceId resource);

/// @brief Ends the carting of a works that is over or taken down: whatever
///        still awaits is the unit's own stock now — nothing fixed is lost
///        with the block — and the supply seam is closed. STUB core, named:
///        what awaited at a stand or a pit lands with the rest, its last
///        journey unpaid; it is the end of a works, not a road for goods.
void FoldAwaitingIntoStock(UnitRow& site);

}  // namespace core

#endif  // CORE_CONSTRUCTION_SITE_SUPPLY_H_
