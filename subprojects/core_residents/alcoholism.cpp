// The drinking of Epoch I (core_residents/alcoholism.h).

#include "alcoholism.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/state_table_ops.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The world_params.csv keys, in the order of the knob list in the parse.
constexpr std::array<std::string_view, 11> kAlcoholismWorldParamKeys = {
    "alcohol_adult_from_years",
    "alcohol_gain_with_distiller",
    "alcohol_gain_winter_idle",
    "alcohol_gain_low_satisfaction",
    "alcohol_low_satisfaction_below",
    "alcohol_loss_employed",
    "alcohol_employed_days_min",
    "alcohol_loss_married",
    "alcohol_loss_sober",
    "alcohol_sober_months_min",
    "alcohol_epoch1_cap"};

/// The width of a band (crime design §6: 0-20, 20-40, 40-60, 60-80, 80-100).
constexpr float kBandWidth = 20.0F;
constexpr int kTopBandIndex = 4;

/// Past this the cell is a typo: nobody's age.
constexpr float kOldestYears = 120.0F;

bool IsWinterMonth(Month month) {
  return month == Month::kDecember || month == Month::kJanuary || month == Month::kFebruary;
}

bool VillageHasDistiller(const WorldState& current) {
  return std::ranges::any_of(current.residents.rows, [](const ResidentRow& person) {
    return person.night_trade == NightTrade::kDistiller;
  });
}

/// The month's change for one adult man.
float MonthChange(const AlcoholismConfig& config,
                  const WorldState& current,
                  const ResidentRow& person,
                  bool distiller,
                  bool sober_village,
                  bool winter) {
  const bool holds_post = person.post.profession.value != kInvalidDefIdValue;
  float change = distiller ? config.gain_with_distiller : 0.0F;
  // A post holder is not idle: his post keeps him out of the accountant's day
  // and so off the worked-days count.
  if (winter && !holds_post && person.days_worked_this_month == 0) {
    change += config.gain_winter_idle;
  }
  const std::uint32_t family_row = FindRow(current.families, person.family);
  if (family_row != kNoRow &&
      current.families.rows[family_row].satisfaction < config.low_satisfaction_below) {
    change += config.gain_low_satisfaction;
  }
  if (holds_post || static_cast<float>(person.days_worked_this_month) >= config.employed_days_min) {
    change -= config.loss_employed;
  }
  if (person.spouse.value != kInvalidEntityIdValue) {
    change -= config.loss_married;
  }
  if (sober_village) {
    change -= config.loss_sober;
  }
  return change;
}

}  // namespace

std::span<const std::string_view> AlcoholismWorldParamKeys() {
  return kAlcoholismWorldParamKeys;
}

bool ParseAlcoholismConfig(const ITableSet& tables, AlcoholismConfig& config, std::string& error) {
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  const Range points{.low = 0.0F, .high = kMetricMax};
  const std::array<ScalarKnob, kAlcoholismWorldParamKeys.size()> knobs = {{
      {.key = kAlcoholismWorldParamKeys[0],
       .value = &config.adult_from_years,
       .range = {.low = 0.0F, .high = kOldestYears}},
      {.key = kAlcoholismWorldParamKeys[1], .value = &config.gain_with_distiller, .range = points},
      {.key = kAlcoholismWorldParamKeys[2], .value = &config.gain_winter_idle, .range = points},
      {.key = kAlcoholismWorldParamKeys[3],
       .value = &config.gain_low_satisfaction,
       .range = points},
      {.key = kAlcoholismWorldParamKeys[4],
       .value = &config.low_satisfaction_below,
       .range = points},
      {.key = kAlcoholismWorldParamKeys[5], .value = &config.loss_employed, .range = points},
      {.key = kAlcoholismWorldParamKeys[6],
       .value = &config.employed_days_min,
       .range = {.low = 0.0F, .high = static_cast<float>(kDaysPerMonth)}},
      {.key = kAlcoholismWorldParamKeys[7], .value = &config.loss_married, .range = points},
      {.key = kAlcoholismWorldParamKeys[8], .value = &config.loss_sober, .range = points},
      {.key = kAlcoholismWorldParamKeys[9],
       .value = &config.sober_months_min,
       .range = {.low = 1.0F, .high = static_cast<float>(kMonthsPerYear)}},
      {.key = kAlcoholismWorldParamKeys[10], .value = &config.epoch1_cap, .range = points},
  }};
  return ReadKnobs(*world, "world_params", knobs, error);
}

int AlcoholismBand(float alcoholism) {
  const int index =
      std::clamp(static_cast<int>(std::floor(alcoholism / kBandWidth)), 0, kTopBandIndex);
  return index * static_cast<int>(kBandWidth);
}

void TurnAlcoholismMonth(const AlcoholismConfig& config, float life_speedup, WorldState& current) {
  const SimDay day = current.calendar.day;
  if (day == 0 || day % kDaysPerMonth != 0) {
    return;
  }
  // The month that closed: the one before today's.
  const std::uint32_t month_index = ((day / kDaysPerMonth) + kMonthsPerYear - 1U) % kMonthsPerYear;
  const bool winter = IsWinterMonth(static_cast<Month>(month_index));
  const bool distiller = VillageHasDistiller(current);
  // The supply is read at the turn, and so is its absence: a month counts
  // as dry when the turn that closes it finds no distiller, the same moment
  // the +2 is decided on, so the two can never disagree about one month.
  std::uint8_t& dry = current.night_theft.dry_months;
  if (distiller) {
    dry = 0;
  } else if (dry < UINT8_MAX) {
    ++dry;
  }
  const bool sober_village = static_cast<float>(dry) >= config.sober_months_min;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    ResidentRow& person = current.residents.rows[row];
    const float age_years = BiologicalAgeYears(life_speedup, person.birth_day, day);
    // MEN ONLY: «пьют мужчины», and a woman has no such metric at all (crime
    // design §6; boss, 2026-09-18). Until that day a woman's change was a
    // quarter of a man's — the numbers table contradicted its own section,
    // and the section was right. Her value stays at nought, and is written
    // so, not merely left: a save made before carries what the old rule gave.
    if (person.sex != Sex::kMale) {
      person.alcoholism = kMetricMin;
    } else if (age_years >= config.adult_from_years) {
      const float change = MonthChange(config, current, person, distiller, sober_village, winter);
      const float before = person.alcoholism;
      person.alcoholism = std::clamp(before + change, kMetricMin, config.epoch1_cap);
      const int band = AlcoholismBand(person.alcoholism);
      if (band != AlcoholismBand(before)) {
        SimEvent& crossed =
            EmitEvent(current, EventKind::kAlcoholismBandCrossed, EventSeverity::kRoutine);
        crossed.resident = current.residents.row_ids[row];
        crossed.family = person.family;
        crossed.amount = band;
      }
    }
    person.days_worked_this_month = 0;
  }
}

}  // namespace core
