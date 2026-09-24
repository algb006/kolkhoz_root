// The family <-> kolkhoz exchange (family_exchange.h): what the household
// gets for its trudodni, what it gets when it is hungry anyway, and what the
// kolkhoz holds back before either.
//
// Everything here is integer bookkeeping in grams and hundredths of a
// trudoden: what leaves a unit stock lands in a pantry to the gram, so the
// settlement's food balance is exactly conserved and a reference run can be
// compared cell by cell.

#include "family_exchange.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/fund_ladder.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/spoilage.h"
#include "core_common/state_table_ops.h"

namespace core {
namespace {

/// @brief Grams of a resource lying in every unit of the settlement.
Grams VillageStock(const WorldState& world, ResourceId resource) {
  if (resource.value == kInvalidDefIdValue) {
    return 0;
  }
  Grams total = 0;
  for (const UnitRow& unit : world.units.rows) {
    // Less what a standing unit's works hold back (boss, parcel 294).
    total += UnreservedOf(unit, resource);
  }
  return total;
}

/// @brief Takes up to `wanted` grams out of the unit stocks, in row order.
/// @return What was actually taken; never more than lies there.
Grams TakeFromUnits(WorldState& world, ResourceId resource, Grams wanted) {
  if (resource.value == kInvalidDefIdValue || wanted <= 0) {
    return 0;
  }
  Grams taken = 0;
  for (UnitRow& unit : world.units.rows) {
    if (taken >= wanted || unit.stock.size() <= resource.value) {
      continue;
    }
    const Grams free = UnreservedOf(unit, resource);
    const Grams take = free < wanted - taken ? free : wanted - taken;
    unit.stock[resource.value] -= take;
    taken += take;
  }
  return taken;
}

void AddToPantry(FamilyRow& family, ResourceId resource, Grams amount) {
  if (resource.value == kInvalidDefIdValue || amount <= 0) {
    return;
  }
  if (family.pantry.size() <= resource.value) {
    family.pantry.resize(resource.value + 1U, 0);
  }
  family.pantry[resource.value] += amount;
}

Grams KilogramsToGrams(float kilograms) {
  return GramsFromKilograms(kilograms);
}

/// @brief Mean satiety of the family's members; 100 for a household with
/// nobody in it (it eats nothing and triggers nothing).
///
/// Deliberately the RAW member satiety and not FamilyRow::component_satiety:
/// the component is capped by the variety ceiling, so a family living on
/// nothing but bread would trip the ration while its bins are full. The
/// ration answers hunger, not monotony.
float FamilySatiety(const WorldState& world, FamilyId family) {
  float total = 0.0F;
  std::uint32_t counted = 0;
  for (const ResidentRow& resident : world.residents.rows) {
    // Away in the district: the district feeds him and his hunger is not the
    // family's (boss seq 1, answer 4).
    if (resident.family.value == family.value && !AwayInDistrict(resident, world.calendar.tick)) {
      total += resident.satiety;
      ++counted;
    }
  }
  return counted == 0 ? 100.0F : total / static_cast<float>(counted);
}

/// @brief How many of the family's members eat out of the pantry at all
/// (below eat_from_bio_years a child is still at the breast).
std::uint32_t EaterCount(const FoodConfig& config,
                         float life_speedup,
                         const WorldState& world,
                         FamilyId family,
                         SimDay day) {
  std::uint32_t eaters = 0;
  for (const ResidentRow& resident : world.residents.rows) {
    // AWAY IN THE DISTRICT EATS THE DISTRICT'S BREAD (boss, boss-core-epoch1-2
    // seq 1, answer 4): counted an eater, his share of the ration and the
    // issue went to the yard, which the family meal then does not spend on
    // him — the kolkhoz fed him twice, once in the hospital and once at home.
    if (resident.family.value != family.value || AwayInDistrict(resident, world.calendar.tick)) {
      continue;
    }
    if (BiologicalAgeYears(life_speedup, resident.birth_day, day) >=
        config.consumption.eat_from_bio_years) {
      ++eaters;
    }
  }
  return eaters;
}

/// Days until held seed of resource `index` is sown at the latest: to the end
/// of the last sowing month of the crops it seeds, capped at `days_left` to
/// the turn; the cap itself when no crop of it names a month.
std::uint32_t SeedHorizonDays(const FoodConfig& config,
                              const WorldState& world,
                              std::uint32_t index,
                              std::uint32_t days_left) {
  const auto today = static_cast<std::uint32_t>(world.calendar.date.month);
  std::uint32_t latest = 0;
  bool any = false;
  for (const SeedNormDef& norm : config.seed_norms) {
    if (norm.resource.value != index || norm.sow_to_month == kNoSowingMonth) {
      continue;
    }
    // To the END of the window's last month: this month counts whole.
    const std::uint32_t months =
        ((norm.sow_to_month + kMonthsPerYear - today) % kMonthsPerYear) + 1U;
    latest = std::max(latest, months * kDaysPerMonth);
    any = true;
  }
  return any ? std::min(latest, days_left) : days_left;
}

/// @brief Grams of each resource the automatic issue may not touch, dense by
/// ResourceId: the two FUNDS the design names outright plus the fodder.
///
/// "The plan reserve and the seed fund are not touched by the automatic
/// issue; eating them takes a deliberate decision to break the fund open"
/// (labor-payment design §4, resources design §2). To that, boss's answer to
/// question Q4 adds the feed: what the bundle may carry is what is left over
/// PLAN, SEED FUND AND FODDER.
///
/// Three sources, one vector:
///   * seed — the sowing still to come. Only fields that have yet to be sown
///     count: a field already in the ground took its seed when its sowing
///     phase closed, and reserving for it twice would freeze grain the
///     settlement has already spent. "Yet to be sown" is the CROP's state
///     and not the field's phase — see the loop.
///   * plan — what is still owed, as far as the crop lies below the seed,
///     carry-over and reaping alike, from the January letter (fund_ladder.h,
///     PlanRungGrams; boss, boss-core-epoch1-4 seq 9 and 10). Until 0.34.42
///     it was as much as THIS YEAR'S REAPING had covered — nought until the
///     reaping, so the whole planned crop was held instead (PlanHoldsIt),
///     and that was hunger beside full barns. Less whatever the chairman has
///     unsealed (kUnsealFund).
///   * fodder — what the kolkhoz herds ATE LAST YEAR, straight off the
///     closed book. It needs no forecast and no second copy of the feeding
///     order, and it corrects itself as the herd grows or shrinks. In the
///     first year there is no closed book and nothing is held back — which
///     is right, because the first year's fodder is the start stock, and
///     that was measured from the first cut for exactly this reason.
std::vector<Grams> IssueReserve(const FoodConfig& config, const WorldState& world) {
  // THE SEED FUND AND THE PLAN RESERVE, AND THE UNSEALINGS OFF BOTH, live in
  // core_common/fund_ladder.h since 2026-09-13, because the herds must stay
  // below the same ladder (both rungs since 0.35.1: the herd_system caller
  // passes the crops' seed norms too) and a rule with two homes grows two answers. The
  // reasons each half is computed the way it is — seed until the SOWING takes
  // it, plan filled by the HARVEST and not by the calendar, one total the
  // releases come off — are written there beside the arithmetic.
  std::vector<Grams> reserve = HeldAboveFodder(world,
                                               config.seed_norms,
                                               config.resources.size(),
                                               config.distribution.reserve_seed_fund != 0,
                                               config.carted_daily);
  // The top two rungs as they stand, before the fodder, and the seed's part
  // of them: what the rot margin below is taken on, each to its own day.
  const std::vector<Grams> seed_and_plan = reserve;
  const ResourceAmounts seed_part =
      config.distribution.reserve_seed_fund != 0
          ? SeedRungLeft(world, config.seed_norms, config.resources.size())
          : ResourceAmounts(config.resources.size(), 0);
  // RUNG 3: THE FODDER CLAIM, AND INSIDE IT THE FODDER FUND (resources
  // design §6; boss, boss-core-epoch1-resume seq 17). Last year's feed of the
  // kolkhoz's herds is held as it was — a cow's oats do not go to the table —
  // and for a work feed the team's fund today, if larger; the chairman's
  // fodder release comes off the larger (core_common/fund_ladder.h). Until
  // 0.34.17 the fund was held nowhere, and releasing it opened the plan.
  const ResourceAmounts fund = config.fodder_fund ? config.fodder_fund(world) : ResourceAmounts{};
  const ResourceAmounts fodder = FodderRungLeft(world, world.ledger.closed.feed, fund);
  for (std::uint32_t index = 0; index < fodder.size() && index < reserve.size(); ++index) {
    reserve[index] += fodder[index];
  }
  // THE PLAN RESERVE HOLDS WHAT WILL ROT BEFORE THE DELIVERY TOO (boss, parcel
  // 434). The issue takes everything above the reserve, the stores then rot
  // as a whole, and the rot took its grams out of the reserve itself: on
  // seed 1934 the oats came to the district 2.3 kg short of 1383 kg, and a
  // plan met only in full was failed. A store loses held/days a day
  // (spoilage.h), so to deliver `plan` in n days it must hold plan * (d/(d-1))^n
  // today; the delivery is at the year's turn.
  //
  // AND WHAT THE SEED RUNG HOLDS ROTS TOO (0.34.42). The margin was taken on
  // the plan alone, so the seed held for the autumn's rye rotted out of the
  // plan's margin and then out of the plan: on seed 1937, year 14, the store
  // stood at the ladder's 3006 kg before the sowing, the sowing took its full
  // 1890, the rest rotted from 1115 to 1096 by the turn, and 1116 were owed
  // — 98 % delivered. It showed once the ration from 40 drew the stores down
  // to the reserve itself. The margin is now taken on both rungs, EACH TO ITS
  // OWN DAY: the plan to the turn, the seed to its sowing (crops.csv
  // sow_from_month). Taken to the turn, the seed potatoes held from January
  // to a May sowing carried half again of themselves — some 30 % of the seed
  // over-held against the lean season (static review of 0.34.42).
  const std::uint32_t days_left = kDaysPerYear - (world.calendar.day % kDaysPerYear);
  const auto margin = [](Grams held, float days, std::uint32_t horizon) {
    return RotMarginGrams(held, days, horizon);  // one home with the seed room
  };
  for (std::uint32_t index = 0; index < seed_and_plan.size() && index < reserve.size(); ++index) {
    const float days =
        index < config.spoil_days.size() ? config.spoil_days[index] * config.keeping_factor : 0.0F;
    if (!(days > 1.0F)) {
      continue;
    }
    const Grams seed =
        index < seed_part.size() ? std::min(seed_part[index], seed_and_plan[index]) : 0;
    const Grams plan = seed_and_plan[index] - seed;
    if (plan > 0) {
      reserve[index] += margin(plan, days, days_left);
    }
    if (seed > 0) {
      reserve[index] += margin(seed, days, SeedHorizonDays(config, world, index, days_left));
    }
  }
  return reserve;
}

/// The issue norm of one position, kilograms per trudoden: the chairman's
/// (WorldState::issue_norms) once he has set any, the table's until then.
/// ONE READER for both distribution passes, so they cannot disagree about a
/// norm (econ's audit M1; kSetIssueNorm).
float IssueNormKg(const FoodConfig& config, const WorldState& world, std::uint32_t index) {
  if (!world.issue_norms.empty()) {
    return index < world.issue_norms.size() ? static_cast<float>(world.issue_norms[index]) /
                                                  static_cast<float>(kGramsPerKilogram)
                                            : 0.0F;
  }
  return index < config.resources.size() ? config.resources[index].issue_kg_per_trudoden : 0.0F;
}

/// @brief What is free to hand out: what lies in the stores minus the funds.
Grams FreeStock(const WorldState& world, const std::vector<Grams>& reserve, ResourceId resource) {
  const Grams held = resource.value < reserve.size() ? reserve[resource.value] : 0;
  const Grams free_stock = VillageStock(world, resource) - held;
  return free_stock > 0 ? free_stock : 0;
}

/// Days a resource keeps, for the order of the issue: zero in the table is
/// "does not go bad", the longest there is.
float KeepsDays(const FoodConfig& config, std::uint32_t index) {
  const float days = index < config.spoil_days.size() ? config.spoil_days[index] : 0.0F;
  return days > 0.0F ? days : std::numeric_limits<float>::infinity();
}

/// Per food category, what the positions served so far that keep SHORTER
/// than the one at hand left uncovered, grams, and whether there were any;
/// and the same for the group of equal spoil_days being served now.
struct ShortfallByCategory {
  static constexpr auto kCategories = static_cast<std::size_t>(FoodCategory::kNotFood) + 1;

  std::array<float, kCategories> shortfall{};
  std::array<bool, kCategories> served{};
  std::array<float, kCategories> group_shortfall{};
  std::array<bool, kCategories> group_served{};

  /// A new spoil_days begins: what the last group left short is now shorter.
  void CloseGroup() {
    for (std::size_t category = 0; category < kCategories; ++category) {
      shortfall[category] += group_shortfall[category];
      served[category] = served[category] || group_served[category];
    }
    group_shortfall.fill(0.0F);
    group_served.fill(false);
  }
};

/// What the monthly bundle can cover, position by position.
struct BundleCover {
  /// Share of each position's norm that is issued, 0..1.
  std::vector<float> coverage;

  /// The bundle's food value asked and covered, kilocalories.
  float wanted_kcal = 0.0F;
  float covered_kcal = 0.0F;
};

/// What each position can cover, and what share of the bundle's food VALUE
/// that comes to. Value is in kilocalories — the one unit in which a litre
/// of milk and a kilogram of potatoes are comparable at all.
///
/// THE FRESH GOES FIRST (boss seq 11 on econ seq 10; Metrics §8, the store
/// after the family, 12cbf2c): within a food category the positions are
/// served shortest spoil_days first, and a position that keeps longer than
/// another of its category with a norm is its SUBSTITUTE — it covers, gram
/// for gram, what the shorter ones could not, and nothing while they were
/// whole. Before this the sauerkraut had a norm of its own beside the
/// vegetables': host's seed 9 pickled 1.6 t on day 84 and the store held none
/// on day 88, eaten while the fresh lay beside it. A substitute's value is
/// counted in the covered kilocalories only — it answers the shorter
/// position's want and asks none of its own. Positions that keep equally
/// long do not substitute for one another: the bread's rye, wheat and
/// barley stay three positions.
BundleCover CoverBundle(const FoodConfig& config,
                        const std::vector<Grams>& reserve,
                        const WorldState& current,
                        const std::vector<Grams>& wanted) {
  const auto roster = static_cast<std::uint32_t>(wanted.size());
  BundleCover cover;
  cover.coverage.assign(roster, 0.0F);
  std::vector<std::uint32_t> order;
  for (std::uint32_t index = 0; index < roster; ++index) {
    if (wanted[index] > 0) {
      order.push_back(index);
    }
  }
  std::ranges::stable_sort(order, [&config](std::uint32_t left, std::uint32_t right) {
    return KeepsDays(config, left) < KeepsDays(config, right);
  });
  ShortfallByCategory shorter;
  float group_days = -1.0F;
  for (const std::uint32_t index : order) {
    if (KeepsDays(config, index) != group_days) {
      shorter.CloseGroup();
      group_days = KeepsDays(config, index);
    }
    const FoodResourceDef& food = config.resources[index];
    const auto category = static_cast<std::size_t>(food.category);
    const bool substitute = food.category != FoodCategory::kNotFood && shorter.served[category];
    float asked = static_cast<float>(wanted[index]);
    if (substitute) {
      asked = std::min(asked, shorter.shortfall[category]);
      shorter.shortfall[category] -= asked;
    }
    // HALF THE MILK, and only half (boss answer Q4): the bundle carries a
    // share of what the farm holds, the rest stays the kolkhoz's. The share
    // is per resource and lives in the table — it is one for everything the
    // farm hands out whole. It applies to the BUNDLE only: the ration below
    // sees the full free stock, because holding milk back from a starving
    // household would be the very "full barn beside a hungry village" this
    // rule exists to forbid.
    // FIRST THE PLAN, THEN THE ISSUE — BY THE RESERVE, NOT THE WHOLE CROP
    // (labor-payment §7; boss, boss-core-epoch1-4 seq 9 and 10). Until
    // 0.34.42 a crop the plan names went out on nothing but what the chairman
    // unsealed, carry-over included (PlanHoldsIt; boss, parcels 438 and 440):
    // on 0.34.40 that was hunger beside full barns, 258 hungry episodes, the
    // first day of hunger with 37 days of the village's need in planned crops
    // in the stores. The plan rung now holds what is owed, carry-over and
    // reaping alike, from the turn (fund_ladder.h, PlanRungGrams); everything
    // above the reserve goes out on trudodni.
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const Grams free_stock = FreeStock(current, reserve, resource);
    const float pool = static_cast<float>(free_stock) * food.issue_share_of_stock;
    const float given = asked > 0.0F ? std::min(pool, asked) : 0.0F;
    cover.coverage[index] = given / static_cast<float>(wanted[index]);
    if (substitute) {
      shorter.shortfall[category] += asked - given;  // what it could not cover either
    } else {
      shorter.group_shortfall[category] += asked - given;
      shorter.group_served[category] = true;
    }
    if (food.kcal_per_gram > 0.0F) {
      cover.wanted_kcal += substitute ? 0.0F : asked * food.kcal_per_gram;
      cover.covered_kcal += given * food.kcal_per_gram;
    }
  }
  return cover;
}

/// The monthly distribution (labor-payment §3, §7): the family trades its
/// outstanding trudodni for a basket of goods.
///
/// HOW A SHORT MONTH SETTLES, canon since 2026-08-30 and rewritten because
/// the run showed what the older wording did. The rule used to be "the
/// bundle advances by its worst position", whose purpose is sound — a family
/// must not clear its whole debt for half a bundle. Taken literally, one
/// empty position cancelled the hand-out entirely: potatoes run out every
/// summer before the new crop, so the whole issue stopped for five months a
/// year while a hundred and forty tonnes of milk stood in the store.
///
/// Now: each position issues what the store can actually give, and the DEBT
/// is redeemed by the share of the bundle's VALUE that was handed over,
/// counted in the same grain equivalent as everything else. A family that
/// received two thirds of what its trudodni were worth redeems two thirds of
/// them; the rest waits for next month. The old rule's purpose survives
/// whole, its accident does not.
///
/// Fodder rides along without touching that share: it has no calories, it is
/// for the yard's animals (livestock design §11), and an empty hayloft has
/// no business stopping the bread.
///
/// The coverage is worked out ONCE for the whole village and applied to every
/// household alike. Serving families in row order against a shrinking store
/// would let the first rows eat and the last rows starve, and nothing in the
/// design says the accountant's list is a queue.
void RunDistribution(const FoodConfig& config,
                     const std::vector<Grams>& reserve,
                     WorldState& current) {
  const auto roster = static_cast<std::uint32_t>(config.resources.size());
  std::vector<Grams> wanted(roster, 0);
  TrudodniHundredths outstanding_total = 0;
  for (const FamilyRow& family : current.families.rows) {
    const TrudodniHundredths outstanding = family.trudodni_account - family.trudodni_redeemed;
    if (outstanding <= 0) {
      continue;
    }
    outstanding_total += outstanding;
    const float trudodni = static_cast<float>(outstanding) / static_cast<float>(kTrudodniScale);
    for (std::uint32_t index = 0; index < roster; ++index) {
      const float norm = IssueNormKg(config, current, index);
      if (norm > 0.0F) {
        wanted[index] += KilogramsToGrams(norm * trudodni);
      }
    }
  }
  if (outstanding_total <= 0) {
    return;
  }
  const BundleCover cover = CoverBundle(config, reserve, current, wanted);
  const std::vector<float>& coverage = cover.coverage;
  const float redeemed_share =
      cover.wanted_kcal > 0.0F ? cover.covered_kcal / cover.wanted_kcal : 0.0F;
  bool issued_to_anyone = false;
  for (FamilyRow& family : current.families.rows) {
    const TrudodniHundredths outstanding = family.trudodni_account - family.trudodni_redeemed;
    if (outstanding <= 0) {
      continue;
    }
    const float trudodni = static_cast<float>(outstanding) / static_cast<float>(kTrudodniScale);
    for (std::uint32_t index = 0; index < roster; ++index) {
      const float norm = IssueNormKg(config, current, index);
      if (norm <= 0.0F || !(coverage[index] > 0.0F)) {
        continue;
      }
      const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
      const Grams issue = KilogramsToGrams(norm * trudodni * coverage[index]);
      // `coverage` carries the substitution too (CoverBundle): a keeping
      // position covers a share of its norm that is the shorter ones'
      // shortfall, and nothing when they were whole.
      // What the store could actually give, not what the norm asked for:
      // the ledger records the hand-out, not the intention.
      const Grams given = TakeFromUnits(current, resource, issue);
      AddToPantry(family, resource, given);
      AddLedgerAmount(current.ledger.current.issued, resource, given);
    }
    family.trudodni_redeemed +=
        static_cast<TrudodniHundredths>(static_cast<float>(outstanding) * redeemed_share);
    issued_to_anyone = true;
  }
  if (issued_to_anyone) {
    // ONE EVENT FOR THE SETTLEMENT, not one per household: the kind's
    // contract names no family (event_state.h), and it is right not to —
    // the distribution is a day of the village, and a hundred and forty
    // identical lines would bury the day it happened.
    EmitEvent(current, EventKind::kDistributionIssued, EventSeverity::kNotable);
  }
}

/// The minimum ration (labor-payment §5): bread, potatoes and a little milk
/// for a hungry household, past the trudodni account entirely. Each position
/// is issued on its own — there is no worst-position rule here, because a
/// ration that stalls whole for want of milk would be a ration that starves
/// people over bookkeeping.
///
/// WHO MAY HAVE IT IS THE CHAIRMAN'S (labor-payment §5, «для конкретной семьи
/// или для всех сразу»; econ's audit M3, Л1): the village-wide checkbox
/// (ChairmanState::ration_auto) or the decision for this yard
/// (FamilyRow::ration_granted). Until 2026-09-18 a table constant armed it
/// for everyone and nothing could switch it — «паёк платит цену скупой
/// выдачи за игрока», and the issue norms had no price. The table's value
/// is now the checkbox's START value, written at genesis.
void RunRation(const FoodConfig& config,
               float life_speedup,
               const std::vector<Grams>& reserve,
               WorldState& current) {
  const bool for_everyone = current.chairman.ration_auto != 0;
  const SimDay day = current.calendar.day;
  const auto days = static_cast<float>(config.distribution.period_days);
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    const FamilyId id = current.families.row_ids[row];
    if (!for_everyone && current.families.rows[row].ration_granted == 0) {
      continue;
    }
    if (FamilySatiety(current, id) > config.distribution.ration_satiety_threshold) {
      continue;
    }
    const std::uint32_t eaters = EaterCount(config, life_speedup, current, id, day);
    if (eaters == 0) {
      continue;
    }
    bool given_anything = false;
    for (std::uint32_t index = 0; index < config.resources.size(); ++index) {
      const float norm = config.resources[index].ration_kg_per_day;
      if (norm <= 0.0F) {
        continue;
      }
      const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
      const Grams wanted = KilogramsToGrams(norm * static_cast<float>(eaters) * days);
      const Grams free_stock = FreeStock(current, reserve, resource);
      const Grams issue = wanted < free_stock ? wanted : free_stock;
      const Grams given = TakeFromUnits(current, resource, issue);
      AddToPantry(current.families.rows[row], resource, given);
      AddLedgerAmount(current.ledger.current.ration, resource, given);
      given_anything = given_anything || given > 0;
    }
    if (given_anything) {
      // Per household, unlike the distribution above, and for the opposite
      // reason: the safety ration is a statement about THIS family, and
      // which families are on it is the whole information in it.
      SimEvent& event = EmitEvent(current, EventKind::kRationIssued, EventSeverity::kNotable);
      event.family = id;
    }
  }
}

/// Net fishing (household design §2): an epoch constant straight into the
/// pantry. No unit, no work order, no mechanic — and deliberately not part
/// of the designed coverage, it is help on top of it. The yearly figure is
/// spread over the game year so that a family that appears in June gets its
/// share of the summer rather than a whole year at once.
void RunNets(const FoodConfig& config, WorldState& current) {
  const std::uint32_t epoch = EpochIndex(current.epoch);
  if (config.fish_resource.value == kInvalidDefIdValue ||
      epoch >= config.plot.fish_kg_per_yard_year.size()) {
    return;
  }
  const Grams daily =
      KilogramsToGrams(config.plot.fish_kg_per_yard_year[epoch] / static_cast<float>(kDaysPerYear));
  if (daily <= 0) {
    return;
  }
  for (FamilyRow& family : current.families.rows) {
    AddToPantry(family, config.fish_resource, daily);
    AddLedgerAmount(current.ledger.current.nets, config.fish_resource, daily);
  }
}

/// The economic year's close (labor-payment §3): both counters burn — what
/// was earned and what was covered. Unredeemed trudodni are not a debt the
/// kolkhoz carries into the next year; that is the whole point of the rule,
/// and it is why the distribution above runs BEFORE the burn on this day.
void BurnTrudodni(WorldState& current) {
  for (FamilyRow& family : current.families.rows) {
    // What burns is what was earned and never covered. Recorded into the
    // book this turn is CLOSING (ledger_state.h): the burn is the ending
    // year's last piece of business, not the new year's first.
    const TrudodniHundredths unspent = family.trudodni_account - family.trudodni_redeemed;
    if (unspent > 0) {
      current.ledger.current.trudodni_burned += unspent;
    }
    family.trudodni_account = 0;
    family.trudodni_redeemed = 0;
  }
}

}  // namespace

std::vector<Grams> SealedFunds(const FoodConfig& config, const WorldState& world) {
  if (config.resources.empty()) {
    return {};
  }
  // THE FUNDS, AND NOT THE PLANNED CROP WHOLE (boss, boss-core-epoch1-4 seq
  // 2). 0.34.39 sealed a crop the plan names whole down to the unsealed, as
  // the distribution then held it (PlanHoldsIt, gone in 0.34.42), and the
  // distiller took not one
  // kilogram of rye, oats or potatoes in 270 village-years — the samogon, a
  // live trouble of the village, switched off. Holding the planned crop
  // whole is a promise to the FAMILIES, not a lock against the thief.
  return IssueReserve(config, world);
}

void RunFamilyExchange(const FoodConfig& config, float life_speedup, WorldState& current) {
  if (config.resources.empty()) {
    return;  // a table-less world has no food roster and nothing to hand out
  }
  RunNets(config, current);
  // THE LARDERS ROT TOO, and they rot here — after the meal. The meal is the
  // needs slot, phase 2; this is the decisions slot, phase 3, of the same
  // tick, so what a family ate today it ate before today's spoilage
  // (transport design §10; boss's ordering, 2026-09-03). Rot the larder
  // before the meal and the village would starve beside a full cellar.
  //
  // Without this the kolkhoz's issue would be a way of hiding food from
  // time: the stores went bad and the pantries did not, so handing food out
  // preserved it. The food_year run measured exactly that — striking out the
  // issue norms came out BETTER than keeping them — and that inversion is
  // what sent me looking.
  if (!config.spoil_days.empty()) {
    for (FamilyRow& family : current.families.rows) {
      if (family.pantry.empty()) {
        continue;
      }
      SpoilAmounts(
          family.pantry, config.spoil_days, config.keeping_factor, current.ledger.current.spoiled);
    }
  }
  const SimDay day = current.calendar.day;
  if (config.distribution.period_days > 0 && day % config.distribution.period_days == 0) {
    const std::vector<Grams> reserve = IssueReserve(config, current);
    RunDistribution(config, reserve, current);
    RunRation(config, life_speedup, reserve, current);
  }
  if (day > 0 && day % kDaysPerYear == 0) {
    BurnTrudodni(current);
  }
}

std::vector<std::pair<ResourceId, Grams>> LockedRationFood(const FoodConfig& config,
                                                           const WorldState& world) {
  std::vector<std::pair<ResourceId, Grams>> locked;
  if (config.resources.empty()) {
    return locked;
  }
  // The same reserve the distribution and the ration read, so the alarm and
  // the ration cannot disagree about what is free.
  const std::vector<Grams> reserve = IssueReserve(config, world);
  for (std::uint32_t index = 0; index < config.resources.size(); ++index) {
    if (!(config.resources[index].ration_kg_per_day > 0.0F)) {
      continue;  // not a ration position: its absence starves nobody
    }
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const Grams stock = VillageStock(world, resource);
    const Grams held = index < reserve.size() ? reserve[index] : 0;
    const Grams in_funds = held < stock ? held : stock;
    if (FreeStock(world, reserve, resource) <= 0 && in_funds > 0) {
      locked.emplace_back(resource, in_funds);
    }
  }
  return locked;
}

void ConsumeRationOrders(WorldState& current) {
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending || order.kind != OrderKind::kSetRation) {
      continue;
    }
    const std::uint8_t wanted = order.enable != 0 ? 1U : 0U;
    OrderRefusal refusal = OrderRefusal::kNone;
    if (order.family.value == kInvalidEntityIdValue) {
      if (current.chairman.ration_auto == wanted) {
        refusal = OrderRefusal::kRuleForbids;
      } else {
        current.chairman.ration_auto = wanted;
      }
    } else {
      const std::uint32_t row = FindRow(current.families, order.family);
      if (row == kNoRow) {
        refusal = OrderRefusal::kNoSuchSubject;
      } else if (current.families.rows[row].ration_granted == wanted) {
        refusal = OrderRefusal::kRuleForbids;
      } else {
        current.families.rows[row].ration_granted = wanted;
      }
    }
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }
}

void ConsumeIssueNormOrders(const FoodConfig& config, WorldState& current) {
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending || order.kind != OrderKind::kSetIssueNorm) {
      continue;
    }
    const std::size_t index = order.resource.value;
    // FOOD ONLY: a position of the bundle is something eaten. Hay and straw
    // go through the fodder table, and a norm on them here would be a second
    // door to the same stores.
    if (index >= config.resources.size() || !(config.resources[index].kcal_per_gram > 0.0F)) {
      order.status = OrderStatus::kRefused;
      order.refusal = OrderRefusal::kNotEligible;
      continue;
    }
    // The first order copies the whole bundle out of the table, so every
    // position the chairman did not touch keeps the table's norm.
    if (current.issue_norms.empty()) {
      current.issue_norms.assign(config.resources.size(), 0);
      for (std::size_t position = 0; position < config.resources.size(); ++position) {
        current.issue_norms[position] =
            KilogramsToGrams(config.resources[position].issue_kg_per_trudoden);
      }
    }
    current.issue_norms[index] = order.amount;
    order.status = OrderStatus::kDone;
  }
}

}  // namespace core
