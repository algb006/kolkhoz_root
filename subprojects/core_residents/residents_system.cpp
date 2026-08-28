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
#include <vector>

#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "life_config.h"

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
  const LifeConfig* config_;
};

class ResidentsSystem final : public IResidentsSystem {
 public:
  explicit ResidentsSystem(const LifeConfig& config) : config_(config), metrics_phase_(config_) {}

  IParallelPhase& NeedsPhase() override { return needs_phase_; }

  IParallelPhase& MetricsPhase() override { return metrics_phase_; }

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

  void RunMarriages(WorldState& current, SimDay day) {
    // Eligible singles marry quickly (the housing gate arrives with units,
    // stage 4 — STUB: houses are not required yet). Brides draw the daily
    // chance in row order; the groom is the first eligible bachelor who is
    // not close kin.
    const float daily_chance = config_.marriage_chance_percent_per_day / 100.0F;
    for (std::uint32_t bride_row = 0; bride_row < current.residents.rows.size(); ++bride_row) {
      ResidentRow& bride = current.residents.rows[bride_row];
      if (bride.sex != Sex::kFemale || bride.spouse.value != kInvalidEntityIdValue ||
          BiologicalAgeYears(config_, bride.birth_day, day) < config_.marriage_age_years) {
        continue;
      }
      if (NextRandomUnitFloat(current.rng) >= daily_chance) {
        continue;
      }
      for (std::uint32_t groom_row = 0; groom_row < current.residents.rows.size(); ++groom_row) {
        ResidentRow& groom = current.residents.rows[groom_row];
        const bool eligible =
            groom.sex == Sex::kMale && groom.spouse.value == kInvalidEntityIdValue &&
            BiologicalAgeYears(config_, groom.birth_day, day) >= config_.marriage_age_years &&
            groom.family.value != bride.family.value &&
            (groom.mother.value == kInvalidEntityIdValue ||
             groom.mother.value != bride.mother.value);
        if (!eligible) {
          continue;
        }
        const FamilyId home = AppendRow(current.families, FamilyRow{});
        bride.spouse = current.residents.row_ids[groom_row];
        bride.family = home;
        groom.spouse = current.residents.row_ids[bride_row];
        groom.family = home;
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

  NeedsStubPhase needs_phase_;

  FamilyMetricsPhase metrics_phase_;
};

/// @brief Reads one key's value cell from a key/value table.
bool ReadValue(const ITable& table, std::string_view key, float& value, std::string& error) {
  const std::uint32_t row = table.FindRowByKey(key);
  const std::uint32_t column = table.FindColumn("value");
  if (row == kNoTableRow || column == kNoTableColumn) {
    error = "life: no row '" + std::string(key) + "' or no value column";
    return false;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    error = "life: value of '" + std::string(key) + "' is not a number";
    return false;
  }
  value = *cell;
  return true;
}

bool ParseLifeTable(const ITable& table, LifeConfig& config, std::string& error) {
  float epoch2 = 0.0F;
  float epoch3 = 0.0F;
  const bool ok =
      ReadValue(table, "life_speedup", config.life_speedup, error) &&
      ReadValue(table, "adult_age_years", config.adult_age_years, error) &&
      ReadValue(table, "marriage_age_years", config.marriage_age_years, error) &&
      ReadValue(table, "fertility_from_years", config.fertility_from_years, error) &&
      ReadValue(table, "fertility_to_years", config.fertility_to_years, error) &&
      ReadValue(table, "mortality_age_mid_years", config.mortality_age_mid_years, error) &&
      ReadValue(table, "mortality_age_old_years", config.mortality_age_old_years, error) &&
      ReadValue(table,
                "mortality_young_percent_per_year",
                config.mortality_young_percent_per_year,
                error) &&
      ReadValue(
          table, "mortality_mid_percent_per_year", config.mortality_mid_percent_per_year, error) &&
      ReadValue(
          table, "mortality_old_percent_per_year", config.mortality_old_percent_per_year, error) &&
      ReadValue(table, "migration_per_year", config.migration_per_year, error) &&
      ReadValue(table, "epoch2_population", epoch2, error) &&
      ReadValue(table, "epoch3_population", epoch3, error) &&
      ReadValue(table,
                "marriage_chance_percent_per_day",
                config.marriage_chance_percent_per_day,
                error) &&
      ReadValue(table, "sex_balance_gain", config.sex_balance_gain, error);
  if (!ok) {
    return false;
  }
  config.epoch2_population = static_cast<std::uint32_t>(epoch2);
  config.epoch3_population = static_cast<std::uint32_t>(epoch3);
  return true;
}

bool ParseEpochRows(const ITable& table, LifeConfig& config, std::string& error) {
  constexpr std::array<std::string_view, 3> kKeys = {"epoch_1", "epoch_2", "epoch_3"};
  const std::uint32_t children_column = table.FindColumn("children_per_family");
  const std::uint32_t mortality_column = table.FindColumn("child_mortality_percent");
  const std::uint32_t outflow_column = table.FindColumn("outflow_percent_per_year");
  if (children_column == kNoTableColumn || mortality_column == kNoTableColumn ||
      outflow_column == kNoTableColumn) {
    error = "demography: a required column is missing";
    return false;
  }
  for (std::uint32_t index = 0; index < kKeys.size(); ++index) {
    const std::uint32_t row = table.FindRowByKey(kKeys[index]);
    const auto children = table.CellReal(row, children_column);
    const auto mortality = table.CellReal(row, mortality_column);
    const auto outflow = table.CellReal(row, outflow_column);
    if (row == kNoTableRow || !children || !mortality || !outflow) {
      error = "demography: row '" + std::string(kKeys[index]) + "' is missing or not numeric";
      return false;
    }
    config.epochs[index] = {.children_per_family = *children,
                            .child_mortality_percent = *mortality,
                            .outflow_percent_per_year = *outflow};
  }
  return true;
}

bool ParseWeightRows(const ITable& table, LifeConfig& config, std::string& error) {
  constexpr std::array<std::string_view, 3> kKeys = {"epoch_1", "epoch_2", "epoch_3"};
  const std::uint32_t satiety_column = table.FindColumn("weight_satiety");
  const std::uint32_t common_column = table.FindColumn("weight_common_cause");
  const std::uint32_t needs_column = table.FindColumn("weight_needs");
  const std::uint32_t rest_column = table.FindColumn("weight_rest");
  if (satiety_column == kNoTableColumn || common_column == kNoTableColumn ||
      needs_column == kNoTableColumn || rest_column == kNoTableColumn) {
    error = "satisfaction: a required column is missing";
    return false;
  }
  for (std::uint32_t index = 0; index < kKeys.size(); ++index) {
    const std::uint32_t row = table.FindRowByKey(kKeys[index]);
    const auto satiety = table.CellReal(row, satiety_column);
    const auto common = table.CellReal(row, common_column);
    const auto needs = table.CellReal(row, needs_column);
    const auto rest = table.CellReal(row, rest_column);
    if (row == kNoTableRow || !satiety || !common || !needs || !rest) {
      error = "satisfaction: row '" + std::string(kKeys[index]) + "' is missing or not numeric";
      return false;
    }
    config.weights[index] = {
        .satiety = *satiety, .common_cause = *common, .needs = *needs, .rest = *rest};
  }
  return true;
}

}  // namespace

std::unique_ptr<IResidentsSystem> CreateResidentsSystem(const ITableSet& tables) {
  LifeConfig config;
  std::string error;
  if (const ITable* life = tables.FindTable("life")) {
    if (!ParseLifeTable(*life, config, error)) {
      LogError(error);
      return nullptr;
    }
  }
  if (const ITable* demography = tables.FindTable("demography")) {
    if (!ParseEpochRows(*demography, config, error)) {
      LogError(error);
      return nullptr;
    }
  }
  if (const ITable* satisfaction = tables.FindTable("satisfaction")) {
    if (!ParseWeightRows(*satisfaction, config, error)) {
      LogError(error);
      return nullptr;
    }
  }
  return std::make_unique<ResidentsSystem>(config);
}

}  // namespace core
