// The herd plan's window line (herd_plan.h).

#include "herd_plan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/herd_state.h"
#include "core_common/state_table_ops.h"
#include "herd_floors.h"
#include "herd_forecast.h"
#include "herd_life.h"
#include "livestock_homes.h"

namespace core {

void ScaleHerdHeads(HerdRow& herd, std::uint16_t heads) {
  const std::uint16_t total = TotalHeads(herd);
  if (total == 0) {
    herd.adult_count = heads;
    return;
  }
  const double share = static_cast<double>(heads) / static_cast<double>(total);
  const auto scaled = [share](std::uint16_t count) {
    return static_cast<std::uint16_t>(std::lround(static_cast<double>(count) * share));
  };
  const std::uint16_t adults = std::min<std::uint16_t>(scaled(herd.adult_count), heads);
  const std::uint16_t juveniles = std::min<std::uint16_t>(
      scaled(herd.juvenile_count), static_cast<std::uint16_t>(heads - adults));
  const std::uint16_t males =
      herd.adult_male_count > 0 && adults > 0
          ? std::clamp<std::uint16_t>(scaled(herd.adult_male_count), 1, adults)
          : std::uint16_t{0};
  herd.adult_count = adults;
  herd.adult_male_count = males;
  herd.juvenile_count = juveniles;
  herd.newborn_count = static_cast<std::uint16_t>(heads - adults - juveniles);
}

namespace {

/// Whether the hay feeds every kolkhoz herd to the lamp's horizon with the
/// herd in `row` held at `heads` and its births stopping there.
bool FedAt(const ProductionConfig& config,
           WorldState& what_if,
           std::uint32_t row,
           std::uint16_t heads) {
  HerdRow& herd = what_if.herds.rows[row];
  ScaleHerdHeads(herd, heads);
  herd.plan_heads = heads;
  return !ForecastHerdFeed(config, what_if, false, FeedHorizon::kWindowByNorm).short_ahead;
}

/// The most heads of the herd in `row`, up to `top`, the hay feeds with the
/// rest of `what_if` as it stands - by halving; 0 when it is short even at
/// nought (the others alone go short).
std::uint16_t HayFor(const ProductionConfig& config,
                     WorldState& what_if,
                     std::uint32_t row,
                     std::uint16_t top) {
  if (FedAt(config, what_if, row, top)) {
    return top;
  }
  std::uint16_t fed = 0;
  if (!FedAt(config, what_if, row, 0)) {
    return 0;
  }
  std::uint16_t hungry = top;
  while (hungry > fed + 1U) {
    const auto middle = static_cast<std::uint16_t>((fed + hungry) / 2U);
    (FedAt(config, what_if, row, middle) ? fed : hungry) = middle;
  }
  return fed;
}

}  // namespace

HerdPlanLine HerdPlanLineOf(const ProductionConfig& config,
                            const WorldState& world,
                            std::uint32_t herd_row) {
  HerdPlanLine line;
  if (herd_row >= world.herds.rows.size()) {
    return line;
  }
  const HerdRow& herd = world.herds.rows[herd_row];
  line.herd = world.herds.row_ids[herd_row];
  line.kind = herd.kind;
  line.heads = TotalHeads(herd);
  line.places = HerdPlaces(config, world, herd_row);
  line.plan_set = herd.plan_heads != kNoHerdPlan;
  line.plan = line.plan_set ? herd.plan_heads : line.places;
  const HerdFloors floors = HerdFloorsOf(config, world);
  const LivestockKindId milk_kind = MilkKind(config);
  const auto floor_of = [&config, &floors, milk_kind](const HerdRow& of) -> std::int64_t {
    return of.kind.value == milk_kind.value           ? floors.cows_kept
           : of.kind.value == config.horse_kind.value ? floors.horses_kept
                                                      : -1;  // a kind with no floor
  };
  // The search runs to the larger of the places and the heads: the window
  // asks what the hay would feed, and the places cap the plan, not the hay.
  const std::uint16_t top = std::max(line.places, line.heads);
  // «СЕНА НА»: every other kolkhoz herd at its own plan, then the halving.
  WorldState what_if = world;
  for (std::uint32_t row = 0; row < what_if.herds.rows.size(); ++row) {
    HerdRow& other = what_if.herds.rows[row];
    if (row != herd_row && other.household_owned == 0 && other.plan_heads != kNoHerdPlan &&
        TotalHeads(other) > other.plan_heads) {
      ScaleHerdHeads(other, other.plan_heads);
    }
  }
  line.hay_for = HayFor(config, what_if, herd_row, top);
  line.hay_for_known = true;
  // AND WITH THE OTHERS AT THEIR FLOORS (econ, core-boss-1289 [10] (2): «the
  // hay to the floors of both herds first, then to the horses»): the milk
  // kind's and the horses' other herds at their floor, every other kind at
  // its plan. The run's bot sizes the horses on it; a what-if, no plan set.
  WorldState at_floors = world;
  for (std::uint32_t row = 0; row < at_floors.herds.rows.size(); ++row) {
    HerdRow& other = at_floors.herds.rows[row];
    if (row == herd_row || other.household_owned != 0) {
      continue;
    }
    const std::int64_t floor = floor_of(other);
    const std::int64_t cap = floor >= 0                        ? floor
                             : other.plan_heads != kNoHerdPlan ? other.plan_heads
                                                               : TotalHeads(other);
    if (TotalHeads(other) > cap) {
      ScaleHerdHeads(other, static_cast<std::uint16_t>(cap));
    }
  }
  line.hay_for_others_at_floor = HayFor(config, at_floors, herd_row, top);
  // «ПО НОРМЕ»: year 1 before its cut opens - the cut is econ's norm formula
  // then (FeedHorizon::kWindowByNorm, NormCutYearOne; boss, core-boss-1289
  // [9] 4), not a cut of the village's own. From the cut's second dry working
  // day the pace reads the village's own mowing.
  line.hay_for_by_norm =
      world.calendar.day < kDaysPerYear &&
      world.calendar.day < static_cast<SimDay>(config.farming.meadow_cut_month) * kDaysPerMonth;
  // THE THREE WARNINGS (the words are rpg's).
  const bool milk = herd.kind.value == milk_kind.value;
  const bool horse = herd.kind.value == config.horse_kind.value;
  const std::int64_t floor = std::max<std::int64_t>(floor_of(herd), 0);
  line.floor_heads =
      static_cast<std::uint16_t>(std::clamp<std::int64_t>(floor, 0, kNoHerdPlan - 1));
  line.below_milk_floor = milk && static_cast<std::int64_t>(line.plan) < floor;
  line.below_teams = horse && static_cast<std::int64_t>(line.plan) < floor;
  line.no_births = line.plan < 2U;
  return line;
}

}  // namespace core
