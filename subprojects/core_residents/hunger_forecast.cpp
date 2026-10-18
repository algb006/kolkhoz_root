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

/// What production says comes in inside the horizon (#1046 (b); boss, the
/// #1046 thread [12]), by the world_params switch (FoodConfig::forecast_terms).
struct IssueInflows {
  ResourceAmounts standing;  ///< 1+: the crop in the fields and the heaps
  ResourceAmounts to_come;   ///< 1+: this year's harvest to come, net of next year's seed
  Grams milk_ahead = 0;      ///< 2: the milkings ahead, net of the cart
};

IssueInflows ReadIssueInflows(const FoodConfig& config,
                              const WorldState& world,
                              std::int32_t horizon) {
  IssueInflows inflows;
  if (config.forecast_terms >= 1 && config.standing_harvest) {
    inflows.standing = config.standing_harvest(world);
    if (config.harvest_to_come) {
      inflows.to_come = config.harvest_to_come(world);
    }
  }
  if (config.forecast_terms >= 2 && config.milk_to_issue_ahead && horizon > 1) {
    // Today's issue takes the milk lying now; each of the other mornings
    // takes the milking before it.
    inflows.milk_ahead = config.milk_to_issue_ahead(world, static_cast<std::uint32_t>(horizon - 1));
  }
  return inflows;
}

/// The crop a position's harvest leaves to share: the standing crop and its
/// heaps, at most this year's harvest to come net of next year's seed, less
/// what the district's plan still owes of it. NAMED DEVIATION from econ §7.2
/// («net of the funds by the norm's own rule»): the funds at the harvest's day
/// cannot be read today - the plan rung holds before the harvest only what
/// the harvest will not pay - so the plan's owed and the seed stand for them.
float CropToShare(const WorldState& world, const IssueInflows& inflows, ResourceId resource) {
  const Grams standing = AmountOf(inflows.standing, resource);
  const Grams net_of_seed =
      inflows.to_come.empty() ? standing : std::min(standing, AmountOf(inflows.to_come, resource));
  const Grams owed = world.plan.announced != 0 ? AmountOf(world.plan.due, resource) -
                                                     AmountOf(world.plan.delivered, resource)
                                               : 0;
  const Grams left = net_of_seed - std::max<Grams>(owed, 0);
  return left > 0 ? static_cast<float>(left) : 0.0F;
}

/// A position's issue to come, grams: `before` its harvest inside the
/// horizon (or to the horizon), and `after` it.
struct IssueTerms {
  float before = 0.0F;
  float after = 0.0F;
  std::int32_t before_days = 0;   ///< the days `before` spans
  std::int32_t harvest_day = -1;  ///< from which `after` is issued; -1 when none
};

IssueTerms PositionIssueTerms(const FoodConfig& config,
                              const WorldState& world,
                              float life_speedup,
                              const std::vector<Grams>& reserve,
                              const IssueInflows& inflows,
                              const IssueNormLine& line,
                              std::int32_t horizon) {
  IssueTerms terms;
  const std::int32_t days =
      line.days_to_harvest > 0 ? std::min(horizon, line.days_to_harvest) : horizon;
  terms.before_days = days;
  bool fallback = false;
  const float to_harvest = ForecastTrudodni(config, world, life_speedup, days, fallback);
  auto free = static_cast<float>(FreeIssueStock(world, reserve, line.resource));
  // THE MILK AHEAD (arm BM; seq 11: milk was 59 % of the missing issue, its
  // norm far above one day's stock, which was the whole cap).
  if (line.resource.value == config.carted_daily.value) {
    free += static_cast<float>(inflows.milk_ahead);
  }
  if (line.grams_per_trudoden > 0) {
    terms.before = std::min(static_cast<float>(line.grams_per_trudoden) * to_harvest, free);
  }
  // THE HARVEST INSIDE THE HORIZON (econ §7.2; arms B and BM): the norm the
  // world will have from the harvest's day, issued from that day to h only -
  // never the crop before it exists.
  const bool harvest_inside =
      !inflows.standing.empty() && line.days_to_harvest > 0 && line.days_to_harvest < horizon;
  if (!harvest_inside) {
    return terms;
  }
  const float after_trudodni =
      ForecastTrudodni(config, world, life_speedup, horizon, fallback) - to_harvest;
  if (!(after_trudodni > 0.0F)) {
    return terms;
  }
  const float remainder =
      std::max(free - terms.before, 0.0F) + CropToShare(world, inflows, line.resource);
  if (line.basis == IssueNormBasis::kShareOfRemainder) {
    if (!(line.trudodni > 0.0F)) {
      return terms;  // not in the default bundle
    }
    // The world's own rule from the harvest: the remainder × the issue share
    // over the trudodni to the next year's harvest.
    const float shared = remainder * config.resources[line.resource.value].issue_share_of_stock;
    const float to_next =
        ForecastTrudodni(
            config, world, life_speedup, days + static_cast<std::int32_t>(kDaysPerYear), fallback) -
        to_harvest;
    if (to_next > 0.0F) {
      terms.after = std::min(shared * after_trudodni / to_next, shared);
    }
  } else if (line.grams_per_trudoden > 0) {
    terms.after = std::min(static_cast<float>(line.grams_per_trudoden) * after_trudodni, remainder);
  }
  if (terms.after > 0.0F) {
    terms.harvest_day = line.days_to_harvest;
  }
  return terms;
}

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
  const IssueInflows inflows = ReadIssueInflows(config, world, horizon);
  float issue_kcal_village = 0.0F;
  std::vector<IssueTerms> position_terms;
  std::vector<float> position_kcal;
  std::int32_t first_harvest = horizon;  // the first day an `after` term starts
  for (const IssueNormLine& line : norms) {
    if (line.resource.value >= config.resources.size()) {
      continue;
    }
    const float kcal_per_gram = config.resources[line.resource.value].kcal_per_gram;
    const IssueTerms terms =
        PositionIssueTerms(config, world, life_speedup, reserve, inflows, line, horizon);
    issue_kcal_village += (terms.before + terms.after) * kcal_per_gram;
    if (terms.harvest_day > 0) {
      first_harvest = std::min(first_harvest, terms.harvest_day);
    }
    position_terms.push_back(terms);
    position_kcal.push_back(kcal_per_gram);
  }
  // THE DAYS BEFORE THE FIRST HARVEST (econ §8, U1): a crop that comes on
  // day 12 feeds nobody on day 5. Up to it, only the `before` terms, spread
  // evenly over their own days.
  float issue_kcal_to_first = 0.0F;
  if (first_harvest < horizon) {
    for (std::size_t index = 0; index < position_terms.size(); ++index) {
      const IssueTerms& terms = position_terms[index];
      if (terms.before_days > 0) {
        issue_kcal_to_first += terms.before * position_kcal[index] *
                               static_cast<float>(std::min(first_harvest, terms.before_days)) /
                               static_cast<float>(terms.before_days);
      }
    }
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
    // Two spans, the horizon and the days to the first harvest inside it: the
    // family is under threat when it runs short by the end of either.
    const float spans[2] = {h, static_cast<float>(first_harvest)};
    const float issues[2] = {issue_kcal_village * share, issue_kcal_to_first * share};
    bool short_by_one = false;
    float days = h;
    for (int span = 0; span < (first_harvest < horizon ? 2 : 1); ++span) {
      const float own = family.own_kcal_mean * spans[span];
      const float need = food.need_kcal_day * spans[span] * share_c;
      if (food.pantry_kcal + issues[span] + own >= need) {
        continue;
      }
      short_by_one = true;
      // THE FIRST SHORT DAY: the pantry spent at the span's daily shortfall.
      const float shortfall_day =
          food.need_kcal_day * share_c - ((issues[span] + own) / spans[span]);
      days = std::min(days, shortfall_day > 0.0F ? food.pantry_kcal / shortfall_day : h);
    }
    if (!short_by_one) {
      continue;
    }
    forecast.under_threat.push_back(food.id);
    first_short = std::min(first_short, std::clamp(days, 0.0F, h));
  }
  if (!forecast.under_threat.empty()) {
    forecast.days_to_first_short = static_cast<std::uint16_t>(std::floor(first_short));
  }
  return forecast;
}

}  // namespace core
