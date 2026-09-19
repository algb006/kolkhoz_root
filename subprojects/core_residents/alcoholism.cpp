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
constexpr std::array<std::string_view, 14> kAlcoholismWorldParamKeys = {
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
    "alcohol_epoch1_cap",
    "alcohol_inherit_threshold",
    "alcohol_inherit_factor",
    "alcohol_inherit_max"};

/// Past this the inheritance factor is a typo: more than the father's own
/// excess, ten times over.
constexpr float kInheritFactorMax = 10.0F;

/// The width of a band (crime design §6: 0-20, 20-40, 40-60, 60-80, 80-100).
constexpr float kBandWidth = 20.0F;
constexpr int kTopBandIndex = 4;

/// Past this the cell is a typo: nobody's age.
constexpr float kOldestYears = 120.0F;

bool IsWinterMonth(Month month) {
  return month == Month::kDecember || month == Month::kJanuary || month == Month::kFebruary;
}

/// The settlement's alcoholism: the mean of its men of 16 and over. STUB of
/// the boundary: one settlement to the map (register 207).
float SettlementAlcoholism(const AlcoholismConfig& config,
                           float life_speedup,
                           const WorldState& current) {
  float sum = 0.0F;
  std::uint32_t men = 0;
  for (const ResidentRow& person : current.residents.rows) {
    if (person.sex == Sex::kMale &&
        BiologicalAgeYears(life_speedup, person.birth_day, current.calendar.day) >=
            config.adult_from_years) {
      sum += person.alcoholism;
      ++men;
    }
  }
  return men > 0 ? sum / static_cast<float>(men) : 0.0F;
}

/// THE PURCHASE IN KIND (crime §6, «Самогон стоит семье»; register 205): the
/// drinker's family pays out of its own pantry, in the raw material's order,
/// into the distiller's family's pantry — a transfer, not a leak. Kilograms
/// by the drinker's band of the metric; an empty pantry buys nothing.
void BuySamogon(const NightTradeConfig& night,
                WorldState& current,
                std::uint32_t buyer_row,
                std::uint32_t distiller_row,
                float alcoholism) {
  // The design's bands read 21–40 and 41–60, so an edge belongs to the lower one.
  float kg = 0.0F;
  if (alcoholism > 2.0F * kBandWidth) {
    kg = night.buy_kg_abuses;
  } else if (alcoholism > kBandWidth) {
    kg = night.buy_kg_drinks;
  }
  const std::uint32_t payer = FindRow(current.families, current.residents.rows[buyer_row].family);
  const std::uint32_t seller =
      FindRow(current.families, current.residents.rows[distiller_row].family);
  if (!(kg > 0.0F) || payer == kNoRow || seller == kNoRow || payer == seller) {
    return;
  }
  Grams owed = GramsFromKilograms(kg);
  for (const ResourceId raw : night.raw_material) {
    if (owed <= 0) {
      break;
    }
    FamilyRow& from = current.families.rows[payer];
    if (from.pantry.size() <= raw.value || from.pantry[raw.value] <= 0) {
      continue;
    }
    const Grams paid = from.pantry[raw.value] < owed ? from.pantry[raw.value] : owed;
    from.pantry[raw.value] -= paid;
    FamilyRow& to = current.families.rows[seller];
    if (to.pantry.size() <= raw.value) {
      to.pantry.resize(static_cast<std::size_t>(raw.value) + 1U, 0);
    }
    to.pantry[raw.value] += paid;
    AddLedgerAmount(current.ledger.current.samogon_paid, raw, paid);
    owed -= paid;
  }
}

/// «ЕСТЬ САМОГОН» IS A YARD'S (register 207): a distiller supplied in the
/// month that closed (`closed_tag`), within reach of the yard. Asked once per
/// family, and its answer is the +2, the sobriety and the purchase alike —
/// three rules that can never disagree about one yard and one month. Moves
/// every family's dry_months by the answer; returns the supplier's resident
/// row per family row, kNoRow for a dry yard.
std::vector<std::uint32_t> TurnYardSuppliers(const NightTradeConfig& night,
                                             WorldState& current,
                                             std::uint32_t closed_tag) {
  std::vector<std::uint32_t> supplier(current.families.rows.size(), kNoRow);
  for (std::uint32_t family = 0; family < current.families.rows.size(); ++family) {
    FamilyRow& yard_row = current.families.rows[family];
    const std::uint32_t house = FindRow(current.units, yard_row.house);
    const Vec2 yard =
        house != kNoRow ? current.units.rows[house].position : yard_row.lost_house_position;
    supplier[family] = NearestSuppliedDistiller(night, current, yard, closed_tag);
    if (supplier[family] != kNoRow) {
      yard_row.dry_months = 0;
    } else if (yard_row.dry_months < UINT8_MAX) {
      ++yard_row.dry_months;
    }
  }
  return supplier;
}

/// THE SETTLEMENT'S ALCOHOLISM (crime §6, «Алкоголизм села»; register 207):
/// the mean of its men after the month's turn, and a crossing of the 20 or
/// the 40 line either way is news. Kept, so the next turn knows which side it
/// was on.
void TurnSettlementAlcoholism(const AlcoholismConfig& config,
                              float life_speedup,
                              WorldState& current) {
  const float was = current.night_theft.settlement_alcoholism;
  const float now = SettlementAlcoholism(config, life_speedup, current);
  current.night_theft.settlement_alcoholism = now;
  for (const float line : {kBandWidth, 2.0F * kBandWidth}) {
    if ((was < line) != (now < line)) {
      SimEvent& crossed =
          EmitEvent(current, EventKind::kSettlementAlcoholismCrossed, EventSeverity::kNotable);
      // The line, signed: +20 rose above it, −20 fell below.
      crossed.amount = static_cast<std::int64_t>(now >= line ? line : -line);
    }
  }
}

/// The month's change for one adult man.
float MonthChange(const AlcoholismConfig& config,
                  const WorldState& current,
                  const ResidentRow& person,
                  bool samogon_at_yard,
                  bool sober_yard,
                  bool winter,
                  float field_loss,
                  float sportiness_loss) {
  const bool holds_post = person.post.profession.value != kInvalidDefIdValue;
  float change = samogon_at_yard ? config.gain_with_distiller : 0.0F;
  // THE SPORTS FIELD AND SPORTINESS (sport.h; register 223; question 225):
  // −1 to a man who went to the field in a counted month, and −1 more once
  // his sportiness has reached `sober_from` — «спорт стал его».
  change -= field_loss + sportiness_loss;
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
  if (sober_yard) {
    change -= config.loss_sober;
  }
  return change;
}

/// THE READING HUT (sport.h; lever ②): a winter month's goer within reach
/// of an open hut drinks less — but one «goes» a month, so the field's and
/// the hut's losses never add up. (The hut's other half, the idle man's
/// winter gain taken off, is the caller's `winter && !hut_reached`.)
float GoerLoss(const SportConfig& sport,
               const ResidentRow& person,
               float age_years,
               bool went_to_field,
               bool hut_reached) {
  const bool went_to_hut = hut_reached && GoesBySelf(sport, person, age_years);
  return std::max(went_to_field ? sport.field_alcohol_loss : 0.0F,
                  went_to_hut ? sport.hut_alcohol_loss : 0.0F);
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
      {.key = kAlcoholismWorldParamKeys[11], .value = &config.inherit_threshold, .range = points},
      {.key = kAlcoholismWorldParamKeys[12],
       .value = &config.inherit_factor,
       .range = {.low = 0.0F, .high = kInheritFactorMax}},
      {.key = kAlcoholismWorldParamKeys[13], .value = &config.inherit_max, .range = points},
  }};
  return ReadKnobs(*world, "world_params", knobs, error);
}

int AlcoholismBand(float alcoholism) {
  const int index =
      std::clamp(static_cast<int>(std::floor(alcoholism / kBandWidth)), 0, kTopBandIndex);
  return index * static_cast<int>(kBandWidth);
}

void TurnAlcoholismMonth(const AlcoholismConfig& config,
                         const NightTradeConfig& night,
                         const SportConfig& sport,
                         float life_speedup,
                         WorldState& current) {
  const SimDay day = current.calendar.day;
  if (day == 0 || day % kDaysPerMonth != 0) {
    return;
  }
  // The month that closed: the one before today's.
  const std::uint32_t month_index = ((day / kDaysPerMonth) + kMonthsPerYear - 1U) % kMonthsPerYear;
  const bool winter = IsWinterMonth(static_cast<Month>(month_index));
  const std::vector<std::uint32_t> supplier =
      TurnYardSuppliers(night, current, SupplyMonthTag(day - 1U));
  const bool field_month = SportMonthCounted(sport, current);
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    ResidentRow& person = current.residents.rows[row];
    const float age_years = BiologicalAgeYears(life_speedup, person.birth_day, day);
    // Read on the month's opening state: whether he went, and what his
    // sportiness stood at, before either moves (sport.h).
    const bool adult = age_years >= config.adult_from_years;
    const bool went = adult && field_month && GoesToTheField(sport, current, person, age_years);
    const bool hut_reached = adult && winter && ReachesTheHut(sport, current, person);
    const float field_loss = GoerLoss(sport, person, age_years, went, hut_reached);
    const float sportiness_loss =
        adult && person.sportiness >= sport.sober_from ? sport.sportiness_alcohol_loss : 0.0F;
    if (adult) {
      TurnSportiness(sport, went, age_years, person);
    }
    // MEN ONLY: «пьют мужчины», and a woman has no such metric at all (crime
    // design §6; boss, 2026-09-18). Until that day a woman's change was a
    // quarter of a man's — the numbers table contradicted its own section,
    // and the section was right. Her value stays at nought, and is written
    // so, not merely left: a save made before carries what the old rule gave.
    if (person.sex != Sex::kMale) {
      person.alcoholism = kMetricMin;
    } else if (age_years >= config.adult_from_years) {
      const std::uint32_t family = FindRow(current.families, person.family);
      const std::uint32_t distiller = family != kNoRow ? supplier[family] : kNoRow;
      const bool sober_yard =
          family != kNoRow &&
          static_cast<float>(current.families.rows[family].dry_months) >= config.sober_months_min;
      const float before = person.alcoholism;
      // THE PURCHASE, by the band of the month that closed — what he drank in
      // it is what his family pays for (register 205).
      if (distiller != kNoRow) {
        BuySamogon(night, current, row, distiller, before);
      }
      const float change = MonthChange(config,
                                       current,
                                       person,
                                       distiller != kNoRow,
                                       sober_yard,
                                       winter && !hut_reached,
                                       field_loss,
                                       sportiness_loss);
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
  // The field's month is read; the new month counts from nought.
  current.sport_month.open_days = 0;
  TurnSettlementAlcoholism(config, life_speedup, current);
}

void InheritAlcoholismDay(const AlcoholismConfig& config, float life_speedup, WorldState& current) {
  const SimDay day = current.calendar.day;
  if (day == 0) {
    return;
  }
  for (ResidentRow& son : current.residents.rows) {
    if (son.sex != Sex::kMale ||
        BiologicalAgeYears(life_speedup, son.birth_day, day) < config.adult_from_years ||
        BiologicalAgeYears(life_speedup, son.birth_day, day - 1U) >= config.adult_from_years) {
      continue;
    }
    const std::uint32_t father = FindRow(current.residents, son.father);
    const float excess = father != kNoRow
                             ? current.residents.rows[father].alcoholism - config.inherit_threshold
                             : 0.0F;
    son.alcoholism = std::clamp(excess * config.inherit_factor, kMetricMin, config.inherit_max);
  }
}

}  // namespace core
