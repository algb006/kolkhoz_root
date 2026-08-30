// Unit test of core_residents. Three parts, as the module has:
//   * stage-3 demography on a hand-built world — births only in fertile
//     marriages, deaths by age, marriages of singles, migration arithmetic,
//     link integrity — plus the family-satisfaction metrics phase with the
//     epoch weights and the low-component law;
//   * stage-6 exchange (task O1) — the monthly basket and its worst-position
//     rule, the seed fund guard, the minimum ration, the nets, and the burn
//     of both trudodni counters at the economic year's close;
//   * stage-6 vitals bookkeeping — the yearly fold of the settlement's mean
//     satiety into the three-year window decision 105 reads.

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_residents/residents_system.h"
#include "core_tables/tables.h"
#include "family_exchange.h"
#include "food_config.h"
#include "vitals.h"

static_assert(std::is_abstract_v<core::IResidentsSystem>, "IResidentsSystem is a contract");
static_assert(std::has_virtual_destructor_v<core::IResidentsSystem>,
              "implementations are destroyed through the interface");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

class EmptyTableSet final : public core::ITableSet {
 public:
  const core::ITable* FindTable(std::string_view /*name*/) const override { return nullptr; }

  std::uint32_t TableCount() const override { return 0; }

  std::string_view TableName(std::uint32_t /*index*/) const override { return {}; }
};

/// Appends an adult of the given biological age (life speedup 4).
core::ResidentId AddAdult(core::WorldState& world,
                          core::FamilyId family,
                          core::Sex sex,
                          float age_years) {
  core::ResidentRow person;
  person.family = family;
  person.sex = sex;
  person.birth_day = -static_cast<std::int32_t>(age_years * 12.0F);  // 12 days = 1 bio year
  return AppendRow(world.residents, person);
}

void Marry(core::WorldState& world, core::ResidentId wife, core::ResidentId husband) {
  world.residents.rows[FindRow(world.residents, wife)].spouse = husband;
  world.residents.rows[FindRow(world.residents, husband)].spouse = wife;
}

/// Runs the demography sub-step over `days` days the way the decisions slot
/// does: one call per day boundary.
void RunDays(core::IResidentsSystem& system, core::WorldState& world, std::uint32_t days) {
  for (std::uint32_t day = 0; day < days; ++day) {
    core::WorldState previous = world;
    world.calendar.tick += core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    system.RunDemographyDecisions(previous, world);
  }
}

// --- stage 6: the family/kolkhoz exchange and the vitals window ------------

/// Three resources: 0 grain, 1 potato, 2 fish. Norms are round numbers so
/// that every expectation below is exact integer arithmetic in grams.
core::FoodConfig MakeExchangeConfig() {
  core::FoodConfig config;
  config.resources.assign(3, core::FoodResourceDef{});
  config.resources[0] = {.kcal_per_gram = 3.3F,
                         .category = core::FoodCategory::kBread,
                         .issue_kg_per_trudoden = 1.0F,
                         .ration_kg_per_day = 2.0F};
  config.resources[1] = {.kcal_per_gram = 0.77F,
                         .category = core::FoodCategory::kPotato,
                         .issue_kg_per_trudoden = 2.0F,
                         .ration_kg_per_day = 0.0F};
  config.resources[2] = {.kcal_per_gram = 0.9F,
                         .category = core::FoodCategory::kFish,
                         .issue_kg_per_trudoden = 0.0F,
                         .ration_kg_per_day = 0.0F};
  config.seed_norms.assign(
      1, core::SeedNormDef{.resource = core::ResourceId{0}, .sowing_norm_kg_per_ha = 180.0F});
  config.fish_resource = core::ResourceId{2};
  config.plot.fish_kg_per_yard_year = {480.0F, 0.0F, 0.0F};  // 10 kg a game day
  return config;
}

/// One store holding `grain_kg` and `potato_kg`, one yard owed `trudodni`
/// hundredths, one adult in it with the given satiety.
core::WorldState MakeExchangeWorld(float grain_kg,
                                   float potato_kg,
                                   core::TrudodniHundredths trudodni,
                                   core::Metric satiety) {
  core::WorldState world;
  core::RefreshCalendarCaches(world.calendar);
  core::UnitRow store;
  store.stock.assign(3, 0);
  store.stock[0] = static_cast<core::Grams>(grain_kg) * core::kGramsPerKilogram;
  store.stock[1] = static_cast<core::Grams>(potato_kg) * core::kGramsPerKilogram;
  AppendRow(world.units, store);
  core::FamilyRow yard;
  yard.trudodni_account = trudodni;
  const core::FamilyId id = AppendRow(world.families, yard);
  core::ResidentRow adult;
  adult.family = id;
  adult.birth_day = -360;  // 30 biological years at speedup 4
  adult.satiety = satiety;
  AppendRow(world.residents, adult);
  return world;
}

core::Grams PantryOf(const core::WorldState& world, std::uint32_t resource) {
  const core::ResourceAmounts& pantry = world.families.rows[0].pantry;
  return pantry.size() > resource ? pantry[resource] : 0;
}

int CheckExchange() {
  int failures = 0;
  const core::FoodConfig config = MakeExchangeConfig();
  constexpr core::Grams kKilo = core::kGramsPerKilogram;

  // Full stores: two trudodni buy the whole basket and clear the debt.
  {
    core::WorldState world = MakeExchangeWorld(100.0F, 100.0F, 200, 70.0F);
    core::RunFamilyExchange(config, 4.0F, world);
    failures +=
        Expect(PantryOf(world, 0) == 2 * kKilo, "the basket issues 1 kg of grain a trudoden");
    failures += Expect(PantryOf(world, 1) == 4 * kKilo, "and 2 kg of potatoes a trudoden");
    failures += Expect(world.families.rows[0].trudodni_redeemed == 200,
                       "a fully covered basket redeems the whole debt");
    failures += Expect(world.units.rows[0].stock[0] == 98 * kKilo,
                       "what the pantry gained the store lost, to the gram");
    // The nets run every day, past the trudodni entirely.
    failures += Expect(PantryOf(world, 2) == 10 * kKilo, "the nets bring the epoch's daily share");
  }

  // Half the grain in the store: the basket advances by its worst position.
  {
    core::WorldState world = MakeExchangeWorld(1.0F, 100.0F, 200, 70.0F);
    core::RunFamilyExchange(config, 4.0F, world);
    failures += Expect(PantryOf(world, 0) == 1 * kKilo, "the short position issues what there is");
    failures += Expect(PantryOf(world, 1) == 2 * kKilo,
                       "and every other position issues the same share, not its own");
    failures += Expect(world.families.rows[0].trudodni_redeemed == 100,
                       "half a basket redeems half the debt; the rest waits for next month");
  }

  // The seed fund is off-limits to the automatic issue.
  {
    core::WorldState world = MakeExchangeWorld(100.0F, 100.0F, 200, 70.0F);
    core::FieldRow field;
    field.area_ga = 1.0F;
    field.phase = core::FieldPhase::kIdle;
    field.rotation_year0 = core::CropId{0};  // 180 kg/ha of resource 0
    AppendRow(world.fields, field);
    core::RunFamilyExchange(config, 4.0F, world);
    failures +=
        Expect(PantryOf(world, 0) == 0, "next sowing's seed is not handed out for trudodni");
    failures += Expect(world.families.rows[0].trudodni_redeemed == 0,
                       "and the debt is not redeemed against food that was never issued");

    core::FoodConfig unguarded = config;
    unguarded.distribution.reserve_seed_fund = 0;
    core::WorldState open_world = MakeExchangeWorld(100.0F, 100.0F, 200, 70.0F);
    AppendRow(open_world.fields, field);
    core::RunFamilyExchange(unguarded, 4.0F, open_world);
    failures += Expect(PantryOf(open_world, 0) == 2 * kKilo,
                       "with the guard off the same stores do cover the basket");
  }

  // The hungry family gets the ration past its (empty) trudodni account.
  {
    core::WorldState world = MakeExchangeWorld(100.0F, 100.0F, 0, 10.0F);
    core::RunFamilyExchange(config, 4.0F, world);
    failures += Expect(PantryOf(world, 0) == 8 * kKilo,
                       "the ration is 2 kg an eater a day over the 4-day period");
    failures +=
        Expect(world.families.rows[0].trudodni_redeemed == 0, "the ration costs no trudodni");
    core::WorldState fed = MakeExchangeWorld(100.0F, 100.0F, 0, 70.0F);
    core::RunFamilyExchange(config, 4.0F, fed);
    failures += Expect(PantryOf(fed, 0) == 0, "a fed family triggers no ration");
  }

  // The economic year's close burns both counters — after the last issue.
  {
    core::WorldState world = MakeExchangeWorld(100.0F, 100.0F, 200, 70.0F);
    world.calendar.tick = static_cast<core::Tick>(core::kDaysPerYear) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    core::RunFamilyExchange(config, 4.0F, world);
    failures += Expect(PantryOf(world, 0) == 2 * kKilo,
                       "the year's last distribution runs before the burn");
    failures += Expect(world.families.rows[0].trudodni_account == 0 &&
                           world.families.rows[0].trudodni_redeemed == 0,
                       "both counters burn at the turn of the economic year");
  }
  return failures;
}

int CheckVitals() {
  int failures = 0;
  core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 80.0F);
  core::ResidentRow second;
  second.family = world.families.row_ids[0];
  second.birth_day = -360;
  second.satiety = 60.0F;  // settlement mean is a round 70
  AppendRow(world.residents, second);
  for (std::uint32_t day = 0; day <= core::kDaysPerYear; ++day) {
    world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    core::AccumulateVitals(world);
  }
  failures += Expect(world.vitals.satiety_year_means.back() == 70.0F,
                     "the finished year's mean satiety enters the window last");
  failures += Expect(world.vitals.satiety_running_days == 1,
                     "and the new year starts counting from its first day");
  return failures;
}

int CheckFoodConfigDefaults(const core::ITableSet& tables) {
  int failures = 0;
  std::string error;
  const core::FoodConfig config = core::ParseFoodConfig(tables, &error);
  failures += Expect(error.empty(), "a table-less world parses food without complaint");
  failures += Expect(config.consumption.adult_kg_grain_eq_per_year == 300.0F,
                     "and keeps the canonical adult norm");
  failures += Expect(config.resources.empty() && config.seed_norms.empty(),
                     "with no roster to size them, the dense vectors stay empty");
  // An empty roster must make the exchange a no-op rather than an error.
  core::WorldState world = MakeExchangeWorld(100.0F, 100.0F, 200, 70.0F);
  core::RunFamilyExchange(config, 4.0F, world);
  failures += Expect(world.families.rows[0].trudodni_redeemed == 0,
                     "and the exchange idles instead of handing out unknown food");
  return failures;
}

}  // namespace

int main() {
  int failures = 0;
  const EmptyTableSet tables;  // canonical defaults compiled into the config
  const auto system = core::CreateResidentsSystem(tables);
  failures += Expect(system != nullptr, "factory yields a system");

  // A hand-built village: three fertile couples, one old man, one single.
  core::WorldState world;
  world.world_seed = 77;
  world.rng = core::SeedRngState(77, 0);
  for (int couple = 0; couple < 3; ++couple) {
    const core::FamilyId yard = AppendRow(world.families, core::FamilyRow{});
    const core::ResidentId wife =
        AddAdult(world, yard, core::Sex::kFemale, 22.0F + static_cast<float>(couple) * 4.0F);
    const core::ResidentId husband =
        AddAdult(world, yard, core::Sex::kMale, 24.0F + static_cast<float>(couple) * 4.0F);
    Marry(world, wife, husband);
  }
  const core::FamilyId old_yard = AppendRow(world.families, core::FamilyRow{});
  AddAdult(world, old_yard, core::Sex::kMale, 74.0F);
  const core::FamilyId single_yard = AppendRow(world.families, core::FamilyRow{});
  AddAdult(world, single_yard, core::Sex::kFemale, 20.0F);

  const std::uint32_t start_population = static_cast<std::uint32_t>(world.residents.rows.size());
  const std::uint32_t start_ids = world.residents.next_id_value;

  // Two game years of village life.
  RunDays(*system, world, 2 * core::kDaysPerYear);

  // Births happened, and every child has married parents and a live family.
  std::uint32_t births = 0;
  bool children_have_parents = true;
  bool families_hold = true;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = world.residents.rows[row];
    families_hold = families_hold && FindRow(world.families, resident.family) != core::kNoRow;
    if (resident.birth_day >= 0) {
      ++births;
      children_have_parents = children_have_parents &&
                              resident.mother.value != core::kInvalidEntityIdValue &&
                              resident.father.value != core::kInvalidEntityIdValue;
    }
  }
  failures += Expect(births > 0, "fertile couples give births within two years");
  failures += Expect(children_have_parents, "every newborn has both parents recorded");
  failures += Expect(families_hold, "every resident's family exists");
  failures += Expect(world.residents.next_id_value >= start_ids + births,
                     "ids grow monotonically, never reused");

  // The 74-year-old (20%/year at 4x life speed) is gone within two game
  // years — eight biological years beyond the old-age band.
  bool old_man_alive = false;
  for (const core::ResidentRow& resident : world.residents.rows) {
    if (resident.birth_day <= -880) {
      old_man_alive = true;
    }
  }
  failures += Expect(!old_man_alive, "the old man died of age within two years");

  // The single woman married (a migrant or widower): spouse symmetry holds
  // for every married resident.
  bool spouses_symmetric = true;
  std::uint32_t married = 0;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const core::ResidentRow& resident = world.residents.rows[row];
    if (resident.spouse.value == core::kInvalidEntityIdValue) {
      continue;
    }
    ++married;
    const std::uint32_t spouse_row = FindRow(world.residents, resident.spouse);
    spouses_symmetric =
        spouses_symmetric && spouse_row != core::kNoRow &&
        world.residents.rows[spouse_row].spouse.value == world.residents.row_ids[row].value;
  }
  failures += Expect(spouses_symmetric, "spouse links are symmetric");
  failures += Expect(married >= 6, "the starting couples are still married");

  // Migration: the canonical 8 a year arrived over two years (minus anyone
  // who died — so at least most of them).
  failures += Expect(world.residents.rows.size() + 5 >= start_population + births + 10,
                     "migrants arrived at about 8 a year");

  // The metrics phase: weighted satisfaction and the low-component law.
  {
    core::WorldState previous = world;
    core::WorldState current = world;
    current.families.rows[0].component_satiety = 60.0F;
    current.families.rows[0].component_common_cause = 60.0F;
    current.families.rows[0].component_needs = 60.0F;
    current.families.rows[0].component_rest = 60.0F;
    current.families.rows[1].component_satiety = 10.0F;  // starving
    current.families.rows[1].component_common_cause = 90.0F;
    current.families.rows[1].component_needs = 90.0F;
    current.families.rows[1].component_rest = 90.0F;
    // The rest component is no longer free-standing: since labor exists it
    // is the members' own rest, averaged, and the metrics phase recomputes
    // it (stage 5). Set the people, not the component.
    for (core::ResidentRow& resident : current.residents.rows) {
      if (resident.family.value == current.families.row_ids[0].value) {
        resident.rest = 60.0F;
      } else if (resident.family.value == current.families.row_ids[1].value) {
        resident.rest = 90.0F;
      }
    }
    core::IParallelPhase& metrics = system->MetricsPhase();
    const std::uint32_t count = metrics.ParallelItemCount(current);
    failures += Expect(count == static_cast<std::uint32_t>(current.families.rows.size()),
                       "the metrics phase iterates every family of the current state");
    metrics.RunItemRange(previous, current, 0, count);
    failures += Expect(current.families.rows[0].satisfaction == 60.0F,
                       "equal components give their value (Epoch I weights sum to 100)");
    failures += Expect(current.families.rows[1].satisfaction <= 20.0F,
                       "a component below 20 caps satisfaction at twice itself");
  }

  failures += CheckFoodConfigDefaults(tables);
  failures += CheckExchange();
  failures += CheckVitals();

  if (failures == 0) {
    std::cout << "unit_core_residents: all checks passed\n";
  }
  return failures;
}
