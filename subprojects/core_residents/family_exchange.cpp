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
#include "core_common/emit_event.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/spoilage.h"

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
///     settlement has already spent. "Yet to be sown" is the CROP's state
///     and not the field's phase — see the loop.
///   * plan — as much of WorldState::plan.due as THIS YEAR'S REAPING has
///     covered so far, and no more. The norm itself is announced in the
///     spring off the land worked last year, but the ladder of funds is a
///     distribution of the HARVEST (resources design §6), so in April there
///     is nothing yet to set aside and the granary of last year is free.
///     Reserving the whole norm from January instead locks that granary
///     against a plan that will be met out of a crop still in the ground,
///     and the settlement starves in the spring beside grain it may not
///     touch — measured, on 2026-09-12, as a leanest day of 20 against 38.
///     Less whatever the chairman has unsealed (kUnsealFund).
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
      // UNTIL THE SOWING TAKES IT, not until the ploughing starts.
      //
      // The condition was `phase == kIdle` until 2026-09-05, and that held
      // the seed for the wrong thing: the plough opened, the field left the
      // idle phase, and the grain went free WEEKS BEFORE the sowing came for
      // it — eleven protected days of forty-eight in one measured year, ONE
      // day in two others (69-reconciliation.md §13.16). The fund was
      // guarding A PHASE OF THE FIELD rather than A QUANTITY OF GRAIN.
      //
      // A field is done needing seed once the crop is in the ground: growing
      // or being reaped. Everything before that — idle, ploughing,
      // harrowing, sowing itself — is a field that still has to be sown, and
      // the design's rule is that the fund opens only by the chairman's own
      // decision (resources design §6), never by a plough.
      const bool already_sown =
          field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
      if (already_sown || field.rotation_year0.value >= config.seed_norms.size()) {
        continue;
      }
      const SeedNormDef& seed = config.seed_norms[field.rotation_year0.value];
      if (seed.resource.value >= reserve.size() || seed.sowing_norm_kg_per_ha <= 0.0F) {
        continue;
      }
      reserve[seed.resource.value] += KilogramsToGrams(seed.sowing_norm_kg_per_ha * field.area_ga);
    }
  }
  // THE PLAN RESERVE IS FILLED BY THE HARVEST, NOT BY THE CALENDAR, and the
  // distinction cost a lean spring to find (boss, 2026-09-12). Resources
  // design §6 opens with what the ladder of funds distributes: "Урожай не
  // лежит одной кучей — он расписан по фондам". The seed fund stands all
  // winter because it is for the sowing to come; the plan reserve is the
  // undelivered remainder of THIS year's plan, and in April this year has
  // reaped nothing, so there is nothing yet to set aside.
  //
  // Reserving the whole norm from January instead locks last year's granary
  // against a plan that will be met out of a crop still in the ground — and
  // the village starves in the spring beside grain it may not touch.
  //
  // THE SIZE OF THE PLAN STILL COMES FROM THE WORKED LAND; only the GRAIN
  // held against it comes from the reaping. The two were both taken off the
  // harvest before, which is why no year could be failed, and taking both
  // off the land swung the instrument through the middle to the other side.
  const ResourceAmounts& reaped = world.ledger.current.harvest;
  for (std::uint32_t index = 0; index < world.plan.due.size() && index < reserve.size(); ++index) {
    const Grams owed = world.plan.due[index];
    const Grams gathered = index < reaped.size() ? reaped[index] : 0;
    reserve[index] += owed < gathered ? owed : gathered;
  }
  // AND WHAT THE CHAIRMAN HAS UNSEALED IS NO LONGER HELD (kUnsealFund;
  // resources design §6). This is the whole mechanism of that verb: the
  // funds are a computation over one heap of grain, so opening one means
  // this sum asks for less.
  //
  // AND THE RESERVE IS ONE TOTAL, so both releases come off the same number
  // and it makes no arithmetic difference which fund the chairman named.
  // Said plainly because the obvious comment to write here is that the seed
  // release comes off the seed's share — it does not, and a sentence
  // claiming a separation the code does not make is worse than no sentence.
  // The two are tracked apart for the save and for the player: which fund
  // was opened is which RISK was taken, and that difference is real even
  // where the subtraction's is not.
  const auto release = [&reserve](const ResourceAmounts& opened) {
    for (std::uint32_t index = 0; index < opened.size() && index < reserve.size(); ++index) {
      reserve[index] = reserve[index] > opened[index] ? reserve[index] - opened[index] : 0;
    }
  };
  for (const ResourceAmounts& opened : world.unsealed.by_fund) {
    release(opened);
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
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
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
  bool issued_to_anyone = false;
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
      const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
      const Grams issue = KilogramsToGrams(norm * trudodni * coverage[index]);
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

}  // namespace core
