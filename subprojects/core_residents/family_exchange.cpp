// The family <-> kolkhoz exchange (family_exchange.h): what the household
// gets for its trudodni, what it gets when it is hungry anyway, and what the
// kolkhoz holds back before either.
//
// Everything here is integer bookkeeping in grams and hundredths of a
// trudoden: what leaves a unit stock lands in a pantry to the gram, so the
// settlement's food balance is exactly conserved and a reference run can be
// compared cell by cell.

#include "family_exchange.h"

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"

namespace core {
namespace {

/// @brief Grams of a resource lying in every unit of the settlement.
Grams VillageStock(const WorldState& world, ResourceId resource) {
  if (resource.value == kInvalidDefIdValue) {
    return 0;
  }
  Grams total = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (unit.stock.size() > resource.value) {
      total += unit.stock[resource.value];
    }
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
    Grams& cell = unit.stock[resource.value];
    const Grams take = cell < wanted - taken ? cell : wanted - taken;
    cell -= take;
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
  return static_cast<Grams>(kilograms * static_cast<float>(kGramsPerKilogram));
}

std::uint32_t EpochIndex(Epoch epoch) {
  return static_cast<std::uint32_t>(epoch) - 1;
}

float BiologicalAgeYears(float life_speedup, std::int32_t birth_day, SimDay day) {
  const float game_years = static_cast<float>(static_cast<std::int32_t>(day) - birth_day) /
                           static_cast<float>(kDaysPerYear);
  return game_years * life_speedup;
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
    if (resident.family.value == family.value) {
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
    if (resident.family.value != family.value) {
      continue;
    }
    if (BiologicalAgeYears(life_speedup, resident.birth_day, day) >=
        config.consumption.eat_from_bio_years) {
      ++eaters;
    }
  }
  return eaters;
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
///     settlement has already spent.
///   * plan — WorldState::plan.due, which accrues as the grain is reaped and
///     is handed over at the turn of the year. Its absence here is what let
///     the issue eat the district's share all summer.
///   * fodder — what the kolkhoz herds ATE LAST YEAR, straight off the
///     closed book. It needs no forecast and no second copy of the feeding
///     order, and it corrects itself as the herd grows or shrinks. In the
///     first year there is no closed book and nothing is held back — which
///     is right, because the first year's fodder is the start stock, and
///     that was measured from the first cut for exactly this reason.
std::vector<Grams> IssueReserve(const FoodConfig& config, const WorldState& world) {
  std::vector<Grams> reserve(config.resources.size(), 0);
  if (config.distribution.reserve_seed_fund != 0) {
    for (const FieldRow& field : world.fields.rows) {
      if (field.phase != FieldPhase::kIdle ||
          field.rotation_year0.value >= config.seed_norms.size()) {
        continue;
      }
      const SeedNormDef& seed = config.seed_norms[field.rotation_year0.value];
      if (seed.resource.value >= reserve.size() || seed.sowing_norm_kg_per_ha <= 0.0F) {
        continue;
      }
      reserve[seed.resource.value] += KilogramsToGrams(seed.sowing_norm_kg_per_ha * field.area_ga);
    }
  }
  for (std::uint32_t index = 0; index < world.plan.due.size() && index < reserve.size(); ++index) {
    reserve[index] += world.plan.due[index];
  }
  const ResourceAmounts& fodder = world.ledger.closed.feed;
  for (std::uint32_t index = 0; index < fodder.size() && index < reserve.size(); ++index) {
    reserve[index] += fodder[index];
  }
  return reserve;
}

/// @brief What is free to hand out: what lies in the stores minus the funds.
Grams FreeStock(const WorldState& world, const std::vector<Grams>& reserve, ResourceId resource) {
  const Grams held = resource.value < reserve.size() ? reserve[resource.value] : 0;
  const Grams free_stock = VillageStock(world, resource) - held;
  return free_stock > 0 ? free_stock : 0;
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
      const float norm = config.resources[index].issue_kg_per_trudoden;
      if (norm > 0.0F) {
        wanted[index] += KilogramsToGrams(norm * trudodni);
      }
    }
  }
  if (outstanding_total <= 0) {
    return;
  }
  // What each position can cover, and what share of the bundle's food VALUE
  // that comes to. Value is in kilocalories — the one unit in which a litre
  // of milk and a kilogram of potatoes are comparable at all.
  std::vector<float> coverage(roster, 0.0F);
  float wanted_kcal = 0.0F;
  float covered_kcal = 0.0F;
  for (std::uint32_t index = 0; index < roster; ++index) {
    if (wanted[index] <= 0) {
      continue;
    }
    const ResourceId resource{static_cast<std::uint16_t>(index)};
    // HALF THE MILK, and only half (boss answer Q4): the bundle carries a
    // share of what the farm holds, the rest stays the kolkhoz's. The share
    // is per resource and lives in the table — it is one for everything the
    // farm hands out whole. It applies to the BUNDLE only: the ration below
    // sees the full free stock, because holding milk back from a starving
    // household would be the very "full barn beside a hungry village" this
    // rule exists to forbid.
    const float pool = static_cast<float>(FreeStock(current, reserve, resource)) *
                       config.resources[index].issue_share_of_stock;
    const float share = pool / static_cast<float>(wanted[index]);
    coverage[index] = share < 1.0F ? share : 1.0F;
    const float kcal = config.resources[index].kcal_per_gram;
    if (kcal > 0.0F) {
      const float position = static_cast<float>(wanted[index]) * kcal;
      wanted_kcal += position;
      covered_kcal += position * coverage[index];
    }
  }
  const float redeemed_share = wanted_kcal > 0.0F ? covered_kcal / wanted_kcal : 0.0F;
  for (FamilyRow& family : current.families.rows) {
    const TrudodniHundredths outstanding = family.trudodni_account - family.trudodni_redeemed;
    if (outstanding <= 0) {
      continue;
    }
    const float trudodni = static_cast<float>(outstanding) / static_cast<float>(kTrudodniScale);
    for (std::uint32_t index = 0; index < roster; ++index) {
      const float norm = config.resources[index].issue_kg_per_trudoden;
      if (norm <= 0.0F || !(coverage[index] > 0.0F)) {
        continue;
      }
      const ResourceId resource{static_cast<std::uint16_t>(index)};
      const Grams issue = KilogramsToGrams(norm * trudodni * coverage[index]);
      // What the store could actually give, not what the norm asked for:
      // the ledger records the hand-out, not the intention.
      const Grams given = TakeFromUnits(current, resource, issue);
      AddToPantry(family, resource, given);
      AddLedgerAmount(current.ledger.current.issued, resource, given);
    }
    family.trudodni_redeemed +=
        static_cast<TrudodniHundredths>(static_cast<float>(outstanding) * redeemed_share);
  }
}

/// The minimum ration (labor-payment §5): bread, potatoes and a little milk
/// for a hungry household, past the trudodni account entirely. Each position
/// is issued on its own — there is no worst-position rule here, because a
/// ration that stalls whole for want of milk would be a ration that starves
/// people over bookkeeping.
///
/// Phase 1 arms it for everyone (DistributionConfig::ration_auto): with no
/// player at the wheel, "the village starves under bad management" and "the
/// ration was never switched on" would otherwise be the same reading.
void RunRation(const FoodConfig& config,
               float life_speedup,
               const std::vector<Grams>& reserve,
               WorldState& current) {
  if (config.distribution.ration_auto == 0) {
    return;
  }
  const SimDay day = current.calendar.day;
  const auto days = static_cast<float>(config.distribution.period_days);
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    const FamilyId id = current.families.row_ids[row];
    if (FamilySatiety(current, id) > config.distribution.ration_satiety_threshold) {
      continue;
    }
    const std::uint32_t eaters = EaterCount(config, life_speedup, current, id, day);
    if (eaters == 0) {
      continue;
    }
    for (std::uint32_t index = 0; index < config.resources.size(); ++index) {
      const float norm = config.resources[index].ration_kg_per_day;
      if (norm <= 0.0F) {
        continue;
      }
      const ResourceId resource{static_cast<std::uint16_t>(index)};
      const Grams wanted = KilogramsToGrams(norm * static_cast<float>(eaters) * days);
      const Grams free_stock = FreeStock(current, reserve, resource);
      const Grams issue = wanted < free_stock ? wanted : free_stock;
      const Grams given = TakeFromUnits(current, resource, issue);
      AddToPantry(current.families.rows[row], resource, given);
      AddLedgerAmount(current.ledger.current.ration, resource, given);
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

void RunFamilyExchange(const FoodConfig& config, float life_speedup, WorldState& current) {
  if (config.resources.empty()) {
    return;  // a table-less world has no food roster and nothing to hand out
  }
  RunNets(config, current);
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

}  // namespace core
