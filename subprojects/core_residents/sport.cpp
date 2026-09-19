// The sports field and sportiness (core_residents/sport.h).

#include "sport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "core_tables/tables.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 17> kSportWorldParamKeys = {"sport_field_radius_m",
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
                                                                   "sportiness_alcohol_loss",
                                                                   "reading_hut_radius_m",
                                                                   "reading_hut_alcohol_loss",
                                                                   "talk_months"};

/// Past this a talk's length is a typo: ten years of evenings.
constexpr float kTalkMonthsMax = 120.0F;

/// Someone holds a post at this unit — for the hut, its librarian, the one
/// post of its staff (unit_staff.csv).
bool IsStaffed(const WorldState& world, UnitId unit) {
  return std::ranges::any_of(world.residents.rows, [unit](const ResidentRow& resident) {
    return resident.post.unit.value == unit.value;
  });
}

/// A unit of `type`, step 1 or more, within `radius_m` of the man's home;
/// with `staffed`, only one somebody holds a post at.
bool BuiltWithinReach(const WorldState& world,
                      const ResidentRow& person,
                      UnitTypeId type,
                      float radius_m,
                      bool staffed) {
  if (type.value == kInvalidDefIdValue) {
    return false;
  }
  Vec2 home;
  if (!HomePositionOf(world, person.family, home)) {
    return false;
  }
  const float radius_sq = radius_m * radius_m;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    // A ruin is no field and no hut (boss seq 176): a talk accepted with
    // nowhere to go, and a goer counted at a dead stadium, punished the
    // other way round.
    if (unit.type.value != type.value || unit.level == 0 || unit.dead != 0) {
      continue;
    }
    const float dx = unit.position.x - home.x;
    const float dy = unit.position.y - home.y;
    if ((dx * dx) + (dy * dy) <= radius_sq &&
        (!staffed || IsStaffed(world, world.units.row_ids[row]))) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::span<const std::string_view> SportWorldParamKeys() {
  return kSportWorldParamKeys;
}

bool ParseSportConfig(const ITableSet& tables, SportConfig& config, std::string& error) {
  if (const ITable* unit_types = tables.FindTable("unit_types")) {
    config.stadium_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("stadium"));
    config.culture_house_type =
        DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("culture_house"));
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
      {.key = kSportWorldParamKeys[14],
       .value = &config.hut_radius_m,
       .range = {.low = 0.0F, .high = 20000.0F}},
      {.key = kSportWorldParamKeys[15], .value = &config.hut_alcohol_loss, .range = points},
      {.key = kSportWorldParamKeys[16],
       .value = &config.talk_months,
       .range = {.low = 0.0F, .high = kTalkMonthsMax}},
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
  return Goes(config, person, age_years, world.calendar.day) &&
         BuiltWithinReach(world, person, config.stadium_type, config.field_radius_m, false);
}

bool ReachesTheHut(const SportConfig& config, const WorldState& world, const ResidentRow& person) {
  return BuiltWithinReach(world, person, config.culture_house_type, config.hut_radius_m, true);
}

bool GoesBySelf(const SportConfig& config, const ResidentRow& person, float age_years) {
  return age_years < config.goer_age_max && person.alcoholism <= config.goer_alcohol_max;
}

bool Goes(const SportConfig& config, const ResidentRow& person, float age_years, SimDay day) {
  return GoesBySelf(config, person, age_years) ||
         (person.talk_until_day != 0 && day <= person.talk_until_day);
}

std::uint32_t TalkSeasonOf(SimDay day) {
  // The calendar's own seasons (calendar.h, SeasonOfMonth), counted on instead of
  // wrapped: December joins the January after it.
  return ((day / kDaysPerMonth) + 1U) / kMonthsPerSeason;
}

void ConsumeTalkOrders(const SportConfig& config,
                       float adult_from_years,
                       float life_speedup,
                       WorldState& current) {
  const SimDay day = current.calendar.day;
  const std::uint32_t season = TalkSeasonOf(day) + 1U;
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending || order.kind != OrderKind::kTalkToSport) {
      continue;
    }
    const std::uint32_t row = FindRow(current.residents, order.resident);
    OrderRefusal refusal = OrderRefusal::kNone;
    if (row == kNoRow) {
      refusal = OrderRefusal::kNoSuchSubject;
    } else {
      ResidentRow& man = current.residents.rows[row];
      if (man.sex != Sex::kMale ||
          BiologicalAgeYears(life_speedup, man.birth_day, day) < adult_from_years) {
        refusal = OrderRefusal::kNotEligible;
      } else if (man.talk_until_day != 0 && day <= man.talk_until_day) {
        refusal = OrderRefusal::kConflictsWithActive;
      } else if (current.chairman.last_talk_season == season) {
        refusal = OrderRefusal::kOncePerSeason;
      } else if (!BuiltWithinReach(
                     current, man, config.stadium_type, config.field_radius_m, false) &&
                 !ReachesTheHut(config, current, man)) {
        refusal = OrderRefusal::kNowhereToGo;
      } else {
        man.talk_until_day =
            day + static_cast<SimDay>(std::lround(config.talk_months * kDaysPerMonth));
        current.chairman.last_talk_season = season;
      }
    }
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }
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
