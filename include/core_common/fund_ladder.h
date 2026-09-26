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
#include <cstdint>
#include <span>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

struct FieldRow;
struct WorldState;

/// @brief SeedNorm::sow_from_month of a crop whose sowing month is not known.
inline constexpr std::uint8_t kNoSowingMonth = 0xFF;

/// @brief What one crop's sowing takes out of the stores.
struct SeedNorm {
  ResourceId resource;  ///< What the seed of this crop is.

  float sowing_norm_kg_per_ha = 0.0F;  ///< 0 = the crop needs no seed stock.

  /// Sown in the autumn before the year it is reaped (crops.csv `is_winter`):
  /// its seed is owed from the autumn of the year BEFORE its slot.
  bool is_winter = false;

  /// The LAST month of its sowing window (crops.csv `sow_to_month`, 0-based),
  /// or kNoSowingMonth: the latest the sowing takes held seed — the horizon
  /// of its rot margin (family_exchange.cpp). Not the window's first month:
  /// inside the window that horizon is nought while the seed waits weeks for
  /// the crew (plan700, seed 1937 year 14, 0.34.42's first draft).
  std::uint8_t sow_to_month = kNoSowingMonth;
};

/// @brief The crop a field's NEXT sowing puts in, read off its rotation and
///        its state today: the first slot until it is sown, reaped or given
///        up for the second slot's winter crop; then the second; and the
///        third once the second is already in the ground (a winter crop sown
///        this autumn). A chain the turn will hold still
///        (FieldRow::rotation_skips_turn) sows its first slot next, unless the
///        second slot's crop is already being worked. A fallow first slot
///        passes to the second; invalid
///        when the slot it lands on is fallow, or there is no rotation.
///
/// THE SLOTS ARE SHIFTED AT EVERY YEAR'S TURN (production_system.cpp), so
/// the slot is chosen by the field's state and never by the year's number —
/// the seed alarm indexed `(year + 1) % 3` over shifted slots and named the
/// wrong crop two years in three (static review, 2026-09-24).
///
/// A WINTER SLOT LOST TO ITS WINDOW (question 278, WinterSlotLost) is a
/// fallow year: its next sowing is the second slot's (0.36.13).
/// @param year0_is_winter Whether `rotation_year0` names a winter crop — the
///        caller's crop table says. Required, so no caller forgets it.
CropId NextSowingCrop(const FieldRow& field, SimDay today, bool year0_is_winter);

/// @brief The plan rung of one resource before any unsealing: what is owed —
///        `plan.due` less `plan.delivered` — as far as the crop lies
///        unreserved in the stores ABOVE what the rungs over it hold
///        (`held_above`: the seed fund's grams of the same resource, which
///        come first), carry-over and this year's reaping alike. Never below
///        nought.
/// @param held_above Grams of the resource the seed rung holds, after its own
///        unsealing; 0 when asked alone.
/// @param index A ResourceId value; past the plan's end the rung is 0.
/// @param carted_daily The position the milk cart carries daily: never held,
///        its share leaves at the milking. Invalid when there is none.
Grams PlanRungGrams(const WorldState& world,
                    std::size_t index,
                    ResourceId carted_daily = ResourceId{},
                    Grams held_above = 0);

/// @brief Rung 1 alone: grams of each resource the seed fund holds for the
///        sowings still to come (see HeldAboveFodder, SEED), less what the
///        chairman has unsealed of the SEED fund, never below nought.
/// @return Dense by ResourceId, sized `resource_count`.
ResourceAmounts SeedRungLeft(const WorldState& world,
                             std::span<const SeedNorm> seed_norms_by_crop,
                             std::size_t resource_count);

/// @brief Grams of each resource held by the seed fund and the plan reserve
///        together, less whatever the chairman has unsealed.
///
/// SEED: the sowing norm of every field that has yet to be sown this year's
/// first slot — idle, ploughing, harrowing or sowing, and not yet growing or
/// being reaped. AND the autumn's winter crop of the second slot, on a field
/// whose first slot is done with — reaped this year, a fallow year, or given
/// up past its window and worked for the winter crop instead — until
/// that winter crop is in the ground. Skipped entirely when
/// `reserve_seed_fund` is false.
///
/// PLAN: what is owed, as far as the crop lies in the stores (PlanRungGrams;
/// boss, boss-core-epoch1-4 seq 10). It replaced, on 0.34.42, "as much as
/// this year's reaping has covered" (boss, 2026-09-12: in April there is
/// nothing yet to set aside, and
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
                                bool reserve_seed_fund,
                                ResourceId carted_daily = ResourceId{});

/// @brief Adds to `reserve` what the top two rungs will lose to rot before
///        they are used, EACH TO ITS OWN DAY: the plan's part to the year's
///        turn, the seed's part to the end of its crop's sowing window
///        (SeedNorm::sow_to_month; the latest of the crops of one seed).
///        spoilage.h's RotMarginGrams on each part, for a resource that
///        keeps more than a day.
///
/// ONE HOME, since 0.35.11, for the people's issue (family_exchange.cpp,
/// IssueReserve) and the herds' feed (herd_system.cpp, FeedAllowance). The
/// herds held the rungs with no margin, ate down to them, and the store's
/// rot took the rest out of the seed: seed 1931 on branch E3 sowed its
/// spring wheat on 2462 kg of 2520 (econ-boss-first-harvest seq 16).
/// @param seed_and_plan The two rungs as they stand, by ResourceId.
/// @param seed_part The seed rung's part of them (SeedRungLeft).
/// @param spoil_days Days each resource keeps, by ResourceId (resources.csv
///        spoil_days); a missing or zero entry keeps.
/// @param keeping_factor The stores' factor on spoil_days (spoilage.h).
/// @param reserve Added to (`+=`), never cleared: what the caller holds back.
void AddRungRotMargins(const WorldState& world,
                       std::span<const SeedNorm> seed_norms_by_crop,
                       const ResourceAmounts& seed_and_plan,
                       const ResourceAmounts& seed_part,
                       std::span<const float> spoil_days,
                       float keeping_factor,
                       ResourceAmounts& reserve);

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
