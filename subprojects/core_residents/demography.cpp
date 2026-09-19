// The structural half of the people (demography.h).

#include "demography.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/emit_event.h"
#include "core_common/family_state.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/plot.h"
#include "core_common/random.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "housing.h"

namespace core {
namespace {

/// @brief Uniform float in [low, high) from the sequential RNG.
float DrawInRange(RngState& rng, float low, float high) {
  return low + NextRandomUnitFloat(rng) * (high - low);
}

/// @brief Rolls a newborn's sex with the self-correcting draw (life-cycle
/// §6): the probability leans toward the sex under-represented among the
/// rising generation, so a small village cannot drift into a lasting skew.
Sex DrawNewbornSex(const LifeConfig& config, const WorldState& current, RngState& rng, SimDay day) {
  std::int32_t boys = 0;
  std::int32_t girls = 0;
  for (const ResidentRow& row : current.residents.rows) {
    if (BiologicalAgeYears(config.life_speedup, row.birth_day, day) < config.adult_age_years) {
      (row.sex == Sex::kMale ? boys : girls) += 1;
    }
  }
  const auto total = static_cast<float>(boys + girls);
  float male_chance = 0.5F;
  if (total > 0.0F) {
    male_chance += config.sex_balance_gain * static_cast<float>(girls - boys) / total;
  }
  male_chance = male_chance < 0.35F ? 0.35F : (male_chance > 0.65F ? 0.65F : male_chance);
  return NextRandomUnitFloat(rng) < male_chance ? Sex::kMale : Sex::kFemale;
}

/// @brief An inclination of a newborn: mostly random, weakly pulled toward
/// the parents' mid-point (metrics design §2: "not inheritance, a leaning").
Metric BlendInclination(RngState& rng,
                        Metric mother_value,
                        Metric father_value,
                        float parent_pull) {
  const float parents_mid = (mother_value + father_value) * 0.5F;
  return parents_mid * parent_pull + DrawInRange(rng, 0.0F, 100.0F) * (1.0F - parent_pull);
}

/// @brief Drops a household that has nobody left in it, and settles what it
/// leaves behind.
///
/// THE ROW MUST GO, and until now only death and departure took it: a yard
/// emptied by a WEDDING stayed on the books for ever — a migrant arrives as
/// a household of one and marries, a widower marries again — and an empty
/// household is not inert. It draws the no-worker plot hours, so it gardens,
/// mows and fishes, and its pantry fills with food nobody can eat. By the
/// twelfth year of the run 136 of 255 households were empty and held 95% of
/// the settlement's food while the living went hungry beside it
/// (manual/balance/69-reconciliation.md §3 D4).
///
/// What it leaves goes to the NEIGHBOURS (boss answer to question Q5): the
/// pantry and the animals, by the same neighbourly hand the canon uses to
/// explain how a yard comes by its first cow. The garden goes with the house,
/// and the house stands free (families design §2, life-cycle §11).
///
/// The heir is the first surviving household in row order. Which neighbour it
/// is decides nothing — there is no proximity in the design and no choice for
/// the player here — and row order is the one rule that is the same on every
/// machine.
void DropFamilyIfEmpty(WorldState& current, FamilyId family) {
  if (family.value == kInvalidEntityIdValue) {
    return;
  }
  for (const ResidentRow& row : current.residents.rows) {
    if (row.family.value == family.value) {
      return;  // somebody still lives here
    }
  }
  const std::uint32_t leaving = FindRow(current.families, family);
  if (leaving == kNoRow) {
    return;
  }
  std::uint32_t heir = kNoRow;
  for (std::uint32_t row = 0; row < current.families.rows.size() && heir == kNoRow; ++row) {
    if (row == leaving) {
      continue;
    }
    for (const ResidentRow& resident : current.residents.rows) {
      if (resident.family.value == current.families.row_ids[row].value) {
        heir = row;
        break;
      }
    }
  }
  // What the yard has EARNED goes with what it owns. A household of one that
  // marries out is not a household that stopped existing: its trudodni were
  // worked for, and dropping the row with an outstanding account destroyed
  // them silently — the year's books stopped closing, which is how the labor
  // run caught it. With no heir at all they are booked as burned, so the
  // ledger's own identity holds either way.
  const TrudodniHundredths outstanding = current.families.rows[leaving].trudodni_account -
                                         current.families.rows[leaving].trudodni_redeemed;
  if (heir != kNoRow) {
    const ResourceAmounts left = current.families.rows[leaving].pantry;
    ResourceAmounts& taken = current.families.rows[heir].pantry;
    if (taken.size() < left.size()) {
      taken.resize(left.size(), 0);
    }
    for (std::uint32_t index = 0; index < left.size(); ++index) {
      taken[index] += left[index];
    }
    if (outstanding > 0) {
      current.families.rows[heir].trudodni_account += outstanding;
    }
    // The animals go to the neighbour's yard — INTO its own flock, not
    // beside it. A yard keeps one herd of a kind (household design §2), and
    // stacking a second row on it quietly doubled the ceiling: every row
    // holds its own cap, so an heir that inherited twice kept twenty hens
    // where the canon allows ten. The herd day trims whatever the merge puts
    // over the limit, by the canon's own three paths.
    const FamilyId home = current.families.row_ids[heir];
    std::vector<HerdId> merged;
    for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
      HerdRow& herd = current.herds.rows[row];
      if (herd.household_owned == 0 || herd.household.value != family.value) {
        continue;
      }
      std::uint32_t into = kNoRow;
      for (std::uint32_t other = 0; other < current.herds.rows.size(); ++other) {
        const HerdRow& theirs = current.herds.rows[other];
        if (other != row && theirs.household_owned != 0 && theirs.household.value == home.value &&
            theirs.kind.value == herd.kind.value) {
          into = other;
          break;
        }
      }
      if (into == kNoRow) {
        herd.household = home;
        continue;
      }
      HerdRow& flock = current.herds.rows[into];
      flock.adult_count = static_cast<std::uint16_t>(flock.adult_count + herd.adult_count);
      flock.juvenile_count = static_cast<std::uint16_t>(flock.juvenile_count + herd.juvenile_count);
      flock.newborn_count = static_cast<std::uint16_t>(flock.newborn_count + herd.newborn_count);
      flock.adult_age_game_years_total += herd.adult_age_game_years_total;
      merged.push_back(current.herds.row_ids[row]);
    }
    for (const HerdId id : merged) {
      RemoveRow(current.herds, id);
    }
  } else {
    if (outstanding > 0) {
      current.ledger.current.trudodni_burned += outstanding;
    }
    // AND THE PANTRY OF A HOUSEHOLD NOBODY INHERITS IS GONE WITH A LINE IN THE
    // BOOK (2026-09-15). Until then it vanished unbooked, and nobody saw it
    // while the seed fund held every oat: the day the fund stopped holding a
    // reaped field's seed, oats reached the pantries, the last family of
    // oat_balance's village died out with 0.418 t of them, and its balance
    // stopped closing. Booked as lost_no_room, the book's "gone" — the same
    // column the stock of a store that fell down goes to.
    const ResourceAmounts& left = current.families.rows[leaving].pantry;
    for (std::uint32_t index = 0; index < left.size(); ++index) {
      if (left[index] > 0) {
        AddLedgerAmount(current.ledger.current.lost_no_room,
                        ResourceId{static_cast<std::uint16_t>(index)},
                        left[index]);
      }
    }
  }
  RemoveRow(current.families, family);
  // The emptied family's house stands free again (families design §2).
  for (UnitRow& unit : current.units.rows) {
    if (unit.household.value == family.value) {
      unit.household = FamilyId{};
    }
  }
}

/// @brief Removes a resident and repairs links: the spouse becomes widowed,
/// an emptied family disappears.
void RemoveResident(WorldState& current, ResidentId id) {
  const std::uint32_t row_index = FindRow(current.residents, id);
  if (row_index == kNoRow) {
    return;
  }
  const FamilyId family = current.residents.rows[row_index].family;
  const ResidentId spouse = current.residents.rows[row_index].spouse;
  // A post empties with its holder, and nobody ordered it (task A7; boss's
  // decision of 2026-09-03). This is the one chokepoint every death and
  // every departure goes through, which is why the announcement lives here
  // and not in each of them — a way out of the village that forgot to say
  // it would be a post the player never learns is empty.
  const PostAssignment post = current.residents.rows[row_index].post;
  if (post.profession.value != kInvalidDefIdValue) {
    SimEvent& vacated = EmitEvent(current, EventKind::kPostVacated, EventSeverity::kNotable);
    vacated.resident = id;
    vacated.unit = post.unit;
    vacated.amount = static_cast<std::int64_t>(post.profession.value);
  }
  RemoveRow(current.residents, id);
  const std::uint32_t spouse_row = FindRow(current.residents, spouse);
  if (spouse_row != kNoRow) {
    current.residents.rows[spouse_row].spouse = ResidentId{};
  }
  DropFamilyIfEmpty(current, family);
}

void RunDeaths(const LifeConfig& config, WorldState& current, SimDay day) {
  std::vector<ResidentId> dead;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const float age =
        BiologicalAgeYears(config.life_speedup, current.residents.rows[row].birth_day, day);
    float percent_per_year = config.mortality_young_percent_per_year;
    if (age >= config.mortality_age_old_years) {
      percent_per_year = config.mortality_old_percent_per_year;
    } else if (age >= config.mortality_age_mid_years) {
      percent_per_year = config.mortality_mid_percent_per_year;
    }
    const float daily_chance = percent_per_year / 100.0F / static_cast<float>(kDaysPerYear);
    if (NextRandomUnitFloat(current.rng) < daily_chance) {
      dead.push_back(current.residents.row_ids[row]);
    }
  }
  for (const ResidentId id : dead) {
    // BEFORE the row goes: afterwards there is no family left to name, and
    // an event that says only "somebody died" is a line the player cannot
    // act on. The two ways out of the village are announced at their own
    // call sites and not inside RemoveResident, which cannot tell them
    // apart — and telling them apart is the whole content of the news.
    const std::uint32_t row = FindRow(current.residents, id);
    SimEvent& died = EmitEvent(current, EventKind::kResidentDied, EventSeverity::kNotable);
    died.resident = id;
    if (row != kNoRow) {
      died.family = current.residents.rows[row].family;
    }
    RemoveResident(current, id);
  }
  current.ledger.current.deaths += static_cast<std::uint32_t>(dead.size());
}

/// Epoch-III outflow (demography design; reference run): the young and
/// single leave. ASSUMPTION: only unmarried working-age residents go —
/// families are indivisible and mass leave papers are not given.
void RunOutflow(const LifeConfig& config,
                WorldState& current,
                const EpochDemography& epoch,
                SimDay day) {
  if (epoch.outflow_percent_per_year <= 0.0F) {
    return;
  }
  const float daily_chance =
      epoch.outflow_percent_per_year / 100.0F / static_cast<float>(kDaysPerYear);
  std::vector<ResidentId> leaving;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& resident = current.residents.rows[row];
    const float age = BiologicalAgeYears(config.life_speedup, resident.birth_day, day);
    const bool working_age = age >= config.adult_age_years && age < config.mortality_age_old_years;
    if (working_age && resident.spouse.value == kInvalidEntityIdValue &&
        NextRandomUnitFloat(current.rng) < daily_chance) {
      leaving.push_back(current.residents.row_ids[row]);
    }
  }
  for (const ResidentId id : leaving) {
    const std::uint32_t row = FindRow(current.residents, id);
    SimEvent& left = EmitEvent(current, EventKind::kResidentLeft, EventSeverity::kNotable);
    left.resident = id;
    if (row != kNoRow) {
      left.family = current.residents.rows[row].family;
    }
    RemoveResident(current, id);
  }
  // Booked apart from deaths, and that is the whole reason the ledger
  // exists: from the outside both are one row fewer in the table.
  current.ledger.current.departures += static_cast<std::uint32_t>(leaving.size());
}

/// The band multiplier of decision 106: a household's satisfaction scales
/// how readily it has children. The multipliers are canon; where the bands
/// fall is ASSUMPTION and lives in life.csv.
float BirthMultiplier(const LifeConfig& config, Metric satisfaction) {
  const BirthConditionsConfig& births = config.birth_conditions;
  for (std::uint32_t band = 0; band < births.satisfaction_bounds.size(); ++band) {
    if (satisfaction < births.satisfaction_bounds[band]) {
      return births.multipliers[band];
    }
  }
  return births.multipliers.back();
}

/// The two hard stops of decision 106. They are STOPS, not scales: a
/// hungry household and a sick woman do not have fewer children, they have
/// none until the condition lifts. Pregnancy itself is not modelled (the
/// cohort model has no room for it), so the stop simply cancels the draw.
///
/// The hunger stop reads the family's YEAR, not its day — canon since
/// 2026-08-30 (life-cycle design §4), and the run is what settled it.
/// Satiety is a fast, seasonal metric on purpose; read daily as a
/// prohibition it says "this season forbids children" rather than "this
/// hunger does", and it closed the village from July to November every
/// year. Health needs no such care because health is already slow.
bool BirthsStopped(const LifeConfig& config, const WorldState& current, const ResidentRow& mother) {
  const BirthConditionsConfig& births = config.birth_conditions;
  if (mother.health < births.mother_health_stop) {
    return true;
  }
  const std::uint32_t family_row = FindRow(current.families, mother.family);
  return family_row != kNoRow &&
         current.families.rows[family_row].satiety_year_mean < births.satiety_stop;
}

void RunBirths(const LifeConfig& config,
               WorldState& current,
               const EpochDemography& epoch,
               SimDay day) {
  // Children per family are spread over the fertile window, as in the
  // reference run: rate per game year = children / fertile game years.
  const float fertile_game_years =
      (config.fertility_to_years - config.fertility_from_years) / config.life_speedup;
  const float daily_chance =
      epoch.children_per_family / fertile_game_years / static_cast<float>(kDaysPerYear);
  std::vector<std::uint32_t> mothers;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& resident = current.residents.rows[row];
    if (resident.sex != Sex::kFemale || resident.spouse.value == kInvalidEntityIdValue) {
      continue;
    }
    const float age = BiologicalAgeYears(config.life_speedup, resident.birth_day, day);
    if (age < config.fertility_from_years || age >= config.fertility_to_years) {
      continue;
    }
    // Decision 106: conditions scale the rate and hunger stops it outright.
    // The draw is taken either way so that the RNG sequence does not depend
    // on how many households happen to be hungry — determinism first.
    const std::uint32_t family_row = FindRow(current.families, resident.family);
    const float multiplier =
        family_row == kNoRow
            ? 1.0F
            : BirthMultiplier(config, current.families.rows[family_row].satisfaction);
    const bool drawn = NextRandomUnitFloat(current.rng) < daily_chance * multiplier;
    if (drawn && !BirthsStopped(config, current, resident)) {
      mothers.push_back(row);
    }
  }
  for (const std::uint32_t mother_row : mothers) {
    // Child mortality applies at birth, off-screen (tone design §4;
    // reference run does the same).
    if (NextRandomUnitFloat(current.rng) * 100.0F < epoch.child_mortality_percent) {
      continue;
    }
    const ResidentRow mother = current.residents.rows[mother_row];
    const std::uint32_t father_row = FindRow(current.residents, mother.spouse);
    const ResidentRow father = father_row == kNoRow ? mother : current.residents.rows[father_row];
    ResidentRow child;
    child.family = mother.family;
    child.mother = current.residents.row_ids[mother_row];
    child.father = mother.spouse;
    child.sex = DrawNewbornSex(config, current, current.rng, day);
    child.birth_day = static_cast<std::int32_t>(day);
    // Born-with inclinations: random with a weak parental pull
    // (ASSUMPTION on the pull strengths until playtests).
    child.intellect = BlendInclination(current.rng, mother.intellect, father.intellect, 0.25F);
    child.stamina = BlendInclination(current.rng, mother.stamina, father.stamina, 0.15F);
    child.optimism = BlendInclination(current.rng, mother.optimism, father.optimism, 0.15F);
    // Ideology starts from the parents' and forms until 16 (metrics design
    // §2; membership.h, TurnIdeologyYear).
    child.ideology = BirthIdeology(
        config.membership, current.rng, mother, father_row == kNoRow ? nullptr : &father);
    child.satiety = 70.0F;
    child.health = DrawInRange(current.rng, 70.0F, 95.0F);
    // THE FIGURE TAKES AFTER THE PARENTS and costs the RNG stream nothing:
    // it is a counter hash keyed by the id this child is ABOUT to be given
    // (core_common/body.h). Drawing it from `current.rng` would reroll every
    // later decision of the world, which is not a theory — the first version
    // did exactly that, and the balance runs caught it by losing truancy and
    // a slice of the year's labour.
    RollBodyFromParents(
        current.world_seed, current.residents.next_id_value, config.body, mother, father, child);
    const ResidentId child_id = AppendRow(current.residents, child);
    // Said out loud where it happens. Until 2026-09-05 this line and the
    // three below it were silent: a hundred and seventy-three children were
    // born over four hundred days and the journal carried none of them,
    // while a vacated post two hundred lines above this one announced
    // itself correctly.
    SimEvent& born = EmitEvent(current, EventKind::kResidentBorn, EventSeverity::kNotable);
    born.resident = child_id;
    born.family = child.family;
    // Counted here and not from `mothers`: the child-mortality draw above
    // skips some of them, and a birth nobody survived is not a birth the
    // village saw.
    current.ledger.current.births += 1;
  }
}

/// @brief Close kin may not marry: same household, a shared parent
/// (maternal or paternal half-siblings included), or a direct
/// parent-child pair (possible after remarriage).
bool AreCloseKin(const ResidentRow& bride,
                 ResidentId bride_id,
                 const ResidentRow& groom,
                 ResidentId groom_id) {
  if (groom.family.value == bride.family.value) {
    return true;
  }
  const bool shared_mother =
      groom.mother.value != kInvalidEntityIdValue && groom.mother.value == bride.mother.value;
  const bool shared_father =
      groom.father.value != kInvalidEntityIdValue && groom.father.value == bride.father.value;
  const bool parent_child =
      groom.mother.value == bride_id.value || bride.father.value == groom_id.value ||
      bride.mother.value == groom_id.value || groom.father.value == bride_id.value;
  return shared_mother || shared_father || parent_child;
}

/// A new household is not conjured out of nothing. The couple comes from
/// two existing yards, and food comes with them — the dowry is the oldest
/// mechanism there is, and here it is also the difference between a
/// village that grows and one that does not.
///
/// Without it every wedding created a family with an EMPTY larder, living
/// on the nets and the monthly issue until the garden paid in September.
/// Those families sat under the hunger stop of decision 106 through most
/// of their fertile years, and since the newly-weds are exactly the people
/// who would have children, the settlement's whole curve halved: 672 by
/// year 33 against the reference run's 1500. The share is ASSUMPTION; that
/// something must move is not.
void PassDowry(WorldState& current, FamilyId from, FamilyId to) {
  const std::uint32_t source = FindRow(current.families, from);
  const std::uint32_t target = FindRow(current.families, to);
  if (source == kNoRow || target == kNoRow || source == target) {
    return;
  }
  constexpr float kDowryShare = 0.25F;
  ResourceAmounts& parents = current.families.rows[source].pantry;
  ResourceAmounts& newlyweds = current.families.rows[target].pantry;
  if (newlyweds.size() < parents.size()) {
    newlyweds.resize(parents.size(), 0);
  }
  for (std::uint32_t index = 0; index < parents.size(); ++index) {
    const auto share = GramsFromFloat(static_cast<float>(parents[index]) * kDowryShare);
    parents[index] -= share;
    newlyweds[index] += share;
  }
}

/// @brief Whether the month is warm enough for a tent (housing design §20,
/// "только в тёплое время"): May to September. The months are the core's
/// reading of the design's words, named here (STUB until a temperature rule
/// is written for it).
bool TentWeather(Month month) {
  return month >= Month::kMay && month <= Month::kSeptember;
}

/// @brief Families whose house is gone go down housing design §20's ladder,
/// the same for every way a roof is lost: a free house; the barrack (STUB —
/// the core keeps one family to a unit, so the rung is skipped); a tent on
/// the old plot in the warm months; and when the cold comes with nowhere to
/// go, the family leaves the kolkhoz for good. Runs first in the day, so a
/// house freed yesterday goes to a family out in the open before any couple.
///
/// Until 2026-09-14 a house was raised from nothing instead (boss, parcel
/// 257).
void RunRoofless(const LifeConfig& config, WorldState& current) {
  std::vector<FamilyId> leaving;
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    FamilyRow& family = current.families.rows[row];
    if (family.house.value != kInvalidEntityIdValue &&
        FindRow(current.units, family.house) != kNoRow) {
      continue;
    }
    const FamilyId id = current.families.row_ids[row];
    const UnitId house = FreeHouse(config, current);
    if (house.value != kInvalidEntityIdValue) {
      const std::uint32_t house_row = FindRow(current.units, house);
      current.units.rows[house_row].household = id;
      family.house = house;
      family.lost_house_position = current.units.rows[house_row].position;
      family.in_tent = 0;
      continue;
    }
    family.house = UnitId{};
    if (TentWeather(current.calendar.date.month)) {
      if (family.in_tent == 0) {
        family.in_tent = 1;
        SimEvent& tent = EmitEvent(current, EventKind::kFamilyInTent, EventSeverity::kNotable);
        tent.family = id;
      }
      continue;
    }
    leaving.push_back(id);
  }
  for (const FamilyId family : leaving) {
    std::vector<ResidentId> members;
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      if (current.residents.rows[row].family.value == family.value) {
        members.push_back(current.residents.row_ids[row]);
      }
    }
    SimEvent& gone =
        EmitEvent(current, EventKind::kFamilyLeftForNoHouse, EventSeverity::kInterrupting);
    gone.family = family;
    gone.amount = static_cast<std::int64_t>(members.size());
    for (const ResidentId member : members) {
      SimEvent& left = EmitEvent(current, EventKind::kResidentLeft, EventSeverity::kNotable);
      left.resident = member;
      left.family = family;
      RemoveResident(current, member);
    }
    current.ledger.current.departures += static_cast<std::uint32_t>(members.size());
    // A family with nobody in it was dropped by the last RemoveResident; one
    // that had nobody to begin with goes here.
    DropFamilyIfEmpty(current, family);
  }
}

/// @brief Whether `resident` is half of a couple waiting for a house.
bool WaitsForHouse(const WorldState& current, ResidentId resident) {
  for (const WeddingWaitRow& couple : current.wedding_waits.rows) {
    if (couple.bride.value == resident.value || couple.groom.value == resident.value) {
      return true;
    }
  }
  return false;
}

/// @brief The wedding itself, into the free house `house`: a new household,
/// the dowries, the two moved in, their old yards dropped if emptied.
void Wed(WorldState& current, ResidentId bride_id, ResidentId groom_id, UnitId house) {
  const std::uint32_t bride_row = FindRow(current.residents, bride_id);
  const std::uint32_t groom_row = FindRow(current.residents, groom_id);
  const std::uint32_t house_row = FindRow(current.units, house);
  if (bride_row == kNoRow || groom_row == kNoRow || house_row == kNoRow) {
    return;
  }
  FamilyRow household;
  household.house = house;
  household.lost_house_position = current.units.rows[house_row].position;
  const FamilyId home = AppendRow(current.families, household);
  current.units.rows[house_row].household = home;
  const FamilyId bride_was = current.residents.rows[bride_row].family;
  const FamilyId groom_was = current.residents.rows[groom_row].family;
  PassDowry(current, bride_was, home);
  PassDowry(current, groom_was, home);
  current.residents.rows[bride_row].spouse = groom_id;
  current.residents.rows[bride_row].family = home;
  current.residents.rows[groom_row].spouse = bride_id;
  current.residents.rows[groom_row].family = home;
  // The bride and the household that did not exist a line ago, which is
  // what the kind's contract asks for (event_state.h).
  SimEvent& wedding = EmitEvent(current, EventKind::kWedding, EventSeverity::kNotable);
  wedding.resident = bride_id;
  wedding.family = home;
  current.ledger.current.weddings += 1;
  // Both parents' yards may now stand empty — a household of one that
  // married out leaves nothing behind but its books.
  DropFamilyIfEmpty(current, bride_was);
  DropFamilyIfEmpty(current, groom_was);
}

/// @brief The couples waiting for a house, oldest first (life-cycle §12;
/// wedding_state.h): a couple one of whose two is gone falls apart; the
/// first couple still whole marries into the first free house, and so on
/// while free houses last.
///
/// THE CHAIRMAN'S DECISION IS A STUB (§13: the application, the approval, a
/// date on a non-working day, the house reserved): the couple marries on the
/// day a free house stands (boss, parcel 257).
///
/// THE QUEUE IS since_day, NOT THE ROW ORDER: a removal swaps the table's last
/// row into the hole (RemoveRow), so after the first couple left the queue a
/// younger one could stand above an older one and marry first. Equal days go
/// by id, which is the order the couples joined.
void RunWeddingQueue(const LifeConfig& config, WorldState& current) {
  std::vector<std::uint32_t> queue(current.wedding_waits.rows.size());
  std::iota(queue.begin(), queue.end(), 0U);
  std::ranges::sort(queue, [&current](std::uint32_t left, std::uint32_t right) {
    const std::uint32_t left_day = current.wedding_waits.rows[left].since_day;
    const std::uint32_t right_day = current.wedding_waits.rows[right].since_day;
    if (left_day != right_day) {
      return left_day < right_day;
    }
    return current.wedding_waits.row_ids[left].value < current.wedding_waits.row_ids[right].value;
  });
  std::vector<WeddingWaitId> done;
  for (const std::uint32_t row : queue) {
    const WeddingWaitRow couple = current.wedding_waits.rows[row];
    const WeddingWaitId id = current.wedding_waits.row_ids[row];
    const std::uint32_t bride_row = FindRow(current.residents, couple.bride);
    const std::uint32_t groom_row = FindRow(current.residents, couple.groom);
    if (bride_row == kNoRow || groom_row == kNoRow ||
        current.residents.rows[bride_row].spouse.value != kInvalidEntityIdValue ||
        current.residents.rows[groom_row].spouse.value != kInvalidEntityIdValue) {
      done.push_back(id);  // one of the two died or left: the couple falls apart
      continue;
    }
    const UnitId house = FreeHouseForCouple(config, current);
    if (house.value == kInvalidEntityIdValue) {
      break;  // no free house for the oldest, so none for anyone behind it
    }
    Wed(current, couple.bride, couple.groom, house);
    done.push_back(id);
  }
  for (const WeddingWaitId id : done) {
    RemoveRow(current.wedding_waits, id);
  }
}

void RunMarriages(const LifeConfig& config, WorldState& current, SimDay day) {
  // Brides draw the daily chance in row order; the groom is the first
  // eligible bachelor who is not close kin. THE HOUSING GATE HOLDS
  // (life-cycle §12, "a wedding takes place only when a free house is ready
  // for the couple"): with a free house the couple marries at once, unless
  // older couples are waiting for one — the queue goes first; without, it
  // joins the queue and looks for nobody else. Until 2026-09-14 a house was
  // raised from nothing here instead (boss, parcel 257).
  const float daily_chance = config.marriage_chance_percent_per_day / 100.0F;
  for (std::uint32_t bride_row = 0; bride_row < current.residents.rows.size(); ++bride_row) {
    if (current.residents.rows[bride_row].sex != Sex::kFemale ||
        current.residents.rows[bride_row].spouse.value != kInvalidEntityIdValue ||
        BiologicalAgeYears(config.life_speedup, current.residents.rows[bride_row].birth_day, day) <
            config.marriage_age_years ||
        WaitsForHouse(current, current.residents.row_ids[bride_row])) {
      continue;
    }
    if (NextRandomUnitFloat(current.rng) >= daily_chance) {
      continue;
    }
    const ResidentId bride_id = current.residents.row_ids[bride_row];
    for (std::uint32_t groom_row = 0; groom_row < current.residents.rows.size(); ++groom_row) {
      const ResidentRow& groom = current.residents.rows[groom_row];
      const ResidentId groom_id = current.residents.row_ids[groom_row];
      const bool eligible =
          groom.sex == Sex::kMale && groom.spouse.value == kInvalidEntityIdValue &&
          BiologicalAgeYears(config.life_speedup, groom.birth_day, day) >=
              config.marriage_age_years &&
          !WaitsForHouse(current, groom_id) &&
          !AreCloseKin(current.residents.rows[bride_row], bride_id, groom, groom_id);
      if (!eligible) {
        continue;
      }
      const UnitId house =
          current.wedding_waits.rows.empty() ? FreeHouseForCouple(config, current) : UnitId{};
      if (house.value != kInvalidEntityIdValue) {
        Wed(current, bride_id, groom_id, house);
        break;
      }
      WeddingWaitRow couple;
      couple.bride = bride_id;
      couple.groom = groom_id;
      couple.since_day = static_cast<std::uint32_t>(day);
      AppendRow(current.wedding_waits, couple);
      SimEvent& waits = EmitEvent(current, EventKind::kWeddingAwaitsHouse, EventSeverity::kNotable);
      waits.resident = bride_id;
      waits.family = current.residents.rows[bride_row].family;
      waits.amount = groom_id.value;
      break;
    }
  }
}

void RunMigration(const LifeConfig& config, WorldState& current, SimDay day) {
  // Exactly migration_per_year arrivals a year, deterministically: one
  // whenever day * rate crosses the next whole number.
  const float rate_per_day = config.migration_per_year / static_cast<float>(kDaysPerYear);
  const auto arrivals = static_cast<std::int32_t>(static_cast<float>(day) * rate_per_day) -
                        static_cast<std::int32_t>(static_cast<float>(day - 1) * rate_per_day);
  for (std::int32_t arrival = 0; arrival < arrivals; ++arrival) {
    ResidentRow migrant;
    // A MIGRANT COMES ONLY TO A FREE HOUSE (district design §2: "strangers
    // come to you if you have a free house"). Until 2026-09-14 a house was
    // raised from nothing for him too; with none free, nobody comes today.
    FamilyRow household;
    household.house = FreeHouse(config, current);
    if (household.house.value == kInvalidEntityIdValue) {
      continue;
    }
    household.lost_house_position =
        current.units.rows[FindRow(current.units, household.house)].position;
    migrant.family = AppendRow(current.families, household);
    const std::uint32_t house_row = FindRow(current.units, household.house);
    current.units.rows[house_row].household = migrant.family;
    migrant.sex = DrawNewbornSex(config, current, current.rng, day);
    // A MIGRANT HAS NO PARENTS HERE, so his figure is an independent draw,
    // like a founder's. Same counter hash, same cost to the stream: none.
    RollBody(current.world_seed, current.residents.next_id_value, config.body, migrant);
    const float age =
        DrawInRange(current.rng, config.marriage_age_years, config.fertility_to_years);
    migrant.birth_day =
        static_cast<std::int32_t>(day) -
        static_cast<std::int32_t>(age / config.life_speedup * static_cast<float>(kDaysPerYear));
    migrant.intellect = DrawInRange(current.rng, 20.0F, 80.0F);
    migrant.stamina = DrawInRange(current.rng, 20.0F, 80.0F);
    migrant.optimism = DrawInRange(current.rng, 20.0F, 80.0F);
    migrant.ideology = DrawInRange(current.rng, 30.0F, 70.0F);
    const ResidentId migrant_id = AppendRow(current.residents, migrant);
    SimEvent& arrived = EmitEvent(current, EventKind::kResidentArrived, EventSeverity::kNotable);
    arrived.resident = migrant_id;
    arrived.family = migrant.family;
    current.ledger.current.arrivals += 1;
  }
}

/// Whether today's work is the sort that takes the cleanliness off a man.
/// «Ферма, стройка, поле» of health design §3; the tannery it also names has
/// no unit in this core.
bool DirtyWorkToday(WorkKind kind) {
  switch (kind) {
    case WorkKind::kHerdCare:
    case WorkKind::kConstruction:
    case WorkKind::kPlowing:
    case WorkKind::kHarrowing:
    case WorkKind::kSowing:
    case WorkKind::kHarvest:
    case WorkKind::kFelling:
      return true;
    default:
      return false;
  }
}

}  // namespace

/// One day of personal cleanliness (health design §3). Declared in
/// demography.h — see there for why it stands beside the day rather than
/// inside it.
///
/// IT FALLS BY ITSELF AND RISES FROM ONE THING, which is the stub and is
/// named in the config beside every knob: the design gives four risers — the
/// bathhouse, a yard's own, clean water nearby, soap and a change of linen —
/// and this core has the machinery for none of the other three. So a STANDING
/// bathhouse gives its day to everybody, «кто именно и как часто ходит» being
/// the part that is missing.
///
/// THE EVENT IS A TRANSITION AND NOT A STATE, as event_state.h requires of
/// every event: it is raised on the day a resident CROSSES the threshold
/// downwards, so there is no second field saying "ill" and no flood of the
/// same news every morning. `first_hygiene_disease` is not this core's word —
/// the seam hears every crossing and the host takes the first, the way it
/// takes `first_store_issue` from `distribution_issued`.
void RunHygiene(const LifeConfig& config, WorldState& current) {
  bool bathhouse = false;
  if (config.bathhouse_type.value != kInvalidDefIdValue) {
    bathhouse = std::any_of(
        current.units.rows.begin(), current.units.rows.end(), [&config](const UnitRow& unit) {
          return unit.type.value == config.bathhouse_type.value && unit.level >= 1 &&
                 unit.dead == 0;
        });
  }
  // THE AFTERNOON AND NOT THE DAY'S MEAN, which is the same day the weather
  // calls hot (time_system.cpp: `mean + swing >= hot_afternoon_c`, raising
  // kHotAfternoon). Until 2026-09-17 this line compared the MEAN against a
  // literal 25 and the surcharge never fired once in sixty days across nine
  // villages — measured, not suspected. The swing lives in the state for
  // exactly this: its own note says production used to keep a copy of the
  // season amplitudes to compute the afternoon, "one number with two homes".
  const float afternoon =
      current.weather.air_temperature_celsius + current.weather.temperature_swing_celsius;
  const bool hot = afternoon >= config.hot_afternoon_celsius;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    ResidentRow& person = current.residents.rows[row];
    const float before = person.hygiene;
    float fall = config.hygiene_fall_per_day;
    if (DirtyWorkToday(person.work.kind)) {
      fall *= config.hygiene_fall_dirty_work_factor;
    }
    if (hot) {
      fall += config.hygiene_fall_heat_extra;
    }
    const float rise = bathhouse ? config.hygiene_rise_bath_per_day : 0.0F;
    person.hygiene = std::clamp(person.hygiene - fall + rise, kMetricMin, kMetricMax);
    // The crossing, and only downwards: a man who was above the line
    // yesterday and is below it today has caught what the filth gives.
    if (before >= config.hygiene_disease_threshold &&
        person.hygiene < config.hygiene_disease_threshold) {
      SimEvent& caught = EmitEvent(current, EventKind::kHygieneDisease, EventSeverity::kNotable);
      caught.resident = current.residents.row_ids[row];
      caught.family = person.family;
    }
  }
}

void RunDemographyDay(const LifeConfig& config, WorldState& current) {
  const SimDay day = current.calendar.day;
  // The era is NOT moved here any more. Until 2026-09-18 this line moved it
  // at 500 and 1200 people — a second door beside the chairman's order that
  // walked round every block of the transition (order_state.h, kAdvanceEra).
  const EpochDemography& epoch = config.epochs[EpochIndex(current.epoch)];
  RunRoofless(config, current);
  // Cleanliness before the deaths and the births, so that a man who caught
  // lice this morning is still in the table to be told about.
  RunHygiene(config, current);
  RunDeaths(config, current, day);
  RunOutflow(config, current, epoch, day);
  RunBirths(config, current, epoch, day);
  RunWeddingQueue(config, current);
  RunMarriages(config, current, day);
  RunMigration(config, current, day);
}

}  // namespace core
