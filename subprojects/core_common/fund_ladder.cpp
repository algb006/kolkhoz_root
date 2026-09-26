/// @file
/// @brief Implementation of fund_ladder.h.

#include "core_common/fund_ladder.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

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

CropId NextSowingCrop(const FieldRow& field, SimDay today, bool year0_is_winter) {
  return NextSowingOf(field, today, year0_is_winter).crop;
}

NextSowing NextSowingOf(const FieldRow& field, SimDay today, bool year0_is_winter) {
  const bool in_ground = field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
  const bool reaped_this_year = field.reaped_day != kNeverReapedDay &&
                                field.reaped_day / kDaysPerYear == today / kDaysPerYear;
  const bool crop_named = field.crop.value != kInvalidDefIdValue;
  // The second slot's crop already in: a winter crop sown this autumn — told
  // from this year's own crop by the slot it names, or, where both slots
  // name the same crop, by this year's reaping having come first.
  if (in_ground && crop_named && field.crop.value == field.rotation_year1.value &&
      (field.crop.value != field.rotation_year0.value || reaped_this_year)) {
    return NextSowing{.crop = field.rotation_year2, .slot = 2};
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
  // AND A FRESH CHAIN'S WINTER CROP NAMED FIRST, ALREADY IN THE GROUND, keeps
  // the mark until the turn into its year (0.36.10, field_work.cpp): the first
  // slot is sown, so the next sowing is the second slot's.
  const bool first_slot_in_ground =
      in_ground && crop_named && field.crop.value == field.rotation_year0.value;
  // A HELD CHAIN SAYS SO (NextSowing::held_chain): its first slot is sown at
  // the next window of its crop — this autumn for a fresh winter crop, next
  // spring for a spring crop named after its window — and the slot's number
  // does not date it (static review of 0.36.21: read as this year's, both
  // lost their seed).
  if (field.rotation_skips_turn != 0 && !preparing_second_slot && !first_slot_in_ground) {
    return field.rotation_year0.value != kInvalidDefIdValue
               ? NextSowing{.crop = field.rotation_year0, .slot = 0, .held_chain = true}
               : NextSowing{.crop = field.rotation_year1, .slot = 1, .held_chain = true};
  }
  // A fallow first slot sows nothing: the next sowing is the second slot's —
  // and so does a winter slot lost to its window (question 278; 0.36.13).
  const bool first_slot_lost = WinterSlotLost(field, year0_is_winter, today);
  if (!in_ground && !reaped_this_year && !preparing_second_slot && !first_slot_lost &&
      field.rotation_year0.value != kInvalidDefIdValue) {
    return NextSowing{.crop = field.rotation_year0, .slot = 0};
  }
  return NextSowing{.crop = field.rotation_year1, .slot = 1};
}

SeedHold SeedHeldByField(const WorldState& world,
                         std::span<const SeedNorm> seed_norms_by_crop,
                         std::size_t resource_count,
                         SimDay as_of) {
  SeedHold hold;
  hold.by_resource.assign(resource_count, 0);
  hold.by_field_row.assign(world.fields.rows.size(), 0);
  hold.seed_of_row.assign(world.fields.rows.size(), ResourceId{});
  ResourceAmounts& held = hold.by_resource;
  const auto month = static_cast<std::int32_t>((as_of % kDaysPerYear) / kDaysPerMonth);
  constexpr auto kYear = static_cast<std::int32_t>(kMonthsPerYear);
  constexpr std::int32_t kNever = std::numeric_limits<std::int32_t>::max();
  const auto months_to = [month](std::uint8_t target) {
    return (static_cast<std::int32_t>(target) - month + kYear) % kYear;
  };

  // THE SOWINGS FIELD BY FIELD (0.36.21; boss-core-epoch1-resume [35]): each
  // arable field's next sowing, in months from `as_of` to its window's end —
  // by the slot it comes from (a winter crop of slot k is sown in the autumn
  // of year k − 1), this month counting whole as DaysToSowingEnd counts it;
  // a held chain at its crop's next window (NextSowing::held_chain). And the
  // month that sowing is reaped, which is a harvest of its seed too.
  struct Sowing {
    const SeedNorm* norm = nullptr;
    std::uint32_t row = 0;
    std::int32_t sow_end = 0;  // months; <= 0 — its window gone
  };

  std::vector<Sowing> sowings;
  // THE NEXT HARVEST OF EACH SEED, from `as_of`, in months — off the FIELDS,
  // not off the crop table (static review of 0.36.21): a crop in the ground
  // (nought while in its reaping months), and a sowing to come, reaped in its
  // slot's year. A seed nothing will reap holds its sowings whatever the
  // calendar says: in a year with no field of oats, next year's oat seed is
  // not "given by August's oats". A crop whose table names no reaping month
  // gives no harvest to wait for.
  std::vector<std::int32_t> harvest_in(held.size(), kNever);
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const FieldRow& field = world.fields.rows[row];
    if (field.kind != LandKind::kArable) {
      continue;
    }
    const bool in_ground =
        (field.phase == FieldPhase::kGrowing ||
         (field.phase == FieldPhase::kHarvest && field.harvest_laid_share < 1.0F)) &&
        field.crop.value < seed_norms_by_crop.size();
    if (in_ground) {
      const SeedNorm& standing = seed_norms_by_crop[field.crop.value];
      if (standing.resource.value < held.size() && standing.harvest_from_month != kNoSowingMonth) {
        const std::uint8_t to = standing.harvest_to_month != kNoSowingMonth
                                    ? standing.harvest_to_month
                                    : standing.harvest_from_month;
        const bool reaping_now = month >= static_cast<std::int32_t>(standing.harvest_from_month) &&
                                 month <= static_cast<std::int32_t>(to);
        const std::int32_t months = reaping_now ? 0 : months_to(standing.harvest_from_month);
        harvest_in[standing.resource.value] = std::min(harvest_in[standing.resource.value], months);
      }
    }
    const bool year0_winter = field.rotation_year0.value < seed_norms_by_crop.size() &&
                              seed_norms_by_crop[field.rotation_year0.value].is_winter;
    const NextSowing next = NextSowingOf(field, as_of, year0_winter);
    if (next.crop.value >= seed_norms_by_crop.size()) {
      continue;
    }
    const SeedNorm& norm = seed_norms_by_crop[next.crop.value];
    if (!(norm.sowing_norm_kg_per_ha > 0.0F) || norm.resource.value >= held.size()) {
      continue;
    }
    // A crop whose table names no last sowing month is sown by its slot's
    // year's end at the latest: held until then.
    const std::uint8_t sow_to = norm.sow_to_month != kNoSowingMonth
                                    ? norm.sow_to_month
                                    : static_cast<std::uint8_t>(kYear - 1);
    const bool reaping_known = norm.harvest_from_month != kNoSowingMonth;
    std::int32_t sow_end = 0;
    std::int32_t reaped_in = 0;
    if (next.held_chain) {
      const std::int32_t sown = months_to(sow_to);
      const std::int32_t reaped = reaping_known ? months_to(norm.harvest_from_month) : 0;
      sow_end = sown + 1;
      reaped_in = reaped > sown ? reaped : reaped + kYear;  // the first reaping after it
    } else {
      const std::int32_t sow_year = static_cast<std::int32_t>(next.slot) - (norm.is_winter ? 1 : 0);
      sow_end = (sow_year * kYear) + static_cast<std::int32_t>(sow_to) - month + 1;
      reaped_in = (static_cast<std::int32_t>(next.slot) * kYear) +
                  static_cast<std::int32_t>(norm.harvest_from_month) - month;
    }
    // A SOWING ALREADY BEGUN — the field under the plough, the harrow or the
    // seed drill for this very crop — is finished by its crew past its window
    // (field_work.cpp, SowingMayOpen: the back edge is the crew's) and takes
    // its seed then. The ladder's own rule held it since question 278; the
    // door's did not, and let the plan take the seed of a sowing in progress
    // (the ladder's unit test, on the move of 0.36.34).
    const bool begun =
        !in_ground && field.crop.value == next.crop.value &&
        (field.phase == FieldPhase::kPlowing || field.phase == FieldPhase::kHarrowing ||
         field.phase == FieldPhase::kSowing);
    if (begun && sow_end < 1) {
      sow_end = 1;
    }
    if (reaping_known && sow_end > 0 && reaped_in >= 0) {
      harvest_in[norm.resource.value] = std::min(harvest_in[norm.resource.value], reaped_in);
    }
    sowings.push_back(Sowing{.norm = &norm, .row = row, .sow_end = sow_end});
  }
  // A FIELD'S SEED IS HELD when its sowing ends before the seed's next
  // harvest: otherwise that harvest gives it. The winter rye is sown in
  // September out of July's rye; holding it from January failed the canon's
  // rye 17 years of 108 (seq 5 item 8). Until 0.36.21 the rule was asked of
  // the seed as a whole, and at the turn held the potato of every chain whose
  // potato comes NEXT year out of this year's stores (econ's E2Bf, seed 1934,
  // the turn of year 7: the position 10 t short beside the idle seed).
  for (const Sowing& sowing : sowings) {
    const auto resource = sowing.norm->resource.value;
    if (sowing.sow_end <= 0 || harvest_in[resource] < sowing.sow_end) {
      continue;  // its window gone, or a harvest comes first
    }
    const Grams norm = GramsFromKilograms(sowing.norm->sowing_norm_kg_per_ha *
                                          world.fields.rows[sowing.row].area_ga);
    held[resource] += norm;
    hold.by_field_row[sowing.row] = norm;
    hold.seed_of_row[sowing.row] = sowing.norm->resource;
  }
  return hold;
}

ResourceAmounts SeedRungLeft(const WorldState& world,
                             std::span<const SeedNorm> seed_norms_by_crop,
                             std::size_t resource_count) {
  // ONE RULE WITH THE DELIVERY DOOR (boss-core-seed-ladders [1]-[2]; 0.36.34).
  // Until then the rung held "this year's sowings still to come": before the
  // turn it kept a first slot whose spring window had gone by, and nothing of
  // next spring's sowings on the fields reaped that year — on seed 1931 at
  // the turn of year 3, 147.5 t of potato against the door's 185 t, and the
  // ration and the families' exchange free to eat the difference from the
  // digging to New Year. The cases its own history named — a reaped field,
  // the autumn's winter crop, a window gone, a first slot given up for the
  // winter crop — are the rule's by NextSowingOf and the harvest-first test.
  ResourceAmounts seed =
      SeedHeldByField(world, seed_norms_by_crop, resource_count, world.calendar.day).by_resource;
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
