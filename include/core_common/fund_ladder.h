/// @file
/// @brief The top two rungs of the store's ladder of funds, computed in one
///        place for the two modules that must stay below them.
/// @threading PARALLEL_READONLY
/// A pure function of the world it is given: it reads the fields, the plan,
/// the year's harvest book and the unsealings, and writes nothing. Its
/// callers run it in the sequential decisions slot and, for the ration
/// alarm (LockedRationFood), between steps on the sim thread.
///
/// Resources design §6: "Урожай расписан по фондам. Наполняются в порядке
/// приоритета, и при нехватке первым страдает нижний" — 1 the seed fund,
/// 2 the plan reserve, 3 the fodder claim, 4 the kolkhoz fund. Two modules
/// spend the stores below rung 2: core_residents hands out the kolkhoz fund
/// (rung 4) and core_production feeds the herds (rung 3). Until 2026-09-13
/// only the first knew the top of the ladder existed — the herds took their
/// feed straight out of the stores, so in a year without an oat harvest the
/// horses ate the grain owed to the district and the plan fell short, which
/// is the opposite of the ladder's own sentence about that rung: "в плохой
/// год район забирает первым, и лошадь худеет раньше, чем срывается сдача".
/// Shared here for the reason haul.h and spoilage.h are: a rule with two homes
/// grows two answers.

#ifndef CORE_COMMON_FUND_LADDER_H_
#define CORE_COMMON_FUND_LADDER_H_

#include <cstddef>
#include <span>

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

struct WorldState;

/// @brief What one crop's sowing takes out of the stores.
struct SeedNorm {
  ResourceId resource;  ///< What the seed of this crop is.

  float sowing_norm_kg_per_ha = 0.0F;  ///< 0 = the crop needs no seed stock.
};

/// @brief Grams of each resource held by the seed fund and the plan reserve
///        together, less whatever the chairman has unsealed.
///
/// SEED: the sowing norm of every field that has yet to be sown this year's
/// first slot — idle, ploughing, harrowing or sowing, and not yet growing or
/// being reaped. Skipped entirely when `reserve_seed_fund` is false.
///
/// PLAN: as much of `plan.due` as this year's reaping has covered so far, and
/// no more (boss, 2026-09-12: in April there is nothing yet to set aside, and
/// reserving the whole norm from January starves the spring beside grain it
/// may not touch).
///
/// UNSEALED: EACH FUND OPENS ITS OWN RUNG, clamped at zero per resource —
/// the seed fund's release comes off the seed rung, the plan reserve's off
/// the plan rung, and the fodder fund's off neither (FodderRungLeft). Until
/// 0.34.17 every release came off one total of the two, and unsealing the
/// fodder fund opened the plan's oats (boss seq 14, answer 3).
///
/// @param seed_norms_by_crop Dense by CropId; a crop past its end needs no seed.
/// @param resource_count Size of the returned vector.
/// @return Dense by ResourceId, sized `resource_count`.
ResourceAmounts HeldAboveFodder(const WorldState& world,
                                std::span<const SeedNorm> seed_norms_by_crop,
                                std::size_t resource_count,
                                bool reserve_seed_fund);

/// @brief Rung 3 as the people's issue must stay below it: THE FODDER CLAIM,
///        AND INSIDE IT THE FODDER FUND (resources design §6; boss seq 17) —
///        per resource the larger of last year's feed of the kolkhoz's herds
///        and the working stock's fund, less what the chairman has unsealed
///        of the FODDER fund, never below nought.
///
/// Last year's feed holds every herd's feed from the people — a cow's oats
/// do not go to the table. The fund is the team's ration until the next
/// reaping of a work feed, and it is what keeps the winter decision alive in
/// the first year, which has no closed book. Both are sized elsewhere and
/// arrive as numbers: the book by the ledger, the fund by production
/// (FodderClaimGrams). What the ladder owns is the composition and the rule
/// that a fodder release opens this rung and no other.
/// @param last_year_feed Dense by ResourceId: `ledger.closed.feed`.
/// @param fodder_fund Dense by ResourceId: the fund's size today, 0 for a
///        resource that is no work feed.
/// @return Dense by ResourceId, the longer of the two sizes.
ResourceAmounts FodderRungLeft(const WorldState& world,
                               const ResourceAmounts& last_year_feed,
                               const ResourceAmounts& fodder_fund);

}  // namespace core

#endif  // CORE_COMMON_FUND_LADDER_H_
