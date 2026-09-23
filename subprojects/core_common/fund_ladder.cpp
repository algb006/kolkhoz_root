/// @file
/// @brief Implementation of fund_ladder.h.

#include "core_common/fund_ladder.h"

#include <algorithm>
#include <cstdint>

#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"

namespace core {

namespace {

/// What the chairman has unsealed of one fund for one resource.
Grams Unsealed(const WorldState& world, FundKind fund, std::size_t index) {
  const auto slot = static_cast<std::size_t>(fund);
  if (slot >= world.unsealed.by_fund.size()) {
    return 0;
  }
  const ResourceAmounts& opened = world.unsealed.by_fund[slot];
  return index < opened.size() ? opened[index] : 0;
}

/// One rung less what was unsealed of IT, never below nought.
Grams RungLeft(Grams held, Grams unsealed) {
  return held > unsealed ? held - unsealed : 0;
}

}  // namespace

ResourceAmounts HeldAboveFodder(const WorldState& world,
                                std::span<const SeedNorm> seed_norms_by_crop,
                                std::size_t resource_count,
                                bool reserve_seed_fund) {
  ResourceAmounts seed(resource_count, 0);
  if (reserve_seed_fund) {
    for (const FieldRow& field : world.fields.rows) {
      // UNTIL THE SOWING TAKES IT, not until the ploughing starts: a field is
      // done needing seed once the crop is in the ground (69-reconciliation.md
      // §13.16 — the fund used to guard a phase of the field rather than a
      // quantity of grain).
      const bool already_sown =
          field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
      // AND NOT ONCE THE REAPING HAS TAKEN IT (2026-09-15, boss parcel 421): a
      // reaped field is idle again and still names this year's crop until the
      // year's turn, so the fund held seed for a sowing already sown and
      // reaped — 52.5 t of potatoes from the October digging to New Year.
      const bool reaped_this_year =
          field.reaped_day != kNeverReapedDay &&
          field.reaped_day / kDaysPerYear == world.calendar.day / kDaysPerYear;
      if (already_sown || reaped_this_year ||
          field.rotation_year0.value >= seed_norms_by_crop.size()) {
        continue;
      }
      const SeedNorm& norm = seed_norms_by_crop[field.rotation_year0.value];
      if (norm.resource.value >= seed.size() || norm.sowing_norm_kg_per_ha <= 0.0F) {
        continue;
      }
      seed[norm.resource.value] += GramsFromKilograms(norm.sowing_norm_kg_per_ha * field.area_ga);
    }
  }
  // THE PLAN RESERVE IS FILLED BY THE HARVEST, NOT BY THE CALENDAR.
  const ResourceAmounts& reaped = world.ledger.current.harvest;
  ResourceAmounts held(resource_count, 0);
  for (std::size_t index = 0; index < resource_count; ++index) {
    Grams plan = 0;
    if (index < world.plan.due.size()) {
      const Grams owed = world.plan.due[index];
      const Grams gathered = index < reaped.size() ? reaped[index] : 0;
      plan = owed < gathered ? owed : gathered;
    }
    // EACH FUND OPENS ITS OWN RUNG (boss, boss-core-epoch1-resume seq 14,
    // answer 3). Until 0.34.17 every release came off one total of both
    // rungs — "which fund was opened is which risk was taken, not which share
    // the subtraction comes from" — and that total is how unsealing the
    // FODDER fund opened the plan's oats (host, econ-host-fodder-and-winter
    // seq 2): the fodder had no rung here to come off, so it came off the
    // plan's. A release now empties only the rung it names.
    held[index] = RungLeft(seed[index], Unsealed(world, FundKind::kSeed, index)) +
                  RungLeft(plan, Unsealed(world, FundKind::kPlanReserve, index));
  }
  return held;
}

ResourceAmounts FodderRungLeft(const WorldState& world,
                               const ResourceAmounts& last_year_feed,
                               const ResourceAmounts& fodder_fund) {
  const std::size_t count = std::max(last_year_feed.size(), fodder_fund.size());
  ResourceAmounts left(count, 0);
  for (std::size_t index = 0; index < count; ++index) {
    const Grams claim = index < last_year_feed.size() ? last_year_feed[index] : 0;
    const Grams fund = index < fodder_fund.size() ? fodder_fund[index] : 0;
    // THE FUND INSIDE THE CLAIM (boss seq 17): the larger of the two is held,
    // and a fodder release comes off THAT. Off the fund alone it would free
    // nothing whenever last year's feed — which already holds the team's
    // oats — was the larger, and the chairman's winter decision would be the
    // button without a wire host found, only moved.
    left[index] = RungLeft(std::max(claim, fund), Unsealed(world, FundKind::kFodder, index));
  }
  return left;
}

}  // namespace core
