/// @file
/// @brief Implementation of fund_ladder.h.

#include "core_common/fund_ladder.h"

#include <algorithm>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/spoilage.h"
#include "core_common/unit_state.h"
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

/// Adds one field's sowing of `crop` to `seed`; a crop past the norms' end,
/// one that needs no seed, or — when `winter_only` — a spring crop adds none.
void AddSowing(std::span<const SeedNorm> seed_norms_by_crop,
               CropId crop,
               float area_ha,
               bool winter_only,
               ResourceAmounts& seed) {
  if (crop.value >= seed_norms_by_crop.size()) {
    return;
  }
  const SeedNorm& norm = seed_norms_by_crop[crop.value];
  if ((winter_only && !norm.is_winter) || norm.resource.value >= seed.size() ||
      norm.sowing_norm_kg_per_ha <= 0.0F) {
    return;
  }
  seed[norm.resource.value] += GramsFromKilograms(norm.sowing_norm_kg_per_ha * area_ha);
}

}  // namespace

Grams PlanRungGrams(const WorldState& world,
                    std::size_t index,
                    ResourceId carted_daily,
                    Grams held_above) {
  // THE CART'S POSITION IS NEVER SEALED: its share leaves at the milking
  // (the milk cart; boss seq 113). Under the old rung milk was held by
  // nothing only because no reaping books milk — a rule by accident; under
  // the stores' own rule it would be held in the dairy, from the issue.
  if (carted_daily.value != kInvalidDefIdValue && index == carted_daily.value) {
    return 0;
  }
  // WHAT IS OWED: this year's due, less what went to the district early
  // (kDeliverPlan, boss seq 25 item 3 — rye shipped in August no longer
  // holds the issue and the theft all autumn).
  //
  // NO WINDOW "BEFORE THE ANNOUNCEMENT". The first draft of 0.34.42 held last
  // year's due from the turn to the spring's figure (labor payment §7, «до
  // объявления плана»), and the static review found it could never run: the
  // district's letter comes in January, in the same tick as the judge that
  // clears the old figure (production_system.cpp), and no reader of the
  // ladder stands between them. A rule that cannot fire is a stub; it went.
  const Grams due = index < world.plan.due.size() ? world.plan.due[index] : 0;
  const Grams sent = index < world.plan.delivered.size() ? world.plan.delivered[index] : 0;
  const Grams owed = due > sent ? due - sent : 0;
  if (owed <= 0) {
    return 0;
  }
  // AS FAR AS THE CROP LIES IN THE STORES — the carry-over and this year's
  // reaping alike (boss, boss-core-epoch1-4 seq 9 and 10). Until 0.34.42
  // the rung was min(owed, gathered THIS year): nought from the turn to the
  // first reaping whatever was carried over, so the plan's crop was held
  // from the issue by PlanHoldsIt alone, whole — hunger beside full barns
  // (258 hungry episodes on 0.34.40, econ and host). Boss's formula kept the
  // reaping after it ("после жатвы — по собранному"); one rule for the whole
  // year is taken instead, because that one lets the carry-over go on the
  // bad year's first reaping — 500 kg reaped against 1000 owed and 800
  // carried would hold 500 and hand 300 of the plan's grain out.
  //
  // AND BELOW THE RUNGS ABOVE IT (resources design §6: the seed fund is rung
  // 1, the plan rung 2, «при нехватке первым страдает нижний»). Capped at all
  // that lies, the plan and the seed counted the same grain twice: 1000 kg
  // lying, 400 of winter seed, 1000 owed held 1400 — and unsealing 300 of the
  // plan freed nothing (static review of 0.34.42).
  const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
  Grams lying = 0;
  for (const UnitRow& unit : world.units.rows) {
    lying += UnreservedOf(unit, resource);
  }
  const Grams below_the_seed = lying > held_above ? lying - held_above : 0;
  return owed < below_the_seed ? owed : below_the_seed;
}

CropId NextSowingCrop(const FieldRow& field, SimDay today) {
  const bool in_ground = field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
  const bool reaped_this_year = field.reaped_day != kNeverReapedDay &&
                                field.reaped_day / kDaysPerYear == today / kDaysPerYear;
  const bool crop_named = field.crop.value != kInvalidDefIdValue;
  // The second slot's crop already in: a winter crop sown this autumn — told
  // from this year's own crop by the slot it names, or, where both slots
  // name the same crop, by this year's reaping having come first.
  if (in_ground && crop_named && field.crop.value == field.rotation_year1.value &&
      (field.crop.value != field.rotation_year0.value || reaped_this_year)) {
    return field.rotation_year2;
  }
  // Worked for the second slot's crop: the first slot gave up its window.
  // Where both slots name one crop the work is the first slot's own.
  const bool preparing_second_slot = !in_ground && crop_named &&
                                     field.crop.value == field.rotation_year1.value &&
                                     field.crop.value != field.rotation_year0.value;
  // A CHAIN NAMED AFTER THIS YEAR'S SOWING STANDS STILL AT THE TURN
  // (land_state.h, rotation_skips_turn): the first slot the chairman named is
  // the one sown next, whatever this year's reaping or standing crop says.
  // A chain named in August as (oats, winter rye) sows the oats first since
  // 0.36.10 (land_state.h): the field waits for them rather than work for the
  // rye, so this names the oats and is right. Before, the rye went in that
  // September and this answer was wrong from its ploughing to the turn.
  if (field.rotation_skips_turn != 0 && !preparing_second_slot) {
    return field.rotation_year0.value != kInvalidDefIdValue ? field.rotation_year0
                                                            : field.rotation_year1;
  }
  // A fallow first slot sows nothing: the next sowing is the second slot's.
  if (!in_ground && !reaped_this_year && !preparing_second_slot &&
      field.rotation_year0.value != kInvalidDefIdValue) {
    return field.rotation_year0;
  }
  return field.rotation_year1;
}

ResourceAmounts SeedRungLeft(const WorldState& world,
                             std::span<const SeedNorm> seed_norms_by_crop,
                             std::size_t resource_count) {
  ResourceAmounts seed(resource_count, 0);
  {
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
      // A FIELD WHOSE FIRST SLOT MISSED ITS WINDOW and is being worked for the
      // second slot's winter crop (TrySow hands it to TrySowWinter): the
      // first slot will not be sown this year, and its seed is owed nobody
      // (static review of the change, 2026-09-24). Where both slots name one
      // crop the work is the first slot's own, and it stays owed above — a
      // spring crop in both slots is not owed below at all, which is how
      // 0.34.36, without this test, held no oats through their own ploughing.
      const bool preparing_second_slot = !already_sown && field.crop.value != kInvalidDefIdValue &&
                                         field.crop.value == field.rotation_year1.value &&
                                         field.crop.value != field.rotation_year0.value;
      if (!already_sown && !reaped_this_year && !preparing_second_slot) {
        AddSowing(seed_norms_by_crop, field.rotation_year0, field.area_ga, false, seed);
      }
      // AND THE AUTUMN'S WINTER CROP IS NEXT YEAR'S SOWING TOO (boss seq 18,
      // econ plan-700 §3). The rye sown in September is the seed fund's own
      // "сев следующего года" (resources design §6), but the rung above
      // read only the first slot and let the reaped field go — so from the
      // reaping to the sowing nothing held that seed, the ration ate it, and
      // the sowing took its 1.89 t out of the plan's rye (seed 1934, year 23).
      // Owed once the first slot is done with — reaped this year, a fallow
      // year, or given up past its window for the winter crop — and until the
      // winter crop is in the ground.
      const bool first_slot_done = reaped_this_year ||
                                   field.rotation_year0.value == kInvalidDefIdValue ||
                                   preparing_second_slot;
      const bool winter_sown = already_sown && field.crop.value == field.rotation_year1.value;
      if (first_slot_done && !winter_sown) {
        AddSowing(seed_norms_by_crop, field.rotation_year1, field.area_ga, true, seed);
      }
    }
  }
  for (std::size_t index = 0; index < resource_count; ++index) {
    seed[index] = RungLeft(seed[index], Unsealed(world, FundKind::kSeed, index));
  }
  return seed;
}

ResourceAmounts HeldAboveFodder(const WorldState& world,
                                std::span<const SeedNorm> seed_norms_by_crop,
                                std::size_t resource_count,
                                bool reserve_seed_fund,
                                ResourceId carted_daily) {
  const ResourceAmounts seed = reserve_seed_fund
                                   ? SeedRungLeft(world, seed_norms_by_crop, resource_count)
                                   : ResourceAmounts(resource_count, 0);
  ResourceAmounts held(resource_count, 0);
  for (std::size_t index = 0; index < resource_count; ++index) {
    const Grams seed_left = seed[index];
    const Grams plan = PlanRungGrams(world, index, carted_daily, seed_left);
    // EACH FUND OPENS ITS OWN RUNG (boss, boss-core-epoch1-resume seq 14,
    // answer 3). Until 0.34.17 every release came off one total of both
    // rungs — "which fund was opened is which risk was taken, not which share
    // the subtraction comes from" — and that total is how unsealing the
    // FODDER fund opened the plan's oats (host, econ-host-fodder-and-winter
    // seq 2): the fodder had no rung here to come off, so it came off the
    // plan's. A release now empties only the rung it names.
    held[index] = seed_left + RungLeft(plan, Unsealed(world, FundKind::kPlanReserve, index));
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

namespace {

/// Days from today to the end of the latest sowing window among the crops
/// whose seed is `index`, never past `days_left` (the year's turn); the turn
/// itself for a resource no crop sows. To the END of the window's last month:
/// this month counts whole. Moved here from family_exchange.cpp (0.35.11).
std::uint32_t SeedHorizonDays(std::span<const SeedNorm> seed_norms,
                              const WorldState& world,
                              std::uint32_t index,
                              std::uint32_t days_left) {
  const auto today = static_cast<std::uint32_t>(world.calendar.date.month);
  std::uint32_t latest = 0;
  bool any = false;
  for (const SeedNorm& norm : seed_norms) {
    if (norm.resource.value != index || norm.sow_to_month == kNoSowingMonth) {
      continue;
    }
    const std::uint32_t months =
        ((norm.sow_to_month + kMonthsPerYear - today) % kMonthsPerYear) + 1U;
    latest = std::max(latest, months * kDaysPerMonth);
    any = true;
  }
  return any ? std::min(latest, days_left) : days_left;
}

}  // namespace

void AddRungRotMargins(const WorldState& world,
                       std::span<const SeedNorm> seed_norms_by_crop,
                       const ResourceAmounts& seed_and_plan,
                       const ResourceAmounts& seed_part,
                       std::span<const float> spoil_days,
                       float keeping_factor,
                       ResourceAmounts& reserve) {
  const std::uint32_t days_left = kDaysPerYear - (world.calendar.day % kDaysPerYear);
  for (std::uint32_t index = 0; index < seed_and_plan.size() && index < reserve.size(); ++index) {
    const float days = index < spoil_days.size() ? spoil_days[index] * keeping_factor : 0.0F;
    if (!(days > 1.0F)) {
      continue;
    }
    const Grams seed =
        index < seed_part.size() ? std::min(seed_part[index], seed_and_plan[index]) : 0;
    const Grams plan = seed_and_plan[index] - seed;
    if (plan > 0) {
      reserve[index] += RotMarginGrams(plan, days, days_left);
    }
    if (seed > 0) {
      reserve[index] +=
          RotMarginGrams(seed, days, SeedHorizonDays(seed_norms_by_crop, world, index, days_left));
    }
  }
}

}  // namespace core
