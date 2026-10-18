// The checks of the hunger lamp's yellow stage (hunger_forecast_checks.h).
//
// THE KNOWN ANSWERS, by hand: an adult needs 300 / 48 = 6.25 kg of grain
// equivalent a day at 3 300 kcal a kg, 20 625 kcal; two adults 41 250 kcal a
// day. With nothing in the stores there is no issue to come and no harvest,
// so the horizon is the cap, 24 days, and the threshold at c = 0.9 is
// 41 250 x 24 x 0.9 = 891 000 kcal. 120 kg of grain in the pantry are
// 396 000 kcal, spent at the day's shortfall of 41 250 x 0.9 = 37 125 kcal:
// the first short day is day 10 (10.67, floored).
#include "hunger_forecast_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "food_config.h"
#include "hunger_forecast.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: hunger forecast: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Grams kKilo = core::kGramsPerKilogram;
constexpr std::uint32_t kToday = 12;
constexpr float kSpeedup = 4.0F;
constexpr float kShare = 0.9F;

/// Grain alone, issued by the table's grams; nothing else on the roster.
core::FoodConfig GrainConfig() {
  core::FoodConfig config;
  config.resources.assign(1, core::FoodResourceDef{});
  config.resources[0] = {.kcal_per_gram = 3.3F,
                         .category = core::FoodCategory::kBread,
                         .issue_kg_per_trudoden = 1.0F,
                         .ration_kg_per_day = 2.0F};
  config.distribution.period_days = 1;
  config.distribution.reserve_seed_fund = 0;
  config.plot.fish_kg_per_yard_year = {0.0F, 0.0F, 0.0F};
  return config;
}

/// One household of two adults of 30, its pantry holding `pantry_grain_kg`
/// of grain; no store, so nothing is issued.
core::WorldState OneFamily(float pantry_grain_kg) {
  core::WorldState world;
  world.calendar.tick = static_cast<core::Tick>(kToday) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  core::FamilyRow yard;
  yard.pantry.assign(1, static_cast<core::Grams>(pantry_grain_kg * static_cast<float>(kKilo)));
  const core::FamilyId id = core::AppendRow(world.families, yard);
  for (int member = 0; member < 2; ++member) {
    core::ResidentRow adult;
    adult.family = id;
    adult.birth_day = static_cast<std::int32_t>(kToday) - static_cast<std::int32_t>(30.0F * 12.0F);
    core::AppendRow(world.residents, adult);
  }
  return world;
}

/// THE SHARE (econ §6, (a1)): two adults of 30 with an empty pantry, and a
/// child of 10 holding `child_pantry_kg` of grain and every trudoden of the
/// last 12 days; a store of 1 000 kg, last year's book 20 trudodni a day. The
/// issue to come is 20 x 24 = 480 trudodni x 1 kg = 480 kg, 1 584 000 kcal -
/// all of it the adults' by their workers, none of it by the ring.
core::WorldState WorkersAndRing(float child_pantry_kg) {
  core::WorldState world = OneFamily(0.0F);
  core::UnitRow store;
  store.level = 1;
  store.stock.assign(1, 1000 * kKilo);
  core::AppendRow(world.units, store);
  world.ledger.closed.trudodni_by_day.fill(20 * core::kTrudodniScale);
  core::FamilyRow child_yard;
  child_yard.pantry.assign(1,
                           static_cast<core::Grams>(child_pantry_kg * static_cast<float>(kKilo)));
  child_yard.trudodni_by_day.fill(2 * core::kTrudodniScale);
  const core::FamilyId child_id = core::AppendRow(world.families, child_yard);
  core::ResidentRow child;
  child.family = child_id;
  child.birth_day = static_cast<std::int32_t>(kToday) - static_cast<std::int32_t>(10.0F * 12.0F);
  core::AppendRow(world.residents, child);
  return world;
}

/// THE INFLOWS (#1046 (b), econ §7.2 and §8): grain under the default rule,
/// its harvest in `harvest_in` days with 10 t standing, and milk at the
/// table's 30 kg a trudoden that the stores hold none of - the milk's kcal
/// keeps the grain under a third of the bundle, so nothing cuts the horizon
/// and it stays 24 days, as on the canon (seq 11).
core::FoodConfig InflowConfig(std::int32_t harvest_in, std::uint8_t terms) {
  core::FoodConfig config = GrainConfig();
  config.resources.push_back({.kcal_per_gram = 0.64F,
                              .category = core::FoodCategory::kDairy,
                              .issue_kg_per_trudoden = 30.0F});
  config.carted_daily = core::ResourceId{1};
  config.days_to_harvest_of = [harvest_in](const core::WorldState&, core::ResourceId resource) {
    return resource.value == 0 ? harvest_in : -1;
  };
  config.standing_harvest = [](const core::WorldState&) {
    return core::ResourceAmounts{10'000 * kKilo, 0};
  };
  config.harvest_to_come = config.standing_harvest;
  config.milk_to_issue_ahead = [](const core::WorldState&, std::uint32_t days) {
    return static_cast<core::Grams>(days) * 100 * kKilo;
  };
  config.forecast_terms = terms;
  return config;
}

/// Two adults of 30 with `pantry_kg` of grain; a store of `store_kg` of grain
/// and no milk; last year's book 20 trudodni a day.
core::WorldState InflowWorld(float pantry_kg, float store_kg) {
  core::WorldState world = OneFamily(pantry_kg);
  world.families.rows[0].pantry.resize(2, 0);
  core::UnitRow store;
  store.level = 1;
  store.stock.assign(2, 0);
  store.stock[0] = static_cast<core::Grams>(store_kg * static_cast<float>(kKilo));
  core::AppendRow(world.units, store);
  world.ledger.closed.trudodni_by_day.fill(20 * core::kTrudodniScale);
  return world;
}

}  // namespace

int CheckTheHungerForecast() {
  int failures = 0;
  const core::FoodConfig config = GrainConfig();
  const core::HungerForecast fed =
      core::ForecastHunger(config, OneFamily(300.0F), kSpeedup, kShare);
  failures += Expect(fed.under_threat.empty() && fed.families_with_members == 1,
                     "300 kg of grain in the pantry carry two adults past the 24-day horizon");
  const core::HungerForecast empty =
      core::ForecastHunger(config, OneFamily(0.0F), kSpeedup, kShare);
  failures += Expect(empty.under_threat.size() == 1 && empty.days_to_first_short == 0,
                     "an empty pantry and no issue: under threat, short today");
  const core::HungerForecast lean =
      core::ForecastHunger(config, OneFamily(120.0F), kSpeedup, kShare);
  failures += Expect(lean.under_threat.size() == 1 && lean.days_to_first_short == 10,
                     "120 kg: 396 000 kcal at 37 125 a day short - the first short day is day 10");
  core::WorldState own = OneFamily(0.0F);
  own.families.rows[0].own_kcal_mean = 40'000.0F;  // 960 000 over 24 days > 891 000
  failures += Expect(core::ForecastHunger(config, own, kSpeedup, kShare).under_threat.empty(),
                     "its own food at 40 000 kcal a day covers the threshold: not under threat");
  const core::HungerForecast strict =
      core::ForecastHunger(config, OneFamily(270.0F), kSpeedup, 1.0F);
  const core::HungerForecast loose =
      core::ForecastHunger(config, OneFamily(270.0F), kSpeedup, 0.8F);
  failures += Expect(strict.under_threat.size() == 1 && loose.under_threat.empty(),
                     "270 kg (891 000 kcal): under threat at c = 1.0 (990 000), not at c = 0.8 "
                     "(792 000) - the share moves the line the known way");
  const core::HungerForecast by_workers =
      core::ForecastHunger(config, WorkersAndRing(300.0F), kSpeedup, kShare);
  failures += Expect(by_workers.under_threat.empty(),
                     "the adults' empty pantry is covered by 1 584 000 kcal of issue at their "
                     "workers' share 2 of 2, though the ring gives them 0 trudodni of 24");
  const core::WorldState no_worker_world = WorkersAndRing(0.0F);
  const core::HungerForecast no_worker =
      core::ForecastHunger(config, no_worker_world, kSpeedup, kShare);
  failures +=
      Expect(no_worker.under_threat.size() == 1 &&
                 no_worker.under_threat[0].value == no_worker_world.families.row_ids[1].value,
             "a child alone, an empty pantry and every trudoden of the ring: no worker, "
             "no issue to come - under threat beside a full store");
  // THE HARVEST INSIDE THE HORIZON (arm B): 100 kg in the pantry and 10 kg in
  // the store carry two adults past a harvest on day 2, and from it the
  // grain's norm shares 10 t over a year's trudodni: 10 t x 240 / 960 = 2.5 t
  // to the horizon. Without the term (0) they are under threat.
  failures += Expect(
      core::ForecastHunger(InflowConfig(2, 1), InflowWorld(100.0F, 10.0F), kSpeedup, kShare)
              .under_threat.empty() &&
          core::ForecastHunger(InflowConfig(2, 0), InflowWorld(100.0F, 10.0F), kSpeedup, kShare)
                  .under_threat.size() == 1,
      "a harvest on day 2 shares 2.5 t to the horizon from its day: not under "
      "threat, and under threat with the term switched off");
  // U1 (econ §8): 60 kg (198 000 kcal) and 10 kg issued (33 000) do not reach
  // a harvest on day 12 (445 500 needed): under threat, though the harvest
  // would cover the horizon, and short on day 5 (198 000 / (37 125 - 2 750)).
  const core::HungerForecast before_harvest =
      core::ForecastHunger(InflowConfig(12, 1), InflowWorld(60.0F, 10.0F), kSpeedup, kShare);
  failures +=
      Expect(before_harvest.under_threat.size() == 1 && before_harvest.days_to_first_short == 5,
             "U1: short on day 5 with the harvest on day 12 - under threat, short day 5");
  // THE MILK AHEAD (arm BM): 23 mornings x 100 kg = 2.3 t of milk, 1 472 000
  // kcal, cover an empty pantry with no harvest anywhere; arm B does not see it.
  failures += Expect(
      core::ForecastHunger(InflowConfig(-1, 2), InflowWorld(0.0F, 0.0F), kSpeedup, kShare)
              .under_threat.empty() &&
          core::ForecastHunger(InflowConfig(-1, 1), InflowWorld(0.0F, 0.0F), kSpeedup, kShare)
                  .under_threat.size() == 1,
      "the milkings ahead cover an empty pantry under arm BM, not under arm B");
  return failures;
}
