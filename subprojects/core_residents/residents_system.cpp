// Implementation of the core_residents boundary
// (include/core_residents/residents_system.h). Stage 3: demography — births,
// deaths, marriages, migration, outflow — in the sequential demography
// sub-step, and family satisfaction in the metrics phase. The needs phase
// stays a zero-item STUB until food exists (stage 6).
//
// The demography model mirrors the cohort reference run
// (manual/balance/sim/demography.py) at per-person granularity: per-day
// probabilities are the annual rates divided by the 48-day year (compounding
// makes effective annual rates slightly lower — tuned via the tables, not
// the code). All draws come from the world's sequential RNG inside the
// single-threaded decisions slot, so replay is exact.

#include "core_residents/residents_system.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "family_exchange.h"
#include "food_config.h"
#include "life_config.h"
#include "vitals.h"

namespace core {
namespace {

float BiologicalAgeYears(const LifeConfig& config, std::int32_t birth_day, SimDay day) {
  const float game_years = static_cast<float>(static_cast<std::int32_t>(day) - birth_day) /
                           static_cast<float>(kDaysPerYear);
  return game_years * config.life_speedup;
}

std::uint32_t EpochIndex(Epoch epoch) {
  return static_cast<std::uint32_t>(epoch) - 1;
}

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
    if (BiologicalAgeYears(config, row.birth_day, day) < config.adult_age_years) {
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

/// @brief Removes a resident and repairs links: the spouse becomes widowed,
/// an emptied family disappears.
void RemoveResident(WorldState& current, ResidentId id) {
  const std::uint32_t row_index = FindRow(current.residents, id);
  if (row_index == kNoRow) {
    return;
  }
  const FamilyId family = current.residents.rows[row_index].family;
  const ResidentId spouse = current.residents.rows[row_index].spouse;
  RemoveRow(current.residents, id);
  const std::uint32_t spouse_row = FindRow(current.residents, spouse);
  if (spouse_row != kNoRow) {
    current.residents.rows[spouse_row].spouse = ResidentId{};
  }
  for (const ResidentRow& row : current.residents.rows) {
    if (row.family.value == family.value) {
      return;  // somebody still lives here
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

/// The needs slot (phase 2). STUB: nothing to compute until food and
/// heating exist (stage 6); zero items keeps the phase a no-op.
class NeedsStubPhase final : public IParallelPhase {
 public:
  std::uint32_t ParallelItemCount(const WorldState& /*current*/) const override { return 0; }

  void RunItemRange(const WorldState& /*previous*/,
                    WorldState& /*current*/,
                    std::uint32_t /*begin_item*/,
                    std::uint32_t /*end_item*/) override {}
};

/// The metrics slot (phase 6): family satisfaction from its four components
/// with the epoch weights and the low-component law (metrics design §7).
/// Parallel by family; each invocation owns its family rows.
class FamilyMetricsPhase final : public IParallelPhase {
 public:
  explicit FamilyMetricsPhase(const LifeConfig& config) : config_(&config) {}

  std::uint32_t ParallelItemCount(const WorldState& current) const override {
    return static_cast<std::uint32_t>(current.families.rows.size());
  }

  void RunItemRange(const WorldState& /*previous*/,
                    WorldState& current,
                    std::uint32_t begin_item,
                    std::uint32_t end_item) override {
    // current.epoch is finalized by the decisions slot (buffer-law rule 4).
    const SatisfactionWeights& weights = config_->weights[EpochIndex(current.epoch)];
    for (std::uint32_t item = begin_item; item < end_item; ++item) {
      FamilyRow& family = current.families.rows[item];
      UpdateRestComponent(current, item, family);
      const float weighted =
          (family.component_satiety * weights.satiety +
           family.component_common_cause * weights.common_cause +
           family.component_needs * weights.needs + family.component_rest * weights.rest) /
          100.0F;
      float lowest = family.component_satiety;
      lowest = family.component_common_cause < lowest ? family.component_common_cause : lowest;
      lowest = family.component_needs < lowest ? family.component_needs : lowest;
      lowest = family.component_rest < lowest ? family.component_rest : lowest;
      // The low-component law: a starving family is not consoled by a club.
      family.satisfaction =
          lowest < 20.0F ? (weighted < lowest * 2.0F ? weighted : lowest * 2.0F) : weighted;
    }
  }

 private:
  /// The family's rest component is its working members' own rest, averaged
  /// (metrics design §11: one number seen from two sides — the man's
  /// fatigue and the household's "is there life beyond work"). Labor moves
  /// resident rest in the decisions slot of this very step, and that block
  /// is finalized before the metrics phase runs (buffer-law rule 4).
  /// A household with nobody of working age keeps its previous value.
  static void UpdateRestComponent(const WorldState& current,
                                  std::uint32_t family_item,
                                  FamilyRow& family) {
    const FamilyId id = current.families.row_ids[family_item];
    float total = 0.0F;
    std::uint32_t counted = 0;
    for (const ResidentRow& resident : current.residents.rows) {
      if (resident.family.value == id.value) {
        total += resident.rest;
        ++counted;
      }
    }
    if (counted > 0) {
      family.component_rest = total / static_cast<float>(counted);
    }
  }

  const LifeConfig* config_;
};

class ResidentsSystem final : public IResidentsSystem {
 public:
  ResidentsSystem(const LifeConfig& config, FoodConfig food)
      : config_(config), food_(std::move(food)), metrics_phase_(config_) {}

  IParallelPhase& NeedsPhase() override { return needs_phase_; }

  IParallelPhase& MetricsPhase() override { return metrics_phase_; }

  /// The whole residents sub-step of the decisions slot, not demography
  /// alone: the name is kept because the interface is a contract, and
  /// manual/66-food-model.md §1 records what it now covers — demography,
  /// then the family/kolkhoz exchange, then the vital statistics.
  ///
  /// The order is deliberate. Demography settles who is alive today before
  /// the exchange counts eaters and hands out food, and the vitals window
  /// closes the day last, over the settlement demography has just finalized.
  void RunDemographyDecisions(const WorldState& previous, WorldState& current) override {
    if (current.calendar.day == previous.calendar.day) {
      return;  // daily work, self-gated to day boundaries
    }
    const SimDay day = current.calendar.day;
    UpdateEpoch(current);
    const EpochDemography& epoch = config_.epochs[EpochIndex(current.epoch)];
    RunDeaths(current, day);
    RunOutflow(current, epoch, day);
    RunBirths(current, epoch, day);
    RunMarriages(current, day);
    RunMigration(current, day);
    RunFamilyExchange(food_, config_.life_speedup, current);
    AccumulateVitals(current);
  }

 private:
  /// STUB: epochs switch by population thresholds, as in the reference run.
  /// The designed era events (readiness index, ceremonies) are project
  /// phase 3; the thresholds keep the balance curve comparable until then.
  void UpdateEpoch(WorldState& current) const {
    const auto population = static_cast<std::uint32_t>(current.residents.rows.size());
    if (current.epoch == Epoch::kOne && population >= config_.epoch2_population) {
      current.epoch = Epoch::kTwo;
    }
    if (current.epoch == Epoch::kTwo && population >= config_.epoch3_population) {
      current.epoch = Epoch::kThree;
    }
  }

  void RunDeaths(WorldState& current, SimDay day) const {
    std::vector<ResidentId> dead;
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      const float age = BiologicalAgeYears(config_, current.residents.rows[row].birth_day, day);
      float percent_per_year = config_.mortality_young_percent_per_year;
      if (age >= config_.mortality_age_old_years) {
        percent_per_year = config_.mortality_old_percent_per_year;
      } else if (age >= config_.mortality_age_mid_years) {
        percent_per_year = config_.mortality_mid_percent_per_year;
      }
      const float daily_chance = percent_per_year / 100.0F / static_cast<float>(kDaysPerYear);
      if (NextRandomUnitFloat(current.rng) < daily_chance) {
        dead.push_back(current.residents.row_ids[row]);
      }
    }
    for (const ResidentId id : dead) {
      RemoveResident(current, id);
    }
  }

  /// Epoch-III outflow (demography design; reference run): the young and
  /// single leave. ASSUMPTION: only unmarried working-age residents go —
  /// families are indivisible and mass leave papers are not given.
  void RunOutflow(WorldState& current, const EpochDemography& epoch, SimDay day) const {
    if (epoch.outflow_percent_per_year <= 0.0F) {
      return;
    }
    const float daily_chance =
        epoch.outflow_percent_per_year / 100.0F / static_cast<float>(kDaysPerYear);
    std::vector<ResidentId> leaving;
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      const ResidentRow& resident = current.residents.rows[row];
      const float age = BiologicalAgeYears(config_, resident.birth_day, day);
      const bool working_age =
          age >= config_.adult_age_years && age < config_.mortality_age_old_years;
      if (working_age && resident.spouse.value == kInvalidEntityIdValue &&
          NextRandomUnitFloat(current.rng) < daily_chance) {
        leaving.push_back(current.residents.row_ids[row]);
      }
    }
    for (const ResidentId id : leaving) {
      RemoveResident(current, id);
    }
  }

  void RunBirths(WorldState& current, const EpochDemography& epoch, SimDay day) {
    // Children per family are spread over the fertile window, as in the
    // reference run: rate per game year = children / fertile game years.
    const float fertile_game_years =
        (config_.fertility_to_years - config_.fertility_from_years) / config_.life_speedup;
    const float daily_chance =
        epoch.children_per_family / fertile_game_years / static_cast<float>(kDaysPerYear);
    std::vector<std::uint32_t> mothers;
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      const ResidentRow& resident = current.residents.rows[row];
      if (resident.sex != Sex::kFemale || resident.spouse.value == kInvalidEntityIdValue) {
        continue;
      }
      const float age = BiologicalAgeYears(config_, resident.birth_day, day);
      if (age >= config_.fertility_from_years && age < config_.fertility_to_years &&
          NextRandomUnitFloat(current.rng) < daily_chance) {
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
      child.sex = DrawNewbornSex(config_, current, current.rng, day);
      child.birth_day = static_cast<std::int32_t>(day);
      // Born-with inclinations: random with a weak parental pull
      // (ASSUMPTION on the pull strengths until playtests).
      child.intellect = BlendInclination(current.rng, mother.intellect, father.intellect, 0.25F);
      child.stamina = BlendInclination(current.rng, mother.stamina, father.stamina, 0.15F);
      child.optimism = BlendInclination(current.rng, mother.optimism, father.optimism, 0.15F);
      // STUB: ideology forms from childhood conditions and locks at 16
      // (metrics design §2); until schools exist it rolls uniform.
      child.ideology = DrawInRange(current.rng, 30.0F, 70.0F);
      child.satiety = 70.0F;
      child.health = DrawInRange(current.rng, 70.0F, 95.0F);
      AppendRow(current.residents, child);
    }
  }

  /// @brief Close kin may not marry: same household, a shared parent
  /// (maternal or paternal half-siblings included), or a direct
  /// parent-child pair (possible after remarriage).
  static bool AreCloseKin(const ResidentRow& bride,
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

  void RunMarriages(WorldState& current, SimDay day) {
    // Brides draw the daily chance in row order; the groom is the first
    // eligible bachelor who is not close kin. The design's housing gate
    // ("no free house — no wedding", families design §2) is a STUB until
    // construction exists: every wedding instantly gets a free house unit,
    // so the gate never blocks — but the house and the family-house link
    // are real from here on.
    const float daily_chance = config_.marriage_chance_percent_per_day / 100.0F;
    for (std::uint32_t bride_row = 0; bride_row < current.residents.rows.size(); ++bride_row) {
      if (current.residents.rows[bride_row].sex != Sex::kFemale ||
          current.residents.rows[bride_row].spouse.value != kInvalidEntityIdValue ||
          BiologicalAgeYears(config_, current.residents.rows[bride_row].birth_day, day) <
              config_.marriage_age_years) {
        continue;
      }
      if (NextRandomUnitFloat(current.rng) >= daily_chance) {
        continue;
      }
      const ResidentId bride_id = current.residents.row_ids[bride_row];
      for (std::uint32_t groom_row = 0; groom_row < current.residents.rows.size(); ++groom_row) {
        const ResidentRow& groom = current.residents.rows[groom_row];
        const bool eligible =
            groom.sex == Sex::kMale && groom.spouse.value == kInvalidEntityIdValue &&
            BiologicalAgeYears(config_, groom.birth_day, day) >= config_.marriage_age_years &&
            !AreCloseKin(current.residents.rows[bride_row],
                         bride_id,
                         groom,
                         current.residents.row_ids[groom_row]);
        if (!eligible) {
          continue;
        }
        FamilyRow household;
        UnitRow house;
        house.type = config_.house_type;
        household.house = AppendRow(current.units, house);
        const FamilyId home = AppendRow(current.families, household);
        const std::uint32_t house_row = FindRow(current.units, household.house);
        current.units.rows[house_row].household = home;
        const ResidentId groom_id = current.residents.row_ids[groom_row];
        current.residents.rows[bride_row].spouse = groom_id;
        current.residents.rows[bride_row].family = home;
        current.residents.rows[groom_row].spouse = bride_id;
        current.residents.rows[groom_row].family = home;
        break;
      }
    }
  }

  void RunMigration(WorldState& current, SimDay day) {
    // Exactly migration_per_year arrivals a year, deterministically: one
    // whenever day * rate crosses the next whole number.
    const float rate_per_day = config_.migration_per_year / static_cast<float>(kDaysPerYear);
    const auto arrivals = static_cast<std::int32_t>(static_cast<float>(day) * rate_per_day) -
                          static_cast<std::int32_t>(static_cast<float>(day - 1) * rate_per_day);
    for (std::int32_t arrival = 0; arrival < arrivals; ++arrival) {
      ResidentRow migrant;
      migrant.family = AppendRow(current.families, FamilyRow{});
      migrant.sex = DrawNewbornSex(config_, current, current.rng, day);
      const float age =
          DrawInRange(current.rng, config_.marriage_age_years, config_.fertility_to_years);
      migrant.birth_day =
          static_cast<std::int32_t>(day) -
          static_cast<std::int32_t>(age / config_.life_speedup * static_cast<float>(kDaysPerYear));
      migrant.intellect = DrawInRange(current.rng, 20.0F, 80.0F);
      migrant.stamina = DrawInRange(current.rng, 20.0F, 80.0F);
      migrant.optimism = DrawInRange(current.rng, 20.0F, 80.0F);
      migrant.ideology = DrawInRange(current.rng, 30.0F, 70.0F);
      AppendRow(current.residents, migrant);
    }
  }

  LifeConfig config_;

  FoodConfig food_;

  NeedsStubPhase needs_phase_;

  FamilyMetricsPhase metrics_phase_;
};

}  // namespace

std::unique_ptr<IResidentsSystem> CreateResidentsSystem(const ITableSet& tables) {
  LifeConfig life;
  std::string error;
  if (!ParseLifeConfig(tables, life, error)) {
    LogError(error);
    return nullptr;
  }
  FoodConfig food = ParseFoodConfig(tables, &error);
  if (!error.empty()) {
    LogError(error);
    return nullptr;
  }
  // The cold metric is not "zero cold", it is "cold is not counted": phase 1
  // has neither firewood nor unit heating (plan §11), so ResidentRow::cold
  // never moves. Said out loud at world creation so that a run showing
  // suspiciously even health through the winter explains itself.
  LogInfo("cold is not simulated in phase 1 (STUB): ResidentRow::cold stays at its default");
  return std::make_unique<ResidentsSystem>(life, std::move(food));
}

}  // namespace core
