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
#include <vector>

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

  /// The months its reaping runs (crops.csv `harvest_from_month`,
  /// `harvest_to_month`, 0-based), or kNoSowingMonth when the table names
  /// none: a harvest of its seed, which comes before a later sowing and
  /// gives it (SeedHeldByField).
  std::uint8_t harvest_from_month = kNoSowingMonth;
  std::uint8_t harvest_to_month = kNoSowingMonth;
};

/// @brief The seed held for the next sowings, with its parts: the grams held
///        for each field row (0 when its sowing is not held) and the seed
///        they are of.
struct SeedHold {
  ResourceAmounts by_resource;          ///< Dense by ResourceId.
  std::vector<Grams> by_field_row;      ///< By row of world.fields.
  std::vector<ResourceId> seed_of_row;  ///< The seed each held row is of.
};

/// @brief THE SEED FUND'S ONE RULE (resources design §6, «сев следующего
///        года»; 0.36.21 field by field, one door since 0.36.34): the norm of
///        each arable field whose own next sowing (NextSowingOf, by the slot
///        it comes from) ends before the seed's next harvest begins. A sowing
///        the harvest comes first to holds nothing today — the winter rye,
///        reaped in July and sown in September, and a chain's potato of NEXT
///        year, which this August's digging gives. The seed's next harvest is
///        read off the FIELDS — a crop in the ground, a sowing to come — and a
///        seed nothing will reap holds its sowings.
///
///        READ BY EVERY DOOR THAT KEEPS SEED: the fund ladder's seed rung
///        (SeedRungLeft, what the ration, the families' exchange and the
///        herds may not take), and core_production's delivery door, seed
///        alarm, goods loan and plan forecast (seed_room.h). Until 0.36.34 the
///        rung had a rule of its own — this year's sowings still to come —
///        and before the turn held next spring's potato on the fields dug that
///        year for nobody (boss-core-seed-ladders [1]-[2]).
/// @param as_of The day the rotation slots describe. At the year's turn the
///        calendar is the new year's and the slots are still the old year's:
///        the turn's callers pass the closing year's last day.
/// @param resource_count The length of `by_resource`.
SeedHold SeedHeldByField(const WorldState& world,
                         std::span<const SeedNorm> seed_norms_by_crop,
                         std::size_t resource_count,
                         SimDay as_of);

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

/// @brief NextSowingCrop's answer with the SLOT it came from: 0 this year's
///        crop, 1 next year's, 2 the year after's — the slot names the year
///        the crop is REAPED, so a winter crop of slot k is sown in the
///        autumn of year k − 1 (0.36.21: the seed held for the plan counts a
///        field's sowing by it, boss-core-epoch1-resume [35]).
struct NextSowing {
  CropId crop;
  std::uint8_t slot = 0;

  /// The answer came from a chain the turn will hold still
  /// (FieldRow::rotation_skips_turn): its first slot is sown at the NEXT
  /// window of its crop from today — this autumn for a fresh winter crop,
  /// next spring for a spring crop named after its window — whatever `slot`
  /// says (static review of 0.36.21).
  bool held_chain = false;
};

/// @brief NextSowingCrop's answer with the slot it came from and whether a
///        held chain gave it (NextSowing). The same reading of the field's
///        state; NextSowingCrop returns its crop.
NextSowing NextSowingOf(const FieldRow& field, SimDay today, bool year0_is_winter);

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

/// @brief Rung 1 alone: SeedHeldByField as of today, less what the chairman
///        has unsealed of the SEED fund, never below nought.
/// @return Dense by ResourceId, sized `resource_count`.
ResourceAmounts SeedRungLeft(const WorldState& world,
                             std::span<const SeedNorm> seed_norms_by_crop,
                             std::size_t resource_count);

/// @brief Grams of each resource held by the seed fund and the plan reserve
///        together, less whatever the chairman has unsealed.
///
/// SEED: SeedRungLeft — every field's next sowing that ends before its seed's
/// next harvest (SeedHeldByField). Skipped entirely when `reserve_seed_fund`
/// is false.
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
