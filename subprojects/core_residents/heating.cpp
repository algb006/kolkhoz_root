// #2261 heating's reader (heating.h). Stage 0: the numbers are read; nothing acts on them yet.

#include "heating.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core_catalog/table_value.h"
#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {
namespace {

/// The keys in the order ParseHeatingConfig reads them (design.db world_param, sort 410).
constexpr std::array<std::string_view, 42> kHeatingWorldParamKeys = {
    "heat_threshold_c",
    "burn_m3_per_degree_day",
    "burn_dilapidated_factor",
    "burn_dugout_factor",
    "burn_level_factor",
    "burn_insulated_factor",
    "coal_m3_per_t",
    "heat_buffer_m3",
    "dilapidated_wear_pct",
    "heat_refill_below_m3",
    "heat_start_buffer_m3",
    "heat_empty_output_penalty_pct",
    "heat_indoor_warm_offset_c",
    "heat_indoor_warm_cap_c",
    "heat_indoor_cold_offset_c",
    "heat_close_below_c",
    "cold_scale_max",
    "cold_gain_night_home",
    "cold_gain_hour_per_10c_frost",
    "cold_loss_night_warm",
    "cold_cold_at",
    "cold_frozen_at",
    "cold_work_penalty_pct",
    "cold_mood_penalty",
    "cold_sickness_pct_day",
    "cold_frozen_sick_chance_pct",  // econ 27 Nov: replaces cold_sickness_frozen_pct_day
    "cold_hardy_offset",
    "cold_weak_offset",
    "cold_era1_offset",
    "cold_clothes_both_factor",
    "cold_clothes_one_factor",
    "cold_bath_relief",
    "cold_hot_meal_relief",
    "deadwood_m3_per_trip",
    "deadwood_full_within_m",
    "deadwood_zero_at_m",
    "cook_home_m3_per_resident_day",
    "cook_meal_m3",
    // econ's five follow-ups (27 Nov): the bath day's extra burn, the cold sickness's split.
    "bath_day_extra_m3",
    "cold_sick_chill_pct",
    "cold_sick_flu_pct",
    "cold_sick_angina_pct"};

/// A refill that came within this of what was wanted is a whole one (float m³ from grams).
constexpr float kM3Slack = 1e-3F;

Grams GramsOfM3(const HeatingConfig& config, float m3) {
  return static_cast<Grams>(std::llround(static_cast<double>(m3) * config.firewood_grams_per_m3));
}

float M3OfGrams(const HeatingConfig& config, Grams grams) {
  return config.firewood_grams_per_m3 > 0.0
             ? static_cast<float>(static_cast<double>(grams) / config.firewood_grams_per_m3)
             : 0.0F;
}

/// Up to `wanted` grams of firewood off the standing units that hold it (not the unit being
/// refilled, not a works' site, above what a works holds back), in unit id order.
Grams TakeFirewood(const HeatingConfig& config,
                   WorldState& world,
                   std::size_t receiver_row,
                   Grams wanted) {
  const ResourceId firewood = config.firewood;
  if (firewood.value == kInvalidDefIdValue || wanted <= 0) {
    return 0;
  }
  Grams taken = 0;
  for (const std::uint32_t row : RowsById(world.units)) {
    if (taken >= wanted) {
      break;
    }
    UnitRow& unit = world.units.rows[row];
    if (row == receiver_row || unit.dead != 0 || unit.level == 0 ||
        unit.construction.phase != ConstructionPhase::kNone ||
        unit.stock.size() <= firewood.value) {
      continue;
    }
    const Grams free = UnreservedOf(unit, firewood);
    const Grams take = std::min(free, wanted - taken);
    if (take > 0) {
      unit.stock[firewood.value] -= take;
      taken += take;
    }
  }
  return taken;
}

/// One deadwood trip's m³ from `home`: whole within deadwood_full_within_m of the nearest
/// forest stand holding stock, linear to 0 at deadwood_zero_at_m, 0 with no stand.
float DeadwoodTripM3(const HeatingConfig& config, const WorldState& world, Vec2 home) {
  if (!(config.deadwood_m3_per_trip > 0.0F)) {
    return 0.0F;
  }
  float nearest = -1.0F;
  for (const TimberStandRow& stand : world.stands.rows) {
    if (!(stand.stock_m3 > 0.0F)) {
      continue;
    }
    const float d = std::hypot(stand.position.x - home.x, stand.position.y - home.y);
    if (nearest < 0.0F || d < nearest) {
      nearest = d;
    }
  }
  if (nearest < 0.0F || nearest >= config.deadwood_zero_at_m) {
    return 0.0F;
  }
  if (nearest <= config.deadwood_full_within_m) {
    return config.deadwood_m3_per_trip;
  }
  const float span = config.deadwood_zero_at_m - config.deadwood_full_within_m;
  return span > 0.0F ? config.deadwood_m3_per_trip * (config.deadwood_zero_at_m - nearest) / span
                     : 0.0F;
}

}  // namespace

std::span<const std::string_view> HeatingWorldParamKeys() {
  return kHeatingWorldParamKeys;
}

bool ParseHeatingConfig(const ITableSet& tables, HeatingConfig& config, std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    // The ranges are design.db's min_ok/max_ok, widened to the physically possible where a
    // tuning pair may need to step past them.
    const auto r = [](float low, float high) { return Range{.low = low, .high = high}; };
    const std::array<ScalarKnob, kHeatingWorldParamKeys.size()> knobs = {{
        {.key = kHeatingWorldParamKeys[0], .value = &config.heat_threshold_c, .range = r(-10, 25)},
        {.key = kHeatingWorldParamKeys[1],
         .value = &config.burn_m3_per_degree_day,
         .range = r(0, 1)},
        {.key = kHeatingWorldParamKeys[2],
         .value = &config.burn_dilapidated_factor,
         .range = r(1, 3)},
        {.key = kHeatingWorldParamKeys[3], .value = &config.burn_dugout_factor, .range = r(1, 3)},
        {.key = kHeatingWorldParamKeys[4], .value = &config.burn_level_factor, .range = r(0.1F, 1)},
        {.key = kHeatingWorldParamKeys[5],
         .value = &config.burn_insulated_factor,
         .range = r(0.1F, 1)},
        {.key = kHeatingWorldParamKeys[6], .value = &config.coal_m3_per_t, .range = r(0.5F, 10)},
        {.key = kHeatingWorldParamKeys[7], .value = &config.heat_buffer_m3, .range = r(0, 50)},
        {.key = kHeatingWorldParamKeys[8],
         .value = &config.dilapidated_wear_pct,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[9],
         .value = &config.heat_refill_below_m3,
         .range = r(0, 50)},
        {.key = kHeatingWorldParamKeys[10],
         .value = &config.heat_start_buffer_m3,
         .range = r(0, 50)},
        {.key = kHeatingWorldParamKeys[11],
         .value = &config.heat_empty_output_penalty_pct,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[12],
         .value = &config.heat_indoor_warm_offset_c,
         .range = r(0, 60)},
        {.key = kHeatingWorldParamKeys[13],
         .value = &config.heat_indoor_warm_cap_c,
         .range = r(5, 30)},
        {.key = kHeatingWorldParamKeys[14],
         .value = &config.heat_indoor_cold_offset_c,
         .range = r(0, 30)},
        {.key = kHeatingWorldParamKeys[15], .value = &config.heat_close_below_c, .range = r(0, 25)},
        {.key = kHeatingWorldParamKeys[16], .value = &config.cold_scale_max, .range = r(10, 500)},
        {.key = kHeatingWorldParamKeys[17],
         .value = &config.cold_gain_night_home,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[18],
         .value = &config.cold_gain_hour_per_10c_frost,
         .range = r(0, 20)},
        {.key = kHeatingWorldParamKeys[19],
         .value = &config.cold_loss_night_warm,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[20], .value = &config.cold_cold_at, .range = r(0, 500)},
        {.key = kHeatingWorldParamKeys[21], .value = &config.cold_frozen_at, .range = r(0, 500)},
        {.key = kHeatingWorldParamKeys[22],
         .value = &config.cold_work_penalty_pct,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[23], .value = &config.cold_mood_penalty, .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[24],
         .value = &config.cold_sickness_pct_day,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[25],
         .value = &config.cold_frozen_sick_chance_pct,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[26], .value = &config.cold_hardy_offset, .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[27], .value = &config.cold_weak_offset, .range = r(-100, 0)},
        {.key = kHeatingWorldParamKeys[28],
         .value = &config.cold_era1_offset,
         .range = r(-100, 100)},
        {.key = kHeatingWorldParamKeys[29],
         .value = &config.cold_clothes_both_factor,
         .range = r(0, 1)},
        {.key = kHeatingWorldParamKeys[30],
         .value = &config.cold_clothes_one_factor,
         .range = r(0, 1)},
        {.key = kHeatingWorldParamKeys[31], .value = &config.cold_bath_relief, .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[32],
         .value = &config.cold_hot_meal_relief,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[33],
         .value = &config.deadwood_m3_per_trip,
         .range = r(0, 5)},
        {.key = kHeatingWorldParamKeys[34],
         .value = &config.deadwood_full_within_m,
         .range = r(0, 20000)},
        {.key = kHeatingWorldParamKeys[35],
         .value = &config.deadwood_zero_at_m,
         .range = r(0, 20000)},
        {.key = kHeatingWorldParamKeys[36],
         .value = &config.cook_home_m3_per_resident_day,
         .range = r(0, 1)},
        {.key = kHeatingWorldParamKeys[37], .value = &config.cook_meal_m3, .range = r(0, 1)},
        {.key = kHeatingWorldParamKeys[38], .value = &config.bath_day_extra_m3, .range = r(0, 5)},
        {.key = kHeatingWorldParamKeys[39],
         .value = &config.cold_sick_chill_pct,
         .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[40], .value = &config.cold_sick_flu_pct, .range = r(0, 100)},
        {.key = kHeatingWorldParamKeys[41],
         .value = &config.cold_sick_angina_pct,
         .range = r(0, 100)},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
  }
  // weather.csv's season means (the splitting's demand reads the coming months by them).
  if (const ITable* const weather = tables.FindTable("weather")) {
    const std::uint32_t mean = weather->FindColumn("temp_mean_c");
    constexpr std::array<std::string_view, 4> kSeasons = {"winter", "spring", "summer", "autumn"};
    for (std::size_t season = 0; season < kSeasons.size() && mean != kNoTableColumn; ++season) {
      const std::uint32_t row = weather->FindRowByKey(kSeasons[season]);
      if (row != kNoTableRow) {
        config.season_mean_c[season] = weather->CellReal(row, mean).value_or(0.0F);
      }
    }
  }
  // The firewood resource and its grams a m³ (resources.csv kg_per_unit; measure m3).
  if (const ITable* const resources = tables.FindTable("resources")) {
    const std::uint32_t row = resources->FindRowByKey("firewood");
    const std::uint32_t kg = resources->FindColumn("kg_per_unit");
    if (row != kNoTableRow && kg != kNoTableColumn) {
      config.firewood = ResourceId{static_cast<std::uint16_t>(row)};
      config.firewood_grams_per_m3 =
          static_cast<double>(resources->CellReal(row, kg).value_or(0.0F)) * 1000.0;
    }
  }
  // unit_types.csv heat_size_factor: empty = 1.0 (a house's worth). Absent column: all 1.0.
  // And has_heating (construction_config.cpp validates it 0/1 for the insulation; read here
  // again for the burn - one column, two readers, the same truth).
  config.heat_size_factor.clear();
  config.has_heating.clear();
  config.is_housing.clear();
  if (const ITable* const types = tables.FindTable("unit_types")) {
    const std::uint32_t heating = types->FindColumn("has_heating");
    config.has_heating.assign(types->RowCount(), 0);
    const std::uint32_t klass = types->FindColumn("class");
    config.is_housing.assign(types->RowCount(), 0);
    for (std::uint32_t row = 0; klass != kNoTableColumn && row < types->RowCount(); ++row) {
      config.is_housing[row] = types->CellText(row, klass) == "housing" ? 1 : 0;
    }
    if (heating != kNoTableColumn) {
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        config.has_heating[row] = types->CellReal(row, heating).value_or(0.0F) > 0.5F ? 1 : 0;
      }
    }
    const std::uint32_t column = types->FindColumn("heat_size_factor");
    config.heat_size_factor.assign(types->RowCount(), 1.0F);
    if (column != kNoTableColumn) {
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        const std::optional<float> value = types->CellReal(row, column);
        if (value && *value > 0.0F) {
          config.heat_size_factor[row] = *value;
        }
      }
    }
  }
  return true;
}

float HeatingBurnToday(const HeatingConfig& config, const UnitRow& unit, float outdoor_mean_c) {
  const float degrees = config.heat_threshold_c - outdoor_mean_c;
  if (!(degrees > 0.0F)) {
    return 0.0F;
  }
  const std::size_t type = unit.type.value;
  float burn = degrees * config.burn_m3_per_degree_day;
  burn *= type < config.heat_size_factor.size() ? config.heat_size_factor[type] : 1.0F;
  if (unit.wear >= config.dilapidated_wear_pct) {
    burn *= config.burn_dilapidated_factor;
  }
  for (std::uint8_t level = 1; level < unit.level; ++level) {
    burn *= config.burn_level_factor;
  }
  if (unit.insulated != 0) {
    burn *= config.burn_insulated_factor;
  }
  return burn;
}

void RunHeatingBurn(const HeatingConfig& config,
                    float life_speedup,
                    float adult_age_years,
                    WorldState& world) {
  HeatingState& heating = world.heating;
  // Inhabited = some family names the unit as its house (settlement_rules' InhabitedUnits, the
  // same rule: a barrack holds several families, UnitRow::household names one at most).
  std::vector<std::uint8_t> inhabited(world.units.rows.size(), 0);
  for (const FamilyRow& family : world.families.rows) {
    const std::uint32_t row = FindRow(world.units, family.house);
    if (row != kNoRow) {
      inhabited[row] = 1;
    }
  }
  // A dwelling some ADULT lives in (the deadwood's gatherer, stage 3c).
  std::vector<std::uint8_t> adult_home(world.units.rows.size(), 0);
  for (const ResidentRow& resident : world.residents.rows) {
    if (BiologicalAgeYears(life_speedup, resident.birth_day, world.calendar.day) <
        adult_age_years) {
      continue;
    }
    const std::uint32_t family = FindRow(world.families, resident.family);
    if (family == kNoRow) {
      continue;
    }
    const std::uint32_t row = FindRow(world.units, world.families.rows[family].house);
    if (row != kNoRow) {
      adult_home[row] = 1;
    }
  }
  const auto heated = [&config](const UnitRow& unit) {
    const std::size_t type = unit.type.value;
    return unit.dead == 0 && unit.level > 0 && type < config.has_heating.size() &&
           config.has_heating[type] != 0;
  };
  if (heating.seeded == 0) {
    // The start's firewood: heat_start_buffer_m3 into each inhabited heated dwelling (econ's
    // pick for the 40 m³ that heating §2 forbids in the log pile).
    for (std::size_t row = 0; row < world.units.rows.size(); ++row) {
      UnitRow& unit = world.units.rows[row];
      if (heated(unit) && inhabited[row] != 0) {
        unit.heat_buffer_m3 = config.heat_start_buffer_m3;
      }
    }
    heating.seeded = 1;
  }
  const float outdoor = world.weather.air_temperature_celsius;
  // THE SPLITTING'S DEMAND (stage 3b): Σ of each burning unit's m³ a degree-day, times the
  // coming days' degrees below the threshold at their season's mean.
  double per_degree_day = 0.0;
  for (std::size_t row = 0; row < world.units.rows.size(); ++row) {
    UnitRow& unit = world.units.rows[row];
    if (!heated(unit)) {
      continue;
    }
    const std::size_t type = unit.type.value;
    const bool dwelling = type < config.is_housing.size() && config.is_housing[type] != 0;
    // An empty dwelling is not stoked: nobody lives there to stoke it.
    if (dwelling && inhabited[row] == 0) {
      continue;
    }
    const bool house = dwelling;
    // One degree below the threshold: the unit's m³ a degree-day.
    per_degree_day +=
        static_cast<double>(HeatingBurnToday(config, unit, config.heat_threshold_c - 1.0F));
    const float want = HeatingBurnToday(config, unit, outdoor);
    if (!(want > 0.0F)) {
      continue;
    }
    // STAGE 3c, THE DEADWOOD (heating §2; econ: the poor family's own fuel, straight into its
    // own house, free time only): a dwelling whose family has an adult at home brings one trip
    // a day while its buffer is under the full one - deadwood_m3_per_trip, whole within
    // deadwood_full_within_m of the nearest forest stand, falling linearly to 0 at
    // deadwood_zero_at_m. STUB: the trip's hours are not taken from anyone's day (the forage
    // net's form), so it costs no labour any instrument sees.
    if (dwelling && unit.heat_buffer_m3 < config.heat_buffer_m3 && adult_home[row] != 0) {
      const float brought = std::min(DeadwoodTripM3(config, world, unit.position),
                                     config.heat_buffer_m3 - unit.heat_buffer_m3);
      if (brought > 0.0F) {
        unit.heat_buffer_m3 += brought;
        heating.probe_deadwood_m3 += static_cast<double>(brought);
      }
    }
    // STAGE 2, THE REFILL (heating §1; econ: under heat_refill_below_m3 back to heat_buffer_m3,
    // from the first day the burn wants fuel): from the village's stores, in unit id order.
    // STUB: the firewood arrives the same day with no cart and no hand - its carrying is
    // logistics' (the cart's trip), not priced here; the probe counts what moved.
    if (unit.heat_buffer_m3 < config.heat_refill_below_m3) {
      const float wanted_m3 = config.heat_buffer_m3 - unit.heat_buffer_m3;
      const Grams got = TakeFirewood(config, world, row, GramsOfM3(config, wanted_m3));
      const float got_m3 = M3OfGrams(config, got);
      unit.heat_buffer_m3 += got_m3;
      heating.probe_refill_m3 += static_cast<double>(got_m3);
      ++heating.probe_refills;
      if (got_m3 + kM3Slack < wanted_m3) {
        ++heating.probe_refill_short;
      }
    }
    const float burnt = std::min(want, unit.heat_buffer_m3);
    unit.heat_buffer_m3 -= burnt;
    ++heating.probe_want_unit_days;
    heating.probe_burnt_m3 += static_cast<double>(burnt);
    const bool short_day = burnt < want;
    if (short_day) {
      ++heating.probe_empty_unit_days;
      heating.probe_short_m3 += static_cast<double>(want - burnt);
    }
    if (house) {
      ++heating.probe_want_house_days;
      if (short_day) {
        ++heating.probe_empty_house_days;
      }
    }
  }
  // The coming month and two (kDaysPerMonth days each), from tomorrow.
  double degrees_1m = 0.0;
  double degrees_2m = 0.0;
  for (std::uint32_t ahead = 1; ahead <= 2U * kDaysPerMonth; ++ahead) {
    const Season season = SeasonOfMonth(DateFromDay(world.calendar.day + ahead).month);
    const auto index = static_cast<std::size_t>(season);
    const double below =
        std::max(0.0, static_cast<double>(config.heat_threshold_c - config.season_mean_c[index]));
    degrees_2m += below;
    if (ahead <= kDaysPerMonth) {
      degrees_1m += below;
    }
  }
  heating.need_1m_m3 = static_cast<float>(per_degree_day * degrees_1m);
  heating.need_2m_m3 = static_cast<float>(per_degree_day * degrees_2m);
}

namespace {

/// STUB bands for the endurance offsets (econ named hardy / ordinary / weak-sick-old and their
/// offsets, not the bands): hardy at stamina+sportiness ≥ 120 of 200; weak under 60, or health
/// under 40, or 55 biological years and over.
constexpr float kHardyToughness = 120.0F;
constexpr float kWeakToughness = 60.0F;
constexpr float kWeakHealth = 40.0F;
constexpr float kOldYears = 55.0F;
constexpr std::uint64_t kColdHitSalt = 0x2261C01DULL;

}  // namespace

void RunResidentsCold(const HeatingConfig& config,
                      float life_speedup,
                      bool epoch_one,
                      WorldState& world) {
  HeatingState& heating = world.heating;
  const float outdoor = world.weather.air_temperature_celsius;
  const bool cold_day = outdoor < config.heat_threshold_c;
  const SimDay day = world.calendar.day;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    ResidentRow& resident = world.residents.rows[row];
    if (AwayInDistrict(resident, world.calendar.tick)) {
      continue;  // the district's roof
    }
    // HIS HOME TONIGHT: warm on a day at or above the threshold, or under a roof whose buffer
    // still holds fuel after today's burn; cold with no house or an empty stove.
    bool warm = !cold_day;
    if (cold_day) {
      const std::uint32_t family = FindRow(world.families, resident.family);
      const std::uint32_t house =
          family == kNoRow ? kNoRow : FindRow(world.units, world.families.rows[family].house);
      warm = house != kNoRow && world.units.rows[house].heat_buffer_m3 > 0.0F;
    }
    resident.cold =
        warm ? std::max(0.0F, resident.cold - config.cold_loss_night_warm)
             : std::min(config.cold_scale_max, resident.cold + config.cold_gain_night_home);
    // His thresholds, moved by his offsets.
    const float toughness = resident.stamina + resident.sportiness;
    const float age = BiologicalAgeYears(life_speedup, resident.birth_day, day);
    float offset = epoch_one ? config.cold_era1_offset : 0.0F;
    if (toughness < kWeakToughness || resident.health < kWeakHealth || age >= kOldYears) {
      offset += config.cold_weak_offset;
    } else if (toughness >= kHardyToughness) {
      offset += config.cold_hardy_offset;
    }
    const std::uint8_t was = resident.cold_stage;
    std::uint8_t stage = 0;
    if (resident.cold >= config.cold_frozen_at + offset) {
      stage = 2;
    } else if (resident.cold >= config.cold_cold_at + offset) {
      stage = 1;
    }
    resident.cold_stage = stage;
    // THE HEALTH HIT (boss: no disease in the first build): reaching «Замёрз», once an episode,
    // or a «Холодно» day.
    float chance_pct = 0.0F;
    if (stage == 2 && was != 2) {
      chance_pct = config.cold_frozen_sick_chance_pct;
    } else if (stage == 1) {
      chance_pct = config.cold_sickness_pct_day;
    }
    if (chance_pct > 0.0F &&
        CounterHashUnitFloat(
            world.world_seed, day, world.residents.row_ids[row].value, kColdHitSalt) *
                100.0F <
            chance_pct) {
      const float taken = std::min(resident.health, kColdHealthHitPoints);
      resident.health -= taken;
      SimEvent& hit = EmitEvent(world, EventKind::kColdHealthHit, EventSeverity::kNotable);
      hit.resident = world.residents.row_ids[row];
      hit.amount = static_cast<std::int64_t>(std::lround(taken));
      ++heating.probe_health_hits;
    }
    if (cold_day) {
      ++heating.probe_cold_season_days;
      heating.probe_cold_stage_days += stage == 1 ? 1U : 0U;
      heating.probe_frozen_stage_days += stage == 2 ? 1U : 0U;
    }
  }
}

}  // namespace core
