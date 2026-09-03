/// @file
/// @brief The one-time turn of the start canon: the day the kolkhoz horses
/// come in off the private yards.
/// @threading SINGLE_THREADED
/// Runs at the head of the herd day, in the production sub-step of the
/// decisions slot (phase 3), from the sim thread. It CHANGES THE SHAPE of
/// the herd table — merges rows and removes the emptied ones — which is
/// legal only in a sequential slot (core rules §10, buffer-law rule 6).
///
/// Model: manual/74-posts.md §5; livestock design §5 ("the groom was not
/// appointed — nothing happened"). It lives in its own file rather than
/// inside herd_system.cpp because it is not part of the herd DAY: feeding,
/// produce, offspring and slaughter happen every day of the campaign, and
/// this happens once in it.
#ifndef CORE_PRODUCTION_STABLE_HORSES_H_
#define CORE_PRODUCTION_STABLE_HORSES_H_

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Gathers every kolkhoz horse herd into one at the yard whose
/// groom's post is held, sets ChairmanState::horses_stabled and raises
/// kHorsesStabled — or does nothing at all.
///
/// Does nothing when the milestone is already set, when the tables name no
/// groom or no horse, or when no BUILT unit has a groom in place. Idempotent
/// by construction: after it runs, no kolkhoz horse stands at a household,
/// so a second call finds nothing to gather.
///
/// @pre Called once per day boundary, from the sequential decisions slot,
///      BEFORE billeting — a horse that arrives today needs a place under
///      the roof today, and billeting is what counts the places.
/// @post The sire count of the merged herd is NOT set here: it is a herd
///       invariant re-derived by the walk that follows in RunHerdDay.
void StableHorses(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_STABLE_HORSES_H_
