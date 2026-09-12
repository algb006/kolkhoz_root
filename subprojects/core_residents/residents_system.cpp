// Implementation of the core_residents boundary
// (include/core_residents/residents_system.h): the module's three slots and
// the demography that has been in it since stage 3 — births, deaths,
// marriages, migration, outflow.
//
// What each slot holds after stage 6 (manual/66-food-model.md §1):
//   needs      the family's daily meal out of its own pantry (family_meal);
//   decisions  demography, then the family/kolkhoz exchange
//              (family_exchange), then the vitals window (vitals);
//   metrics    the plot and its garden (household_plot), the satiety and
//              rest components, and the satisfaction aggregate.
//
// The heavy lifting lives in those files; what is left here is the
// demography itself, the phase objects and the factory.
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
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/definitions.h"
#include "core_common/body.h"
#include "core_common/calendar.h"
#include "core_common/plot.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/required_tables.h"
#include "core_tables/tables.h"
#include "demography.h"
#include "family_exchange.h"
#include "family_meal.h"
#include "food_config.h"
#include "household_plot.h"
#include "life_config.h"
#include "vitals.h"

namespace core {
namespace {

class FamilyNeedsPhase final : public IParallelPhase {
 public:
  FamilyNeedsPhase(const FoodConfig& food, float life_speedup)
      : food_(&food), life_speedup_(life_speedup) {}

  std::uint32_t ParallelItemCount(const WorldState& current) const override {
    return static_cast<std::uint32_t>(current.families.rows.size());
  }

  void RunItemRange(const WorldState& previous,
                    WorldState& current,
                    std::uint32_t begin_item,
                    std::uint32_t end_item) override {
    for (std::uint32_t item = begin_item; item < end_item; ++item) {
      RunFamilyMeal(*food_, life_speedup_, previous, current, item);
    }
  }

 private:
  const FoodConfig* food_;

  float life_speedup_;
};

/// The metrics slot (phase 5): family satisfaction from its four components
/// with the epoch weights and the low-component law (metrics design §7).
/// Parallel by family; each invocation owns its family rows.
class FamilyMetricsPhase final : public IParallelPhase {
 public:
  FamilyMetricsPhase(const LifeConfig& config, const FoodConfig& food)
      : config_(&config), food_(&food) {}

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
      // The plot goes first: the day's hours are an input to nothing here
      // yet, but the garden it pays out feeds tomorrow's meal, and both
      // belong to the same owned row.
      RunHouseholdPlot(*food_, config_->life_speedup, current, item);
      FamilyRow& family = current.families.rows[item];
      family.component_satiety = SatietyComponent(*food_, current, item);
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

  const FoodConfig* food_;
};

class ResidentsSystem final : public IResidentsSystem {
 public:
  ResidentsSystem(const LifeConfig& config, FoodConfig food)
      : config_(config),
        food_(std::move(food)),
        needs_phase_(food_, config_.life_speedup),
        metrics_phase_(config_, food_) {}

  IParallelPhase& NeedsPhase() override { return needs_phase_; }

  IParallelPhase& MetricsPhase() override { return metrics_phase_; }

  /// Every family whose mean member satiety has fallen to the floor at which
  /// the kolkhoz owes the safety ration.
  ///
  /// ONE HOME for the predicate, and it has two readers: CollectAlarms turns
  /// them into kFamilyGoingHungry, and the food light reads "is anybody
  /// hungry right now" as its red. Two copies of a threshold drift the day
  /// somebody adds a reason to one of them.
  ///
  /// The THRESHOLD is the ration's, but this is not a report that the ration
  /// is running — it holds whether or not the auto-rule is armed, because it
  /// speaks of the trouble and not of the treatment (boss, 2026-09-03;
  /// alarm_state.h).
  ///
  /// Raw member satiety, not FamilyRow::component_satiety, for the same
  /// reason the ration itself uses it: the component is capped by the
  /// variety ceiling, and a family living on nothing but bread would raise
  /// a hunger alarm with full bins.
  void CollectHungryFamilies(const WorldState& completed, std::vector<FamilyId>& out) const {
    const std::size_t families = completed.families.rows.size();
    if (families == 0) {
      return;
    }
    // One pass over the residents rather than one pass per family: the same
    // answer, and it does not become quadratic on a grown village.
    std::vector<float> satiety_sum(families, 0.0F);
    std::vector<std::uint32_t> counted(families, 0);
    for (const ResidentRow& resident : completed.residents.rows) {
      const std::uint32_t row = FindRow(completed.families, resident.family);
      if (row == kNoRow) {
        continue;
      }
      satiety_sum[row] += resident.satiety;
      ++counted[row];
    }
    for (std::uint32_t row = 0; row < families; ++row) {
      if (counted[row] == 0) {
        continue;  // an empty household eats nothing and triggers nothing
      }
      const float satiety = satiety_sum[row] / static_cast<float>(counted[row]);
      if (satiety > food_.distribution.ration_satiety_threshold) {
        continue;
      }
      out.push_back(completed.families.row_ids[row]);
    }
  }

  /// kFamilyGoingHungry, one per hungry family, in family row order — the
  /// session sorts by id.
  void CollectAlarms(const WorldState& completed, std::vector<Alarm>& alarms) const override {
    std::vector<FamilyId> hungry;
    CollectHungryFamilies(completed, hungry);
    for (const FamilyId family : hungry) {
      Alarm alarm;
      alarm.kind = AlarmKind::kFamilyGoingHungry;
      alarm.family = family;
      alarms.push_back(alarm);
    }
  }

  /// Everything edible of one resource the settlement can reach: the stores
  /// AND the family pantries. A forecast that counted only the stores would
  /// go yellow every spring with the pantries full, and a light that cries
  /// wolf is a light nobody reads.
  static Grams EdibleHeld(const WorldState& world, ResourceId resource) {
    Grams total = 0;
    for (const UnitRow& unit : world.units.rows) {
      if (unit.level != 0) {
        total += AmountOf(unit.stock, resource);
      }
    }
    for (const FamilyRow& family : world.families.rows) {
      total += AmountOf(family.pantry, resource);
    }
    return total;
  }

  bool AnyoneGoingHungry(const WorldState& completed) const {
    std::vector<FamilyId> hungry;
    CollectHungryFamilies(completed, hungry);
    return !hungry.empty();
  }

  /// The one derived number of the figure (residents_system.h). Adulthood is
  /// the life table's own threshold, read against BIOLOGICAL years like every
  /// other age rule in this module — calendar years against a biological
  /// threshold is the mistake that once read a village of adults as a village
  /// of children.
  float HeightMeters(const WorldState& completed, ResidentId resident) const override {
    const std::uint32_t row = FindRow(completed.residents, resident);
    if (row == kNoRow) {
      return 0.0F;
    }
    const ResidentRow& person = completed.residents.rows[row];
    const float age =
        BiologicalAgeYears(config_.life_speedup, person.birth_day, completed.calendar.day);
    // THE AGE AND NOTHING ELSE. This used to hand over a BOOLEAN — "is he an
    // adult" — computed here against life.csv's adult year, and the answer
    // for a child was zero. Since 2026-09-13 the figure carries four bands of
    // childhood and their fractions, and the rule lives with them: a caller
    // that decided the band here would be the rule's second home.
    return core::HeightMeters(person, config_.body, age);
  }

  void CollectStockForecast(const WorldState& completed,
                            std::int32_t days_to_harvest,
                            std::vector<StockForecast>& lights) const override {
    StockForecast food;
    food.kind = StockKind::kFood;
    food.days_to_date = days_to_harvest;

    const float need_kg = SettlementDailyNeedKilograms(food_, config_.life_speedup, completed);
    if (!(need_kg > 0.0F)) {
      // An empty village eats nothing. "Never runs out" and not "no data":
      // the question was asked and answered (stock_forecast.h).
      food.days_of_stock = kStockNeverRunsOut;
      food.light = LightFrom(food.days_of_stock, food.days_to_date, 0, false);
      lights.push_back(food);
      return;
    }

    // Everything edible, wherever it lies — stores and pantries both. A
    // forecast that counted only the stores would go yellow every spring
    // while the pantries were full, and the player would learn to ignore it.
    // Converted through calories, because the norm is a grain EQUIVALENT and
    // a tonne of potatoes is not a tonne of rye.
    const float reference = food_.consumption.grain_reference_kcal_per_gram;
    double kcal = 0.0;
    if (reference > 0.0F) {
      for (std::uint32_t resource = 0; resource < food_.resources.size(); ++resource) {
        const float density = food_.resources[resource].kcal_per_gram;
        if (!(density > 0.0F)) {
          continue;
        }
        const ResourceId id = DefIdFromIndex<ResourceIdTag>(resource);
        kcal += static_cast<double>(EdibleHeld(completed, id)) * static_cast<double>(density);
      }
    }
    const double need_kcal = static_cast<double>(need_kg) * static_cast<double>(kGramsPerKilogram) *
                             static_cast<double>(reference);
    const double days = need_kcal > 0.0 ? kcal / need_kcal : 0.0;
    food.days_of_stock = days >= static_cast<double>(kStockForecastHorizonDays)
                             ? kStockForecastHorizonDays
                             : static_cast<std::int32_t>(days);
    food.light = LightFrom(food.days_of_stock,
                           food.days_to_date,
                           static_cast<std::int32_t>(food_.consumption.food_light_margin_days),
                           AnyoneGoingHungry(completed));
    lights.push_back(food);
  }

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
    // The structural work — who dies, leaves, is born, marries, arrives,
    // and who gets a roof — lives in demography.cpp. What is left here is
    // the two things that follow from it and are not it: the family's
    // exchange with the settlement, and the vitals that accumulate.
    RunDemographyDay(config_, current);
    RunFamilyExchange(food_, config_.life_speedup, current);
    AccumulateVitals(config_, food_.satiety.health_loss_satiety_threshold, current);
  }

 private:
  LifeConfig config_;

  FoodConfig food_;

  FamilyNeedsPhase needs_phase_;

  FamilyMetricsPhase metrics_phase_;
};

}  // namespace

std::unique_ptr<IResidentsSystem> CreateResidentsSystem(const ITableSet& tables, StubTables stubs) {
  // THE DEFAULTS ARE LEGITIMATE AND THEIR SILENCE WAS NOT
  // (core_tables/stub_tables.h). A caller that has not said it wants
  // this module's documented defaults is refused by name, so that a
  // table set which is merely INCOMPLETE cannot pass for one that is
  // as its author meant it.
  //
  // THE LIST IS THE WHOLE READ SET, and it named only the first three until
  // 2026-09-08. The rest are read by this module's own config parsers, fell
  // back to their defaults when absent, and passed a check that had been
  // written to catch exactly that (core_tables/required_tables.h).
  if (!RequireTables(tables,
                     stubs,
                     "residents",
                     {"demography",
                      "life",
                      "satisfaction",
                      "resources",
                      "food",
                      "crops",
                      "labor",
                      "unit_types",
                      "world_params"},
                     nullptr)) {
    return nullptr;
  }

  LifeConfig life;
  std::string error;
  if (!ParseLifeConfig(tables, life, error)) {
    LogError(error);
    return nullptr;
  }
  // The plot rules come from the CATALOGUE, which is the one reader of
  // those columns — not from a courier hand-carrying them across a module
  // seam, which is what this was until the catalogue existed. NOTHING HERE
  // POINTS BACK AT THE TABLE SET: the catalogue holds values, not spans or
  // views into it, so the table set is free to die first. The catalogue
  // itself does NOT die with this function — it is a member of `life`, which
  // the system below stores by value, and it outlives this factory. The
  // sentence that stood here said the opposite and named the wrong owner,
  // which is the claim somebody later builds a span on.
  if (!LoadDefinitions(tables, stubs, life.definitions, error)) {
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
