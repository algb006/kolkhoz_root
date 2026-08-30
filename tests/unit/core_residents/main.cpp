// Unit test of core_residents. Four parts, as the module has:
//   * stage-3 demography on a hand-built world — births only in fertile
//     marriages, deaths by age, marriages of singles, migration arithmetic,
//     link integrity — plus the family-satisfaction metrics phase with the
//     epoch weights and the low-component law;
//   * stage-6 exchange (task O1) — the monthly basket and its worst-position
//     rule, the seed fund guard, the minimum ration, the nets, and the burn
//     of both trudodni counters at the economic year's close;
//   * stage-6 vitals bookkeeping — the yearly fold of the settlement's mean
//     satiety into the three-year window decision 105 reads;
//   * stage-6 meal and plot (task O2) — the norm on the table, the
//     proportional burn of the bins, the season's variety mask and its
//     ceiling, the plot hours with their factors and the garden they pay.

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
#include "family_meal.h"
#include "food_config.h"
#include "household_plot.h"
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

/// Four resources: 0 grain, 1 potato, 2 fish, 3 vegetables. Norms are round
/// numbers so that every expectation below is exact integer arithmetic.
core::FoodConfig MakeExchangeConfig() {
  core::FoodConfig config;
  config.resources.assign(4, core::FoodResourceDef{});
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
  config.resources[3] = {.kcal_per_gram = 0.25F,
                         .category = core::FoodCategory::kVegetables,
                         .issue_kg_per_trudoden = 0.0F,
                         .ration_kg_per_day = 0.0F};
  config.seed_norms.assign(
      1, core::SeedNormDef{.resource = core::ResourceId{0}, .sowing_norm_kg_per_ha = 180.0F});
  config.fish_resource = core::ResourceId{2};
  config.potato_resource = core::ResourceId{1};
  config.vegetables_resource = core::ResourceId{3};
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
  store.stock.assign(4, 0);
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

  // Half the grain in the store: every position issues what it can, and the
  // debt is redeemed by the share of the bundle's VALUE actually handed over
  // (labor-payment §3, canon of 2026-08-30). Wanted: 2 kg of grain at 3.3
  // kcal/g and 4 kg of potatoes at 0.77 — 6600 + 3080 = 9680 kcal. The store
  // has one kilogram of grain, so 3300 + 3080 = 6380 lands: 65.9%.
  {
    core::WorldState world = MakeExchangeWorld(1.0F, 100.0F, 200, 70.0F);
    core::RunFamilyExchange(config, 4.0F, world);
    failures += Expect(PantryOf(world, 0) == 1 * kKilo, "the short position issues what there is");
    failures += Expect(PantryOf(world, 1) == 4 * kKilo,
                       "and a position the store CAN cover is issued whole — an empty bin no "
                       "longer stops the rest of the basket");
    const core::TrudodniHundredths redeemed = world.families.rows[0].trudodni_redeemed;
    failures += Expect(redeemed >= 129 && redeemed <= 133,
                       "and the debt is redeemed by the share of the bundle's value received");
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
    failures += Expect(world.families.rows[0].trudodni_redeemed < 100,
                       "and the debt is redeemed only for the value that WAS issued");

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
  const core::LifeConfig life;
  core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 80.0F);
  core::ResidentRow second;
  second.family = world.families.row_ids[0];
  second.birth_day = -360;
  second.satiety = 60.0F;  // settlement mean is a round 70
  AppendRow(world.residents, second);
  for (std::uint32_t day = 0; day <= core::kDaysPerYear; ++day) {
    world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    core::AccumulateVitals(life, world);
  }
  failures += Expect(world.vitals.satiety_year_means.back() == 70.0F,
                     "the finished year's mean satiety enters the window last");
  failures += Expect(world.vitals.satiety_running_days == 1,
                     "and the new year starts counting from its first day");
  // Life expectancy over that window: the neutral point is 70 and the year
  // came in at 70, but the two years before it were the neutral 70 as well,
  // so nutrition contributes nothing and the base stands.
  failures += Expect(world.vitals.life_expectancy_years == life.vitals.base_years,
                     "a settlement fed at the neutral point lives its base span");

  // A settlement that ate badly for three years loses the band's worth.
  {
    core::WorldState hungry = MakeExchangeWorld(0.0F, 0.0F, 0, 20.0F);
    for (std::uint32_t day = 0; day <= 3U * core::kDaysPerYear; ++day) {
      hungry.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
      core::RefreshCalendarCaches(hungry.calendar);
      core::AccumulateVitals(life, hungry);
    }
    failures += Expect(hungry.vitals.life_expectancy_years ==
                           life.vitals.base_years + life.vitals.nutrition_years_min,
                       "three starving years cost the whole nutrition band");
  }
  return failures;
}

// --- stage 6, task O2: the meal, the satiety component and the plot -------

/// Sets the world to the given day and hour without running any phase.
void SetClock(core::WorldState& world, core::SimDay day, std::uint32_t hour) {
  world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay + hour;
  core::RefreshCalendarCaches(world.calendar);
}

/// Puts kilograms of a resource straight into the yard's pantry.
void FillPantry(core::WorldState& world, std::uint32_t resource, float kilograms) {
  core::ResourceAmounts& pantry = world.families.rows[0].pantry;
  if (pantry.size() <= resource) {
    pantry.resize(resource + 1U, 0);
  }
  pantry[resource] = static_cast<core::Grams>(kilograms) * core::kGramsPerKilogram;
}

int CheckMeal() {
  int failures = 0;
  const core::FoodConfig config = MakeExchangeConfig();
  // One adult of 30 biological years owes the whole adult norm: 300 kg
  // grain-eq a game year is 6.25 kg a day, and at 3.3 kcal/g that is
  // 20 625 kcal on the table.
  constexpr float kNeedKcal = 20625.0F;

  // A full pantry covers the norm exactly once and no more: what is eaten
  // is the need, not the stock.
  {
    core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 70.0F);
    FillPantry(world, 0, 10.0F);  // 33 000 kcal of grain
    SetClock(world, 4, core::kTicksPerDay - 1U);
    core::RunFamilyMeal(config, 4.0F, world, world, 0);
    const core::Grams left = world.families.rows[0].pantry[0];
    const float eaten_kcal =
        static_cast<float>(10 * core::kGramsPerKilogram - left) * config.resources[0].kcal_per_gram;
    failures += Expect(eaten_kcal > kNeedKcal * 0.99F && eaten_kcal < kNeedKcal * 1.01F,
                       "the family eats its norm and leaves the rest in the bin");
    failures += Expect(world.residents.rows[0].satiety == 74.0F,
                       "a fed day drifts satiety toward 100 by one day's drift");
    failures += Expect(world.families.rows[0].food_variety_mask == 0b1,
                       "the variety mask records the one category that was on the table");
    failures += Expect(world.residents.rows[0].health > 70.0F,
                       "and a satiety above the recovery threshold mends health");
  }

  // Two bins, one meal: the burn is proportional to what is stored, so
  // neither bin is emptied while the other stands full.
  {
    core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 70.0F);
    FillPantry(world, 0, 10.0F);
    FillPantry(world, 1, 10.0F);
    SetClock(world, 4, core::kTicksPerDay - 1U);
    core::RunFamilyMeal(config, 4.0F, world, world, 0);
    failures += Expect(world.families.rows[0].pantry[0] == world.families.rows[0].pantry[1],
                       "equal stocks are eaten in equal measure");
    failures +=
        Expect(world.families.rows[0].food_variety_mask == 0b11, "both categories reach the mask");
  }

  // An empty pantry: satiety falls by the day's drift and, once under the
  // threshold, health follows it down.
  {
    core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 30.0F);
    SetClock(world, 4, core::kTicksPerDay - 1U);
    core::RunFamilyMeal(config, 4.0F, world, world, 0);
    failures += Expect(world.residents.rows[0].satiety == 26.0F,
                       "an empty pantry drifts satiety toward zero");
    failures += Expect(world.residents.rows[0].health < 70.0F,
                       "and hunger under the threshold costs health, a point a week");
  }

  // The mask is a SEASON's table, not a day's: it clears when the season
  // turns and fills again from that day's meal.
  {
    core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 70.0F);
    world.families.rows[0].food_variety_mask = 0b1111;
    FillPantry(world, 0, 10.0F);
    SetClock(world, 8, core::kTicksPerDay - 1U);  // 1 March: spring begins
    core::RunFamilyMeal(config, 4.0F, world, world, 0);
    failures += Expect(world.families.rows[0].food_variety_mask == 0b1,
                       "the season's first meal starts the mask over");
  }
  return failures;
}

int CheckSatietyComponent() {
  int failures = 0;
  const core::FoodConfig config = MakeExchangeConfig();
  core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 80.0F);
  world.families.rows[0].food_variety_mask = 0b1;  // bread alone
  failures += Expect(core::SatietyComponent(config, world, 0) == 50.0F,
                     "one category in Epoch I caps the component at 50, as the design says");
  world.families.rows[0].food_variety_mask = 0b111;
  failures += Expect(core::SatietyComponent(config, world, 0) == 80.0F,
                     "the epoch's three categories lift the ceiling off the mean");
  return failures;
}

int CheckPlot() {
  int failures = 0;
  const core::FoodConfig config = MakeExchangeConfig();
  constexpr std::uint32_t kPlotHour = core::kTicksPerDay - 2U;

  // A yard with nobody at kolkhoz work: the no-worker base, less what the
  // absence of an elder costs it.
  {
    core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 70.0F);
    SetClock(world, 4, kPlotHour);
    core::RunHouseholdPlot(config, 4.0F, world, 0);
    failures += Expect(world.families.rows[0].household_hours == 5.0F - 1.3F,
                       "an elderless yard with nobody out works its base less the elders' worth");
  }

  // A yard whose worker spent ten hours away: the exact remainder of the
  // day, sleep and the road already taken out of it.
  {
    core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 70.0F);
    world.residents.rows[0].work.hours_away_today = 10.0F;
    SetClock(world, 4, kPlotHour);
    core::RunHouseholdPlot(config, 4.0F, world, 0);
    failures += Expect(world.families.rows[0].household_hours == 24.0F - 8.0F - 10.0F - 1.3F,
                       "a day out leaves the yard what the day actually left it");
  }

  // A whole growing season at full attention pays the yard's whole yield.
  {
    core::WorldState world = MakeExchangeWorld(0.0F, 0.0F, 0, 70.0F);
    core::ResidentRow granny;
    granny.family = world.families.row_ids[0];
    granny.birth_day = -780;  // 65 biological years: an elder in the yard
    AppendRow(world.residents, granny);
    // 1 April to 30 September, the growing season of the model.
    for (core::SimDay day = 12; day <= 35; ++day) {
      SetClock(world, day, kPlotHour);
      core::RunHouseholdPlot(config, 4.0F, world, 0);
    }
    failures += Expect(world.families.rows[0].household_hours > config.plot.full_yield_hours,
                       "an elder at home keeps the yard above the full-yield hours");
    failures += Expect(
        world.families.rows[0].pantry[1] == static_cast<core::Grams>(900) * core::kGramsPerKilogram,
        "September pays the yard's whole potato crop for a season of full days");
    failures += Expect(world.families.rows[0].pantry[3] ==
                           static_cast<core::Grams>(1200) * core::kGramsPerKilogram,
                       "and its whole vegetable crop");
    failures += Expect(world.families.rows[0].plot_ratio_days == 0,
                       "the season's accumulators reset after the harvest");
  }
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
    current.families.rows[0].component_common_cause = 60.0F;
    current.families.rows[0].component_needs = 60.0F;
    current.families.rows[1].component_common_cause = 90.0F;
    current.families.rows[1].component_needs = 90.0F;
    // Two of the four components are no longer free-standing: rest is the
    // members' own rest averaged (stage 5) and satiety is their satiety
    // under the variety ceiling (stage 6). Set the people, not the
    // components — and give both yards a varied enough table that the
    // ceiling of Epoch I (three categories) does not bite.
    constexpr std::uint16_t kThreeCategories = 0b111;
    current.families.rows[0].food_variety_mask = kThreeCategories;
    current.families.rows[1].food_variety_mask = kThreeCategories;
    for (core::ResidentRow& resident : current.residents.rows) {
      if (resident.family.value == current.families.row_ids[0].value) {
        resident.rest = 60.0F;
        resident.satiety = 60.0F;
      } else if (resident.family.value == current.families.row_ids[1].value) {
        resident.rest = 90.0F;
        resident.satiety = 10.0F;  // starving
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
  failures += CheckMeal();
  failures += CheckSatietyComponent();
  failures += CheckPlot();
  failures += CheckExchange();
  failures += CheckVitals();

  if (failures == 0) {
    std::cout << "unit_core_residents: all checks passed\n";
  }
  return failures;
}
