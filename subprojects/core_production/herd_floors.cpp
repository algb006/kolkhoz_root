#include "herd_floors.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"

namespace core {
namespace {

/// What a count rounded up may stand above a whole number by and still be
/// that number: the floats' own error, far below any head.
constexpr double kHair = 1.0e-4;

/// The heads standing in the kolkhoz herds of one class.
struct ClassHeads {
  std::int64_t adults = 0;
  std::int64_t juveniles = 0;
};

Grams DenseAt(const ResourceAmounts& amounts, std::size_t index) {
  return index < amounts.size() ? amounts[index] : 0;
}

/// The least `count` in (low, high] with which `short_with(count)` is false,
/// given that it is true at `low` and false at `high`.
template <typename ShortWith>
std::int64_t LeastNotShort(std::int64_t low, std::int64_t high, ShortWith short_with) {
  while (high - low > 1) {
    const std::int64_t middle = low + ((high - low) / 2);
    (short_with(middle) ? low : high) = middle;
  }
  return high;
}

}  // namespace

LivestockKindId MilkKind(const ProductionConfig& config) {
  LivestockKindId best;
  float most = 0.0F;
  for (std::size_t index = 0; index < config.livestock.size(); ++index) {
    if (config.livestock[index].milk_l_per_year > most) {
      most = config.livestock[index].milk_l_per_year;
      best = DefIdFromIndex<LivestockKindIdTag>(index);
    }
  }
  return best;
}

HerdFloors HerdFloorsOf(const ProductionConfig& config, const WorldState& world) {
  HerdFloors floors;
  // THE PLOUGHING: the plan's own base of worked hectares when it has one —
  // a floor off all the arable ate the cows' hay (hay_answer.h, where this
  // rule stood until 0.37.142).
  double base_ha = static_cast<double>(world.plan.worked_ha_last_year);
  if (!(base_ha > 0.0)) {
    for (const FieldRow& field : world.fields.rows) {
      base_ha += field.kind == LandKind::kArable ? static_cast<double>(field.area_ga) : 0.0;
    }
  }
  const double window = static_cast<double>(config.farming.plough_window_days);
  if (window > 0.0) {
    const double team_days = base_ha * static_cast<double>(config.farming.plow_days_per_ha);
    // Rounded up off floats: a hair above a whole number is that number,
    // not the next — 5.6 ha x 10/7 over 8 days is ONE team, and came out
    // 1.0000001 and two in this function's first check.
    floors.plough_teams = static_cast<std::int64_t>(std::ceil((team_days / window) - kHair));
    floors.horses_kept = static_cast<std::int64_t>(
        std::ceil((static_cast<double>(floors.plough_teams) *
                   static_cast<double>(config.farming.plough_floor_margin)) -
                  kHair));
  }
  // THE MILK: the highest position ever named, not this year's — this year's
  // follows the herd down, and a floor read off it followed it too.
  const std::size_t milk = config.milk_resource.value;
  const LivestockKindId cow = MilkKind(config);
  // BEFORE THE DISTRICT HAS SPOKEN AT ALL the floor is NOT KNOWN, and that is
  // not «no floor»: every cow is kept (0.37.143; host, host-boss-pin-0-37-
  // 133-2026-10-02 [43]). The highest positions are written at the plan's
  // first announcement; until then they are empty. On 0.37.142 an empty
  // vector read as a position of nought, the floor was nought, and the lamp
  // of the first morning named all 39 cows of a poor start above it.
  if (world.plan.highest_due.empty() && cow.value != kInvalidDefIdValue) {
    floors.cows_kept = std::numeric_limits<std::int64_t>::max();
    return floors;
  }
  const Grams due = std::max({DenseAt(world.plan.highest_due, milk),
                              DenseAt(world.plan.due, milk),
                              DenseAt(world.ledger.closed.plan_due, milk)});
  if (due <= 0 || cow.value == kInvalidDefIdValue) {
    return floors;
  }
  const Grams cow_days = DenseAt(world.ledger.closed.adult_head_days, cow.value);
  const Grams milked = DenseAt(world.ledger.closed.herd_produce, milk);
  if (cow_days <= 0 || milked <= 0) {
    floors.cows_kept = std::numeric_limits<std::int64_t>::max();
    return floors;
  }
  const double yield_a_cow = static_cast<double>(milked) * static_cast<double>(kDaysPerYear) /
                             static_cast<double>(cow_days);
  floors.cows_kept =
      static_cast<std::int64_t>(std::ceil((static_cast<double>(config.farming.milk_floor_margin) *
                                           static_cast<double>(due) / yield_a_cow) -
                                          kHair));
  return floors;
}

HandOverAdvice LeastHeadsToHandOver(const ProductionConfig& config, const WorldState& world) {
  HandOverAdvice advice;
  const auto short_without = [&config, &world](const HeadsHandedOver& handed) {
    return ForecastHerdFeed(config, world, false, FeedHorizon::kNearestScythes, nullptr, &handed)
        .short_ahead;
  };
  HeadsHandedOver handed;
  if (!short_without(handed)) {
    return advice;
  }
  const LivestockKindId cow = MilkKind(config);
  ClassHeads horses;
  ClassHeads cows;
  ClassHeads other;
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
      continue;
    }
    ClassHeads& heads = herd.kind.value == config.horse_kind.value
                            ? horses
                            : (herd.kind.value == cow.value ? cows : other);
    heads.adults += herd.adult_count;
    heads.juveniles += herd.juvenile_count;
  }
  const HerdFloors floors = HerdFloorsOf(config, world);
  const std::int64_t horses_above = std::max<std::int64_t>(0, horses.adults - floors.horses_kept);
  const std::int64_t cows_above = std::max<std::int64_t>(0, cows.adults - floors.cows_kept);
  // One class of `handed` taken from `from` (short there) up to `upto`:
  // true, and the class left at the least count that feeds the rest, when
  // `upto` does; false, and the class left at `upto`, when it does not.
  const auto step = [&short_without, &handed](std::int64_t HeadsHandedOver::* count,
                                              std::int64_t upto) {
    const std::int64_t from = handed.*count;
    if (upto <= from) {
      return false;
    }
    handed.*count = upto;
    if (short_without(handed)) {
      return false;
    }
    handed.*count = LeastNotShort(from, upto, [&](std::int64_t middle) {
      HeadsHandedOver probe = handed;
      probe.*count = middle;
      return short_without(probe);
    });
    return true;
  };
  // ABOVE THE FLOORS: the horses, then the cows.
  bool fed = step(&HeadsHandedOver::horses, horses_above);
  advice.horses = handed.horses;
  if (!fed) {
    fed = step(&HeadsHandedOver::cows, cows_above);
    advice.stock = handed.cows;
  }
  if (fed) {
    return advice;
  }
  // BELOW THEM, named apart: the kinds with no floor, the cows, the horses.
  fed = step(&HeadsHandedOver::other, other.adults + other.juveniles) ||
        step(&HeadsHandedOver::cows, cows.adults + cows.juveniles) ||
        step(&HeadsHandedOver::horses, horses.adults + horses.juveniles);
  advice.below_floor_stock = handed.other + (handed.cows - advice.stock);
  advice.below_floor_horses = handed.horses - advice.horses;
  return advice;
}

}  // namespace core
