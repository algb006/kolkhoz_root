// The lamp «В буртах сгнило N т» (#1005, #1068): the clamps' rot over the
// last year, read off the window SpoilStores writes, against the long answer
// a vegetable store is.

#include "clamp_rot_lamp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/clamp_rot_state.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"

namespace core {

namespace {

/// May, 0-based: the lamp's first month. Econ's (§4c, revised from August):
/// the summer before the second harvest goes into the clamps, against a
/// store of 140 man-days.
constexpr std::uint32_t kClampRotFirstMonth = 4;

/// The 0-based month the potato's harvest window opens, or kMonthsPerYear
/// when the tables grow no potato.
std::uint32_t PotatoHarvestMonth(const ProductionConfig& config) {
  std::uint32_t month = kMonthsPerYear;
  for (const CropDef& crop : config.crops) {
    if (crop.resource.value == config.potato_resource.value) {
      month = std::min<std::uint32_t>(month, crop.harvest_from_month);
    }
  }
  return month;
}

}  // namespace

void CollectClampRotAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms) {
  if (config.potato_resource.value == kInvalidDefIdValue ||
      config.vegetable_store_type.value == kInvalidDefIdValue) {
    return;
  }
  const std::uint32_t harvest_day = PotatoHarvestMonth(config) * kDaysPerMonth;
  const std::uint32_t day_of_year = world.calendar.day % kDaysPerYear;
  if (day_of_year < kClampRotFirstMonth * kDaysPerMonth || day_of_year >= harvest_day) {
    return;
  }
  // A STORE IN HAND IN ANY STATE puts it out: standing, a site, works open -
  // the decision is made.
  const bool store_in_hand = std::ranges::any_of(world.units.rows, [&config](const UnitRow& unit) {
    return unit.type.value == config.vegetable_store_type.value && unit.dead == 0;
  });
  if (store_in_hand) {
    return;
  }
  const Grams rotted = ClampRotOfYear(world.clamp_rot, world.calendar.day);
  const auto threshold = static_cast<Grams>(std::llround(
      static_cast<double>(config.farming.clamp_rot_lamp_t) * static_cast<double>(kGramsPerTonne)));
  if (rotted <= 0 || rotted < threshold) {
    return;
  }
  Alarm alarm;
  alarm.kind = AlarmKind::kClampRot;
  alarm.resource = config.potato_resource;
  alarm.amount = rotted;
  alarm.lamp = 1;
  alarm.days_to_loss =
      DaysToLossOf(static_cast<std::int64_t>(harvest_day) - static_cast<std::int64_t>(day_of_year));
  alarm.advice = AlarmAdvice::kBuildStore;
  alarm.advice_unit_type = config.vegetable_store_type;
  alarms.push_back(alarm);
}

}  // namespace core
