// The sports field and sportiness (core_residents/sport.h).

#include "sport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_common/geometry.h"
#include "core_common/work_seam.h"
#include "core_tables/tables.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 14> kSportWorldParamKeys = {"sport_field_radius_m",
                                                                   "sport_open_temp_c",
                                                                   "sport_open_days_min",
                                                                   "sport_goer_age_max",
                                                                   "sport_goer_alcohol_max",
                                                                   "sport_field_alcohol_loss",
                                                                   "sportiness_gain_goer",
                                                                   "sportiness_gain_drinker_share",
                                                                   "sportiness_drinker_above",
                                                                   "sportiness_decay",
                                                                   "sportiness_decay_old_from",
                                                                   "sportiness_decay_old_extra",
                                                                   "sportiness_sober_from",
                                                                   "sportiness_alcohol_loss"};

}  // namespace

std::span<const std::string_view> SportWorldParamKeys() {
  return kSportWorldParamKeys;
}

bool ParseSportConfig(const ITableSet& tables, SportConfig& config, std::string& error) {
  if (const ITable* unit_types = tables.FindTable("unit_types")) {
    config.stadium_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("stadium"));
  }
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  const Range points{.low = 0.0F, .high = 100.0F};
  const std::array<ScalarKnob, kSportWorldParamKeys.size()> knobs = {{
      {.key = kSportWorldParamKeys[0],
       .value = &config.field_radius_m,
       .range = {.low = 0.0F, .high = 20000.0F}},
      {.key = kSportWorldParamKeys[1],
       .value = &config.open_temp_c,
       .range = {.low = -50.0F, .high = 50.0F}},
      {.key = kSportWorldParamKeys[2],
       .value = &config.open_days_min,
       .range = {.low = 0.0F, .high = static_cast<float>(kDaysPerMonth)}},
      {.key = kSportWorldParamKeys[3],
       .value = &config.goer_age_max,
       .range = {.low = 0.0F, .high = 150.0F}},
      {.key = kSportWorldParamKeys[4], .value = &config.goer_alcohol_max, .range = points},
      {.key = kSportWorldParamKeys[5], .value = &config.field_alcohol_loss, .range = points},
      {.key = kSportWorldParamKeys[6], .value = &config.gain_goer, .range = points},
      {.key = kSportWorldParamKeys[7],
       .value = &config.gain_drinker_share,
       .range = {.low = 0.0F, .high = 1.0F}},
      {.key = kSportWorldParamKeys[8], .value = &config.drinker_above, .range = points},
      {.key = kSportWorldParamKeys[9], .value = &config.decay, .range = points},
      {.key = kSportWorldParamKeys[10],
       .value = &config.decay_old_from,
       .range = {.low = 0.0F, .high = 150.0F}},
      {.key = kSportWorldParamKeys[11], .value = &config.decay_old_extra, .range = points},
      {.key = kSportWorldParamKeys[12], .value = &config.sober_from, .range = points},
      {.key = kSportWorldParamKeys[13], .value = &config.sportiness_alcohol_loss, .range = points},
  }};
  return ReadKnobs(*world, "world_params", knobs, error);
}

void CountSportDay(const SportConfig& config, WorldState& current) {
  SportMonth& month = current.sport_month;
  const WeatherState& weather = current.weather;
  const bool open = weather.air_temperature_celsius >= config.open_temp_c &&
                    weather.precipitation == Precipitation::kNone && month.downpour_yesterday == 0;
  if (open && month.open_days < 255U) {
    ++month.open_days;
  }
  month.downpour_yesterday = weather.heavy_hours > 0 ? 1U : 0U;
}

bool SportMonthCounted(const SportConfig& config, const WorldState& world) {
  return static_cast<float>(world.sport_month.open_days) >= config.open_days_min;
}

bool GoesToTheField(const SportConfig& config,
                    const WorldState& world,
                    const ResidentRow& person,
                    float age_years) {
  if (age_years >= config.goer_age_max || person.alcoholism > config.goer_alcohol_max ||
      config.stadium_type.value == kInvalidDefIdValue) {
    return false;
  }
  Vec2 home;
  if (!HomePositionOf(world, person.family, home)) {
    return false;
  }
  const float radius_sq = config.field_radius_m * config.field_radius_m;
  return std::ranges::any_of(world.units.rows, [&](const UnitRow& unit) {
    if (unit.type.value != config.stadium_type.value || unit.level == 0) {
      return false;
    }
    const float dx = unit.position.x - home.x;
    const float dy = unit.position.y - home.y;
    return (dx * dx) + (dy * dy) <= radius_sq;
  });
}

void TurnSportiness(const SportConfig& config, bool went, float age_years, ResidentRow& person) {
  float change = 0.0F;
  if (went) {
    change = config.gain_goer *
             (person.alcoholism > config.drinker_above ? config.gain_drinker_share : 1.0F);
  } else {
    change = -config.decay;
  }
  if (age_years > config.decay_old_from) {
    change -= config.decay_old_extra;
  }
  person.sportiness = std::clamp(person.sportiness + change, kMetricMin, kMetricMax);
}

}  // namespace core
