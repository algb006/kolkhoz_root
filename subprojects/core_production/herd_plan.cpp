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
  return !ForecastHerdFeed(config, what_if, false, FeedHorizon::kNearestScythes).short_ahead;
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
  // «СЕНА НА»: every other kolkhoz herd at its own plan, then the halving.
  WorldState what_if = world;
  for (std::uint32_t row = 0; row < what_if.herds.rows.size(); ++row) {
    HerdRow& other = what_if.herds.rows[row];
    if (row != herd_row && other.household_owned == 0 && other.plan_heads != kNoHerdPlan &&
        TotalHeads(other) > other.plan_heads) {
      ScaleHerdHeads(other, other.plan_heads);
    }
  }
  // The search runs to the larger of the places and the heads: the window
  // asks what the hay would feed, and the places cap the plan, not the hay.
  const std::uint16_t top = std::max(line.places, line.heads);
  std::uint16_t fed = 0;
  if (FedAt(config, what_if, herd_row, top)) {
    fed = top;
  } else if (FedAt(config, what_if, herd_row, 0)) {
    // fed at 0, hungry at top: the largest fed count between them.
    std::uint16_t hungry = top;
    while (hungry > fed + 1U) {
      const auto middle = static_cast<std::uint16_t>((fed + hungry) / 2U);
      (FedAt(config, what_if, herd_row, middle) ? fed : hungry) = middle;
    }
  }
  // Short even at nought: the others' plans alone go short, nothing is left.
  line.hay_for = fed;
  line.hay_for_known = true;
  // «ПО НОРМЕ»: year 1 before its cut closes - the forecast reads the
  // mowing's norm then, not a cut of the village's own (herd_forecast.h,
  // «YEAR 1, WITH NO BOOK»). STUB: econ's formula of the first year.
  const HerdFeedForecast today =
      ForecastHerdFeed(config, world, false, FeedHorizon::kNearestScythes);
  line.hay_for_by_norm =
      world.calendar.day < kDaysPerYear && world.calendar.day <= today.cut_end_day;
  // THE THREE WARNINGS (the words are rpg's).
  const HerdFloors floors = HerdFloorsOf(config, world);
  const bool milk = herd.kind.value == MilkKind(config).value;
  const bool horse = herd.kind.value == config.horse_kind.value;
  const std::int64_t floor = milk ? floors.cows_kept : horse ? floors.horses_kept : 0;
  line.floor_heads =
      static_cast<std::uint16_t>(std::clamp<std::int64_t>(floor, 0, kNoHerdPlan - 1));
  line.below_milk_floor = milk && static_cast<std::int64_t>(line.plan) < floor;
  line.below_teams = horse && static_cast<std::int64_t>(line.plan) < floor;
  line.no_births = line.plan < 2U;
  return line;
}

}  // namespace core
