// The cold ladder of the kolkhoz herds (herd_cold.h).

#include "herd_cold.h"

#include <algorithm>
#include <cstdint>

#include "core_common/emit_event.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "herd_life.h"
#include "production_config.h"
#include "stock_ops.h"

namespace core {
namespace {

/// The herd's own unit, when it stands (level > 0); nullptr otherwise.
const UnitRow* HerdUnit(const WorldState& world, const HerdRow& herd) {
  if (herd.unit.value == kInvalidEntityIdValue) {
    return nullptr;
  }
  const std::uint32_t row = FindRow(world.units, herd.unit);
  if (row == kNoRow || world.units.rows[row].level == 0) {
    return nullptr;
  }
  return &world.units.rows[row];
}

/// The night of the day's weather: the mean less the day's swing (camera
/// design §4, «ночь = среднее − размах»; weather_of_day.cpp's own night).
float NightCelsius(const WorldState& world) {
  return world.weather.air_temperature_celsius - world.weather.temperature_swing_celsius;
}

}  // namespace

bool UnitIsWarmPlace(const ProductionConfig& config, const UnitRow& unit) {
  if (unit.level == 0) {
    return false;
  }
  if (unit.insulated != 0) {
    return true;
  }
  return unit.type.value < config.unit_types.size() &&
         config.unit_types[unit.type.value].WarmPlaceAt(unit.level);
}

std::uint16_t HeadsUnderRoof(const HerdRow& herd) {
  const std::uint16_t total = TotalHeads(herd);
  return total > herd.billeted_count ? static_cast<std::uint16_t>(total - herd.billeted_count)
                                     : std::uint16_t{0};
}

void CountColdNight(const ProductionConfig& config,
                    const LivestockDef& kind,
                    HerdRow& herd,
                    const WorldState& world) {
  const UnitRow* const unit = HerdUnit(world, herd);
  const bool freezes_anywhere = kind.freezes_in_cold_place != 0 || kind.freezes_in_warm_place != 0;
  if (herd.household_owned != 0 || unit == nullptr || HeadsUnderRoof(herd) == 0 ||
      !freezes_anywhere) {
    // Outside the metric: a family's own animals (the family keeps them), a
    // herd wholly on billet (the hosts keep them), a kind that never
    // freezes. Its count is nought and it stood nowhere cold.
    herd.cold_nights = 0;
    herd.cold_place_yesterday = 0;
    return;
  }
  const bool warm = UnitIsWarmPlace(config, *unit);
  if (!InColdSeason(config, world.calendar.date.month)) {
    // Out of the calendar's winter no night counts and none is carried: «1
    // марта счётчик обнуляется» (boss, billet thread [8]). Where the herd
    // stood is still written — it is the place, not the cold.
    herd.cold_nights = 0;
    herd.cold_place_yesterday = warm ? 0U : 1U;
    return;
  }
  if (warm && herd.cold_place_yesterday != 0) {
    herd.cold_nights = 0;  // «переезд в тёплое место обнуляет счётчик назавтра»
  }
  const bool freezes_here =
      warm ? kind.freezes_in_warm_place != 0 : kind.freezes_in_cold_place != 0;
  const float threshold = warm ? kind.cold_night_warm_place_c : kind.cold_night_cold_place_c;
  const float night = NightCelsius(world);
  float step = config.farming.cold_step_warm_night;
  if (freezes_here && night < threshold) {
    step = night < config.farming.still_frost_c ? config.farming.cold_step_still_frost
                                                : config.farming.cold_step_night;
  }
  constexpr float kCountCeiling = 255.0F;
  const float counted =
      std::clamp(static_cast<float>(herd.cold_nights) + step, 0.0F, kCountCeiling);
  herd.cold_nights = static_cast<std::uint8_t>(counted);
  herd.cold_place_yesterday = warm ? 0U : 1U;
}

bool HerdFreezing(const HerdRow& herd) {
  return herd.cold_nights >= 1 && HeadsUnderRoof(herd) > 0;
}

bool HerdFreezingToDeath(const ProductionConfig& config, const HerdRow& herd) {
  return HerdFreezing(herd) &&
         static_cast<float>(herd.cold_nights) >= config.farming.freezing_counter;
}

float ColdProduceFactor(const LivestockDef& kind, const HerdRow& herd) {
  const auto total = static_cast<float>(TotalHeads(herd));
  if (!HerdFreezing(herd) || !(total > 0.0F)) {
    return 1.0F;
  }
  const float cold_share = static_cast<float>(HeadsUnderRoof(herd)) / total;
  return 1.0F - (cold_share * (1.0F - kind.freezing_produce_factor));
}

float ColdDraughtFactor(const ProductionConfig& config, const WorldState& world) {
  float heads = 0.0F;
  float lost = 0.0F;
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned != 0 || herd.kind.value != config.horse_kind.value ||
        herd.kind.value >= config.livestock.size()) {
      continue;
    }
    const float factor = config.livestock[herd.kind.value].freezing_draught_factor;
    heads += static_cast<float>(TotalHeads(herd));
    if (HerdFreezing(herd)) {
      lost += static_cast<float>(HeadsUnderRoof(herd)) * (1.0F - factor);
    }
  }
  return heads > 0.0F ? 1.0F - (lost / heads) : 1.0F;
}

void RunFrostDeaths(const ProductionConfig& config,
                    const LivestockDef& /*kind*/,
                    HerdRow& herd,
                    HerdId herd_id,
                    WorldState& world,
                    YearLedger& book) {
  if (!HerdFreezingToDeath(config, herd) || herd.adult_count == 0) {
    return;
  }
  // THE ADULTS UNDER THE COLD ROOF, by the herd's own share under it: the
  // billet holds its heads warm, and the counter is the herd's, not a head's.
  const auto total = static_cast<float>(TotalHeads(herd));
  const float adults_in_cold =
      static_cast<float>(herd.adult_count) * static_cast<float>(HeadsUnderRoof(herd)) / total;
  const std::uint16_t owed =
      DrawFlow(herd.frost_progress, adults_in_cold * config.farming.freezing_loss_share_day);
  const std::uint16_t before = herd.adult_count;
  const std::uint16_t gone = TakeHeads(herd.adult_count, owed);
  if (gone == 0) {
    return;
  }
  // The age total and the sires follow the loss in proportion, as hunger's
  // (herd_life.cpp, RunHungerDeaths): the frost takes no age and no sex.
  const float mean = herd.adult_age_game_years_total / static_cast<float>(before);
  herd.adult_age_game_years_total =
      herd.adult_count == 0 ? 0.0F
                            : herd.adult_age_game_years_total - (static_cast<float>(gone) * mean);
  herd.adult_male_count = MalesAfterLoss(herd.adult_male_count, before, gone);
  AddLedgerHeads(book.herd_frozen, herd.kind, gone);
  SimEvent& event = EmitEvent(world, EventKind::kHerdFroze);
  event.herd = herd_id;
  event.amount = static_cast<std::int64_t>(gone);
}

bool InColdSeason(const ProductionConfig& config, Month month) {
  const auto index = static_cast<std::uint8_t>(month);
  const std::uint8_t first = config.farming.cold_first_month;
  const std::uint8_t last = config.farming.cold_last_month;
  // A span that wraps the year's turn (December..February) is the two ends.
  return first <= last ? index >= first && index <= last : index >= first || index <= last;
}

std::uint32_t DaysToColdSeason(const ProductionConfig& config, std::uint32_t day_of_year) {
  const std::uint32_t today = day_of_year % kDaysPerYear;
  if (InColdSeason(config, static_cast<Month>(today / kDaysPerMonth))) {
    return 0;
  }
  const std::uint32_t opens =
      static_cast<std::uint32_t>(config.farming.cold_first_month) * kDaysPerMonth;
  return (opens + kDaysPerYear - today) % kDaysPerYear;
}

namespace {

/// kInsulateStraw while the stores hold one livestock insulation's straw
/// (boss, 2026-10-01); kNone otherwise — the billet and the knife are the
/// lamp's text, not a move.
AlarmAdvice StrawIfHeld(const ProductionConfig& config, const WorldState& world) {
  if (config.straw_resource.value == kInvalidDefIdValue) {
    return AlarmAdvice::kNone;
  }
  constexpr float kKilogramsPerTonne = 1000.0F;
  const Grams price =
      KilogramsToGrams(config.farming.insulation_livestock_straw_t * kKilogramsPerTonne);
  return price > 0 && TakeableGrams(world, config, config.straw_resource) >= price
             ? AlarmAdvice::kInsulateStraw
             : AlarmAdvice::kNone;
}

}  // namespace

void CollectHerdColdAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms) {
  // THE RED: every kolkhoz herd freezing today.
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const HerdRow& herd = world.herds.rows[row];
    if (herd.household_owned != 0 || !HerdFreezing(herd)) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kHerdFreezing;
    alarm.herd = world.herds.row_ids[row];
    alarm.amount = HeadsUnderRoof(herd);
    alarm.advice = StrawIfHeld(config, world);
    alarms.push_back(alarm);
  }
  // THE YELLOW, in the autumn: the heads that will stand under a cold roof
  // when the calendar's winter opens — the herd, up to its roof's room (what
  // the roof does not hold stands billeted, warm). Until 0.37.69 the billet
  // kept its places first in a frost month and the yellow counted what it
  // would not keep; «в мороз постой первым» went with the billet of the
  // start (the human's «Постой отменяем», 2026-10-01).
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  const std::uint32_t day_of_year = world.calendar.day % kDaysPerYear;
  const std::uint32_t days = DaysToColdSeason(config, day_of_year);
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const HerdRow& herd = world.herds.rows[row];
    const UnitRow* const unit = HerdUnit(world, herd);
    if (herd.household_owned != 0 || unit == nullptr ||
        herd.kind.value >= config.livestock.size() || UnitIsWarmPlace(config, *unit) ||
        unit->type.value >= config.unit_types.size()) {
      continue;
    }
    const LivestockDef& kind = config.livestock[herd.kind.value];
    if (kind.freezes_in_cold_place == 0) {
      continue;
    }
    const auto heads = static_cast<float>(TotalHeads(herd));
    const UnitTypeDef& type = config.unit_types[unit->type.value];
    const float cold = std::min(heads, type.LivestockCapacityHeadAt(unit->level));
    const bool autumn = month > config.farming.pasture_to_month && days > 0;
    if (!autumn || !(cold >= 1.0F)) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kHerdColdAhead;
    alarm.herd = world.herds.row_ids[row];
    alarm.amount = static_cast<std::int64_t>(cold);
    alarm.days_ahead = static_cast<std::uint16_t>(days);
    alarm.advice = type.WarmPlaceAt(static_cast<std::uint8_t>(unit->level + 1U))
                       ? AlarmAdvice::kWarmYard
                       : AlarmAdvice::kInsulateStraw;
    alarm.lamp = 0;
    alarms.push_back(alarm);
  }
}

}  // namespace core
