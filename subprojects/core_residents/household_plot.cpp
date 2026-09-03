// The private plot (household_plot.h): the day's hours and the garden.
//
// The model is additive, as the design's own table is, but the BASE is not a
// seasonal constant: it is the exact remainder of the day of those who went
// out to work — 24 hours less sleep less the hours away, the stage-5
// arithmetic with the road and the distance to the job already inside it.
// The design's "housing nearby +1.0 / distant job -1.5" factors are that
// same quantity guessed at, so they are not applied on top of it: that would
// count the walk twice.

#include "household_plot.h"

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {
namespace {

float BiologicalAgeYears(float life_speedup, std::int32_t birth_day, SimDay day) {
  const float game_years = static_cast<float>(static_cast<std::int32_t>(day) - birth_day) /
                           static_cast<float>(kDaysPerYear);
  return game_years * life_speedup;
}

std::uint32_t EpochIndex(Epoch epoch) {
  return static_cast<std::uint32_t>(epoch) - 1;
}

bool MonthInRange(std::uint8_t month, std::uint8_t from, std::uint8_t to) {
  return month >= from && month <= to;
}

/// What the household looks like today, as the plot table asks about it.
struct YardToday {
  float away_hours_total = 0.0F;
  std::uint32_t members = 0;
  std::uint32_t workers = 0;
  std::uint32_t schoolchildren = 0;
  bool has_elder = false;
  bool has_drinker = false;
  bool has_sick = false;
};

YardToday SurveyYard(const PlotConfig& plot,
                     float life_speedup,
                     const WorldState& current,
                     FamilyId id,
                     SimDay day) {
  YardToday yard;
  for (const ResidentRow& resident : current.residents.rows) {
    if (resident.family.value != id.value) {
      continue;
    }
    ++yard.members;
    const float age = BiologicalAgeYears(life_speedup, resident.birth_day, day);
    if (resident.work.hours_away_today > 0.0F) {
      yard.away_hours_total += resident.work.hours_away_today;
      ++yard.workers;
    }
    if (age >= plot.elder_from_bio_years) {
      yard.has_elder = true;
    }
    if (age >= plot.schoolchild_from_bio_years && age < plot.schoolchild_to_bio_years) {
      ++yard.schoolchildren;
    }
    if (resident.alcoholism >= plot.drinker_alcoholism_threshold) {
      yard.has_drinker = true;
    }
    if (resident.health < plot.sickness_health_threshold) {
      yard.has_sick = true;
    }
  }
  return yard;
}

/// The additive table of household design §1 over the day's remainder.
/// Elders carry BOTH signs around the same base — the design's table lists
/// the yard with them and the yard without them as two rows, so their
/// absence costs exactly what their presence is worth.
float PlotHours(const PlotConfig& plot, const YardToday& yard, std::uint8_t month) {
  if (yard.members == 0) {
    return 0.0F;  // nobody lives here: an empty yard digs no garden
  }
  float hours = plot.no_worker_base_hours;
  if (yard.workers > 0) {
    const float away = yard.away_hours_total / static_cast<float>(yard.workers);
    const float left = static_cast<float>(kTicksPerDay) - plot.sleep_hours - away;
    hours = left > 0.0F ? left : 0.0F;
  }
  hours += yard.has_elder ? plot.elders_hours : -plot.elders_hours;
  if (yard.schoolchildren > 0) {
    const bool summer = MonthInRange(month, plot.summer_from_month, plot.summer_to_month);
    const float each = summer ? plot.schoolchild_summer_hours : plot.schoolchild_hours;
    const float added = each * static_cast<float>(yard.schoolchildren);
    hours += added < plot.schoolchild_hours_cap ? added : plot.schoolchild_hours_cap;
  }
  if (yard.has_drinker) {
    hours -= plot.drinker_hours;
  }
  if (yard.has_sick) {
    hours -= plot.sickness_hours;
  }
  return hours > 0.0F ? hours : 0.0F;
}

void AddToPantry(FamilyRow& family, ResourceId resource, float kilograms) {
  const auto amount = GramsFromKilograms(kilograms);
  if (resource.value == kInvalidDefIdValue || amount <= 0) {
    return;
  }
  if (family.pantry.size() <= resource.value) {
    family.pantry.resize(resource.value + 1U, 0);
  }
  family.pantry[resource.value] += amount;
}

/// @brief The season's attention so far, as the harvests scale by it.
float SeasonRatio(const FamilyRow& family) {
  return family.plot_ratio_days > 0
             ? family.plot_ratio_sum / static_cast<float>(family.plot_ratio_days)
             : 0.0F;
}

/// The yard's own haymaking. It is carried in a month before the garden and
/// does NOT reset the season's accumulators — the garden closes the season,
/// the scythe only interrupts it.
void MowHay(const FoodConfig& config, FamilyRow& family) {
  AddToPantry(family, config.hay_resource, config.plot.hay_kg_per_yard_year * SeasonRatio(family));
}

/// The garden pays out on the last day of its month, so that the whole month
/// has been counted into the season's average first. Snow does not ruin a
/// garden (farming design §6): the harvest always lands, the only question
/// is how much attention it got. This is also where the season closes.
void HarvestGarden(const FoodConfig& config, FamilyRow& family) {
  const float ratio = SeasonRatio(family);
  AddToPantry(family, config.potato_resource, config.plot.potato_kg_per_yard_year * ratio);
  AddToPantry(family, config.vegetables_resource, config.plot.vegetables_kg_per_yard_year * ratio);
  family.plot_ratio_sum = 0.0F;
  family.plot_ratio_days = 0;
}

}  // namespace

void RunHouseholdPlot(const FoodConfig& config,
                      float life_speedup,
                      WorldState& current,
                      std::uint32_t family_item) {
  if (HourFromTick(current.calendar.tick) != kTicksPerDay - 2U) {
    return;  // once a day, at the last hour the day's orders are still alive
  }
  if (EpochIndex(current.epoch) >= config.satiety.categories_norm_by_epoch.size()) {
    return;  // an epoch this config does not know
  }
  const PlotConfig& plot = config.plot;
  const SimDay day = current.calendar.day;
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  const FamilyId id = current.families.row_ids[family_item];
  FamilyRow& family = current.families.rows[family_item];

  const YardToday yard = SurveyYard(plot, life_speedup, current, id, day);
  family.household_hours = PlotHours(plot, yard, month);

  // The garden's yield scales by the season's average attention: the daily
  // ratio min(1, hours / full-yield hours) is the curve the design's own
  // yard examples draw (7.5 h -> 100%, 2.7 -> 68%, 1.2 -> 30%, 0.5 -> 12%).
  if (MonthInRange(month, plot.growing_from_month, plot.growing_to_month) &&
      plot.full_yield_hours > 0.0F) {
    const float ratio = family.household_hours / plot.full_yield_hours;
    family.plot_ratio_sum += ratio < 1.0F ? ratio : 1.0F;
    family.plot_ratio_days += 1;
  }
  if (current.calendar.date.day_in_month != kDaysPerMonth - 1U) {
    return;
  }
  if (month == plot.hay_harvest_month) {
    MowHay(config, family);
  }
  if (month == plot.garden_harvest_month) {
    HarvestGarden(config, family);
  }
}

}  // namespace core
