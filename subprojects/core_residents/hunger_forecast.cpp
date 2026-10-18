// The hunger lamp's yellow stage (hunger_forecast.h; #1046).

#include "hunger_forecast.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/issue_norm_view.h"
#include "core_common/state_table_ops.h"
#include "family_exchange.h"
#include "family_meal.h"
#include "household_advance.h"
#include "issue_norm.h"

namespace core {

namespace {

/// A family's position in the forecast: its members' need and its stocks.
struct FamilyFood {
  FamilyId id;
  float need_kcal_day = 0.0F;
  float pantry_kcal = 0.0F;
  float members = 0.0F;
  float workers = 0.0F;
};

}  // namespace

HungerForecast ForecastHunger(const FoodConfig& config,
                              const WorldState& world,
                              float life_speedup,
                              float share_c) {
  HungerForecast forecast;
  if (config.resources.empty()) {
    return forecast;
  }
  const float grain_kcal_per_kg =
      static_cast<float>(kGramsPerKilogram) * config.consumption.grain_reference_kcal_per_gram;
  // THE MEMBERS AT HOME, by family: the meal's own norm.
  std::vector<FamilyFood> families(world.families.rows.size());
  for (std::size_t row = 0; row < families.size(); ++row) {
    families[row].id = world.families.row_ids[row];
  }
  for (const ResidentRow& resident : world.residents.rows) {
    if (AwayInDistrict(resident, world.calendar.tick)) {
      continue;
    }
    const std::uint32_t row = FindRow(world.families, resident.family);
    if (row == kNoRow) {
      continue;
    }
    const float age = BiologicalAgeYears(life_speedup, resident.birth_day, world.calendar.day);
    families[row].need_kcal_day +=
        DailyNeedKilograms(config.consumption, age, false) * grain_kcal_per_kg;
    families[row].members += 1.0F;
  }
  // THE SHARE IS THE FAMILY'S WORKERS AGAINST THE VILLAGE'S (econ §6, (a1);
  // the #1046 thread [6]), counted by the advance's own door (CountHousehold:
  // at home, of working age). Until 0.37.292 it was the family's trudodni over
  // the last 12 days (FamilyRow::trudodni_by_day), and a family between bursts of work read
  // low: on the canon's false threats its ring held 1.1 trudodni while it
  // earned 18 in the next 24 days, and its issue came to 6.8 times the
  // forecast (seq 3).
  float workers_all = 0.0F;
  for (std::size_t row = 0; row < families.size(); ++row) {
    const FamilyRow& family = world.families.rows[row];
    for (std::size_t r = 0; r < family.pantry.size() && r < config.resources.size(); ++r) {
      families[row].pantry_kcal +=
          static_cast<float>(family.pantry[r]) * config.resources[r].kcal_per_gram;
    }
    families[row].workers = static_cast<float>(
        CountHousehold(config, life_speedup, world, families[row].id, world.calendar.day).workers);
    workers_all += families[row].workers;
    forecast.families_with_members += families[row].members > 0.0F ? 1U : 0U;
  }
  // THE ISSUE TO COME, by position: the distribution's own norm, and never
  // more than the position's free stock - a table-grams norm stands on an
  // empty store too, and promises an issue nobody can give.
  const std::vector<Grams> reserve = SealedFunds(config, world);
  const std::vector<IssueNormLine> norms = ResolveIssueNorms(config, reserve, world, life_speedup);
  float issue_kcal_per_trudoden = 0.0F;
  for (const IssueNormLine& line : norms) {
    if (line.resource.value < config.resources.size()) {
      issue_kcal_per_trudoden += static_cast<float>(line.grams_per_trudoden) *
                                 config.resources[line.resource.value].kcal_per_gram;
    }
  }
  // THE HORIZON: the nearest harvest of a position giving more than a third
  // of the issue's kcal, at most the cap.
  std::int32_t horizon = kHungerForecastHorizonDays;
  for (const IssueNormLine& line : norms) {
    if (line.resource.value >= config.resources.size() || line.days_to_harvest <= 0) {
      continue;
    }
    const float kcal = static_cast<float>(line.grams_per_trudoden) *
                       config.resources[line.resource.value].kcal_per_gram;
    if (issue_kcal_per_trudoden > 0.0F && kcal * 3.0F > issue_kcal_per_trudoden) {
      horizon = std::min(horizon, line.days_to_harvest);
    }
  }
  // The village's trudodni to each position's harvest within the horizon.
  float issue_kcal_village = 0.0F;
  for (const IssueNormLine& line : norms) {
    if (line.resource.value >= config.resources.size() || line.grams_per_trudoden <= 0) {
      continue;
    }
    const std::int32_t days =
        line.days_to_harvest > 0 ? std::min(horizon, line.days_to_harvest) : horizon;
    bool fallback = false;
    const float trudodni = ForecastTrudodni(config, world, life_speedup, days, fallback);
    const float wanted = static_cast<float>(line.grams_per_trudoden) * trudodni;
    const auto free = static_cast<float>(FreeIssueStock(world, reserve, line.resource));
    issue_kcal_village +=
        std::min(wanted, free) * config.resources[line.resource.value].kcal_per_gram;
  }
  const float h = static_cast<float>(horizon);
  float first_short = h;
  for (std::size_t row = 0; row < families.size(); ++row) {
    const FamilyFood& food = families[row];
    if (!(food.members > 0.0F) || !(food.need_kcal_day > 0.0F)) {
      continue;
    }
    const FamilyRow& family = world.families.rows[row];
    // No worker of its own: no issue to come (econ §6).
    const float share = workers_all > 0.0F ? food.workers / workers_all : 0.0F;
    const float issue = issue_kcal_village * share;
    const float own = family.own_kcal_mean * h;
    const float need = food.need_kcal_day * h * share_c;
    if (food.pantry_kcal + issue + own >= need) {
      continue;
    }
    forecast.under_threat.push_back(food.id);
    // THE FIRST SHORT DAY: the pantry spent at the day's shortfall.
    const float shortfall_day = food.need_kcal_day * share_c - ((issue + own) / h);
    const float days = shortfall_day > 0.0F ? food.pantry_kcal / shortfall_day : h;
    first_short = std::min(first_short, std::clamp(days, 0.0F, h));
  }
  if (!forecast.under_threat.empty()) {
    forecast.days_to_first_short = static_cast<std::uint16_t>(std::floor(first_short));
  }
  return forecast;
}

}  // namespace core
