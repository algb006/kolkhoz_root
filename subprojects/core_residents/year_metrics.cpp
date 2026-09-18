#include "year_metrics.h"

#include <algorithm>
#include <bit>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/stock_forecast.h"
#include "core_common/unit_state.h"
#include "family_meal.h"

namespace core {

namespace {

/// The first of December, as a day of the year. The month is 0-based in the
/// calendar and the design's date is human, so the conversion is spelled out
/// once here rather than as a bare 44 somewhere.
constexpr std::uint32_t kDecemberFirstDayOfYear =
    static_cast<std::uint32_t>(Month::kDecember) * kDaysPerMonth;

/// Everything edible of one resource, wherever it lies.
Grams EdibleHeld(const WorldState& world, ResourceId resource) {
  Grams total = 0;
  for (const UnitRow& unit : world.units.rows) {
    if (unit.level != 0) {
      total += UnreservedOf(unit, resource);
    }
  }
  for (const FamilyRow& family : world.families.rows) {
    total += AmountOf(family.pantry, resource);
  }
  return total;
}

}  // namespace

float SettlementFoodDays(const FoodConfig& food, const LifeConfig& life, const WorldState& world) {
  const float need_kg = SettlementDailyNeedKilograms(food, life.life_speedup, world);
  if (!(need_kg > 0.0F)) {
    // An empty village eats nothing, and that is an ANSWER rather than a
    // missing value: the question was asked and the stock never runs out.
    return static_cast<float>(kStockForecastHorizonDays);
  }
  const float reference = food.consumption.grain_reference_kcal_per_gram;
  double kcal = 0.0;
  if (reference > 0.0F) {
    for (std::uint32_t resource = 0; resource < food.resources.size(); ++resource) {
      const float density = food.resources[resource].kcal_per_gram;
      if (!(density > 0.0F)) {
        continue;
      }
      const ResourceId id = DefIdFromIndex<ResourceIdTag>(resource);
      kcal += static_cast<double>(EdibleHeld(world, id)) * static_cast<double>(density);
    }
  }
  const double need_kcal = static_cast<double>(need_kg) * static_cast<double>(kGramsPerKilogram) *
                           static_cast<double>(reference);
  const double days = need_kcal > 0.0 ? kcal / need_kcal : 0.0;
  return days >= static_cast<double>(kStockForecastHorizonDays)
             ? static_cast<float>(kStockForecastHorizonDays)
             : static_cast<float>(days);
}

void AccumulateYearMetrics(const FoodConfig& food, const LifeConfig& life, WorldState& world) {
  YearLedger& book = world.ledger.current;
  for (const FamilyRow& family : world.families.rows) {
    book.satisfaction_sum += family.satisfaction;
    ++book.satisfaction_samples;
  }
  // ONE PERSON-DAY PER ABLE-BODIED VILLAGER, ON WORKING DAYS ONLY, and the
  // unit is chosen to match what it will be divided into:
  // `total_assignment_days` counts one day of one person's assignment, so the
  // denominator counts one day of one person's availability. The ledger's own
  // mechanisation columns record what the alternative costs — a count over a
  // sum of fractions can pass 1 and means nothing on the way there.
  //
  // THE REST DAYS ARE OUT, and leaving them in would have been a silent
  // ceiling rather than a wrong number (boss, parcel 134). A year is 48 days
  // of which about 41 are worked; no assignment is ever given on a holiday,
  // so a denominator of every day caps the component near 85 per cent
  // FOREVER, in every campaign, and nobody would ever learn why the social
  // index would not fill. A component that cannot reach its own ceiling is a
  // rule that cannot fire, one storey down. With rest days out, a hundred
  // means "everybody able was out on every working day" — reachable, and
  // meaning what it says.
  //
  // It is also the right reading of the component: a holiday is a day off
  // that nobody may declare a working day (calendar.h), so it is not a yard's
  // choice and not the drift into the private plot this measures.
  //
  // THE AGE IS THIS MODULE'S, biological like every other age rule here.
  // core_labor keeps its own working age in labor.csv and the two are not
  // guaranteed equal; the share would be a ratio of two different villages
  // if this loop borrowed that one, so it uses the threshold that belongs to
  // the module doing the counting and says so.
  if (!IsDayOffIn(world, world.calendar.day)) {
    for (const ResidentRow& person : world.residents.rows) {
      const float age = BiologicalAgeYears(life.life_speedup, person.birth_day, world.calendar.day);
      if (age >= life.body.age_adult_from_years) {
        book.able_bodied_days += 1.0F;
      }
    }
  }
  // THE SEASON THAT JUST ENDED, read on the one day it can be. The masks are
  // cleared at the evening meal of a season's FIRST day, so at this day's
  // turn the finished season still stands whole in them.
  if (IsFirstDayOfSeason(world.calendar.day) && !world.families.rows.empty()) {
    float categories = 0.0F;
    for (const FamilyRow& family : world.families.rows) {
      categories += static_cast<float>(std::popcount(family.food_variety_mask));
    }
    const float village_mean = categories / static_cast<float>(world.families.rows.size());
    book.worst_season_variety = book.variety_seasons_seen == 0
                                    ? village_mean
                                    : std::min(book.worst_season_variety, village_mean);
    book.variety_seasons_seen = static_cast<std::uint8_t>(book.variety_seasons_seen + 1U);
  }
  if (world.calendar.day % kDaysPerYear != kDecemberFirstDayOfYear) {
    return;
  }
  // THE WINTERING IS SAMPLED ON ITS DATE AND THE DAYS ARE STORED RAW. The
  // share against the days to the grass is taken once, by the reader, off
  // three numbers each of which can be checked against the world — the feed
  // half and the date are core_production's and are booked there.
  book.food_days_dec1 = SettlementFoodDays(food, life, world);
  book.winter_cover_taken = 1;
}

}  // namespace core
