#include "herd_forecast.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "district_limit.h"
#include "herd_life.h"
#include "herd_system.h"
#include "night_pasture.h"
#include "stock_ops.h"

namespace core {
namespace {

/// A feed is out when its store has less than this, kilograms.
constexpr float kOutKg = 1.0e-3F;

/// A herd is fed when what it got is within this of its need, feed units.
constexpr float kFedSlackUnits = 0.001F;

/// One kolkhoz herd as the forecast carries it: fractional heads, grown and
/// born by the herd day's own numbers.
struct ProjectedHerd {
  HerdId id;
  LivestockKindId kind;
  float adults = 0.0F;
  float juveniles = 0.0F;
  float newborns = 0.0F;
  /// Heads a day A FEMALE brings while the calving band stands and the gates
  /// hold; 0 when any gate is shut (herd_life.cpp, RunBirths). The gates are
  /// today's: a herd billeted or hungry today calves in no forecast day.
  float births_per_female_band_day = 0.0F;
  /// The adult males (a sexed kind): the females are the rest, and the young
  /// grown up in the forecast calve with them (static review of 0.37.57 — a
  /// rate frozen at today's females under-counted next year's band, the
  /// unsafe side). 0 for an unsexed kind, whose adults all breed.
  float males = 0.0F;
  /// The day, as an offset from today, the herd is in the village from: 0
  /// for a herd that stands, the arrival for a lot of stock on its way.
  std::uint32_t from_day = 0;
};

std::uint16_t RoundHeads(float heads) {
  const float rounded = std::floor(heads + 0.5F);
  return rounded <= 0.0F ? 0U : static_cast<std::uint16_t>(rounded > 65535.0F ? 65535.0F : rounded);
}

/// THE BIRTHS' GATES, as RunBirths reads them (herd_life.cpp): a hungry
/// herd, a kind that keeps sires with none, a horse with no stable — any
/// shut gate, no offspring; and the billet as a SHARE, CalvingRoofShare, the
/// same home RunBirths reads (0.37.62). The share is today's, held over the
/// whole horizon: the forecast does not guess when the chairman will raise
/// the barn. The daily rate a female inside the band as RunBirths draws it
/// (its rate is females × this).
float BirthsPerFemaleBandDay(const ProductionConfig& config,
                             const LivestockDef& kind,
                             LivestockKindId kind_id,
                             const HerdRow& herd,
                             bool stable_built) {
  if (herd.fed_share < config.farming.calving_fed_share_floor) {
    return 0.0F;
  }
  if (kind.sexed != 0 && kind.males_share > 0.0F && herd.adult_male_count == 0) {
    return 0.0F;
  }
  if (kind_id.value == config.horse_kind.value && !stable_built) {
    return 0.0F;
  }
  if (!(kind.births_per_game_year > 0.0F)) {
    return 0.0F;
  }
  const auto band_days = static_cast<float>(
      (config.farming.birth_to_month - config.farming.birth_from_month + 1U) * kDaysPerMonth);
  return band_days > 0.0F
             ? CalvingRoofShare(herd) * kind.births_per_game_year * kind.litter_heads / band_days
             : 0.0F;
}

/// The heads' day of growing up, as RunMaturation flows them: newborns into
/// juveniles over the newborn months, juveniles into adults over the rest.
void MatureOneDay(const LivestockDef& kind, ProjectedHerd& herd) {
  const auto month_days = static_cast<float>(kDaysPerMonth);
  const float newborn_days = kind.newborn_game_months * month_days;
  const float juvenile_days = (kind.adult_from_game_months - kind.newborn_game_months) * month_days;
  // RunMaturation's order: the newborns join the juveniles first, and the
  // juveniles' flow is drawn off the count WITH them (static review of 0.37.57).
  if (newborn_days > 0.0F) {
    const float grown_newborns = herd.newborns / newborn_days;
    herd.newborns -= grown_newborns;
    herd.juveniles += grown_newborns;
  }
  if (juvenile_days > 0.0F) {
    const float grown_juveniles = herd.juveniles / juvenile_days;
    herd.juveniles -= grown_juveniles;
    herd.adults += grown_juveniles;
    // AND THE YOUNG MALES THE HERD HAS NO ROOM FOR GO TO MEAT AS THEY GROW
    // (RunMaturation): half of what grows is male, the herd keeps its share
    // of sires, the rest is culled. Kept out of the forecast at first, it
    // carried a kolkhoz herd's bull calves for a year (0.37.57's pair).
    if (kind.sexed != 0) {
      const auto target = static_cast<float>(TargetMales(kind, RoundHeads(herd.adults)));
      const float room = target > herd.males ? target - herd.males : 0.0F;
      const float culled = std::max(0.0F, grown_juveniles * 0.5F - room);
      herd.males += std::min(grown_juveniles, room);
      herd.adults -= culled;
    }
  }
}

/// The hay the forecast expects cut this year, grams, still to come: last
/// year's (the closed book) less what this year has laid already. Asked from
/// year 2 only: year 1 has no book, and its forecast stops at the first cut
/// (ForecastHerdFeed; boss [88], option (б)).
Grams ExpectedCutStillToCome(const ProductionConfig& config, const WorldState& world) {
  const ResourceId hay = config.hay_resource;
  if (hay.value == kInvalidDefIdValue) {
    return 0;
  }
  const Grams expected = AmountOf(world.ledger.closed.harvest, hay);
  const Grams laid = AmountOf(world.ledger.current.harvest, hay);
  return expected > laid ? expected - laid : 0;
}

/// THE MONTH THE CUT ENDS (static review of 0.37.57): the later of the
/// meadows' cut month and the last harvest month of a crop that gives hay
/// (clover and timothy, June-August) — last year's hay in the book is both,
/// and landing all of it at the meadows' month over-promised, then read 0 in
/// July with the mowing still under way.
std::uint8_t HayCutEndMonth(const ProductionConfig& config) {
  std::uint8_t month = config.farming.meadow_cut_month;
  for (const CropDef& crop : config.crops) {
    if (crop.resource.value == config.hay_resource.value && crop.harvest_to_month > month) {
      month = crop.harvest_to_month;
    }
  }
  return month;
}

/// Whether a delivery of `resource` would find room through the door today:
/// a numbered store that takes it with room left, or an outline that does.
bool StoreHasRoomFor(const ProductionConfig& config, const WorldState& world, ResourceId resource) {
  return std::ranges::any_of(world.units.rows, [&config, resource](const UnitRow& unit) {
    return (NumberedStoreAccepts(unit, config, resource) && FreeRoomGrams(unit, config) > 0) ||
           HomeOutlineAccepts(unit, config, resource);
  });
}

/// Heads a projected herd has for the drain's arithmetic.
float HeadsOf(const ProjectedHerd& herd) {
  return herd.adults + herd.juveniles;
}

/// A standing kolkhoz herd as the forecast starts it.
ProjectedHerd ProjectHerd(const ProductionConfig& config,
                          const WorldState& world,
                          std::uint32_t row,
                          bool stable_built) {
  const HerdRow& herd = world.herds.rows[row];
  ProjectedHerd projected;
  projected.id = world.herds.row_ids[row];
  projected.kind = herd.kind;
  projected.adults = static_cast<float>(herd.adult_count);
  projected.juveniles = static_cast<float>(herd.juvenile_count);
  projected.newborns = static_cast<float>(herd.newborn_count);
  const LivestockDef& kind = config.livestock[herd.kind.value];
  projected.births_per_female_band_day =
      BirthsPerFemaleBandDay(config, kind, herd.kind, herd, stable_built);
  projected.males = kind.sexed != 0 ? static_cast<float>(herd.adult_male_count) : 0.0F;
  return projected;
}

/// One forecast day of a herd, in the herd day's order: the maturing, then
/// the day's births inside the calving band.
void ProjectHerdDay(const ProductionConfig& config, std::uint8_t month, ProjectedHerd& herd) {
  const LivestockDef& kind = config.livestock[herd.kind.value];
  MatureOneDay(kind, herd);
  if (herd.births_per_female_band_day > 0.0F &&
      MonthInRange(month, config.farming.birth_from_month, config.farming.birth_to_month)) {
    (kind.newborn_game_months > 0.0F ? herd.newborns : herd.juveniles) +=
        std::max(0.0F, herd.adults - herd.males) * herd.births_per_female_band_day;
  }
}

}  // namespace

float ForecastHerdHeads(const ProductionConfig& config,
                        const WorldState& world,
                        HerdId herd,
                        std::uint32_t days) {
  const std::uint32_t row = FindRow(world.herds, herd);
  if (row == kNoRow || world.herds.rows[row].kind.value >= config.livestock.size()) {
    return 0.0F;
  }
  ProjectedHerd projected = ProjectHerd(config, world, row, StableBuilt(world, config));
  for (std::uint32_t day = 1; day <= days; ++day) {
    ProjectHerdDay(
        config, static_cast<std::uint8_t>(DateFromDay(world.calendar.day + day).month), projected);
  }
  return projected.adults + projected.juveniles + projected.newborns;
}

std::vector<float> HerdFeedHeldKg(const ProductionConfig& config, const WorldState& world) {
  const ResourceAmounts allowance = HerdFeedAllowance(config, world);
  std::vector<float> held_kg(config.feed_values.size(), 0.0F);
  for (std::uint32_t resource = 0; resource < held_kg.size(); ++resource) {
    if (!(config.feed_values[resource] > 0.0F) || resource >= allowance.size()) {
      continue;
    }
    held_kg[resource] =
        static_cast<float>(allowance[resource]) / static_cast<float>(kGramsPerKilogram);
  }
  if (config.hay_resource.value < held_kg.size()) {
    held_kg[config.hay_resource.value] +=
        static_cast<float>(HeapAbovePlanDebt(world, config.hay_resource)) /
        static_cast<float>(kGramsPerKilogram);
  }
  return held_kg;
}

void DrainFeedDay(const ProductionConfig& config,
                  const std::vector<std::uint8_t>& peoples_foods,
                  std::span<const FeedDayNeed> needs,
                  std::vector<float>& held_kg,
                  std::vector<float>& covered) {
  covered.assign(needs.size(), 0.0F);
  for (std::size_t index = 0; index < needs.size(); ++index) {
    const FeedDayNeed& herd = needs[index];
    float got = 0.0F;
    for (const FeedLinkDef& link : config.feed_links) {
      if (got >= herd.units) {
        break;
      }
      // The kind filter is the whole point: a feeding order belongs to the
      // kind it names, and a cow is never offered the pigs' barley.
      if (link.kind.value != herd.kind.value || link.resource.value >= held_kg.size()) {
        continue;
      }
      // Work-only feeds are the horse's WAGE, not its keep (boss, Q2): in a
      // forecast nobody is in the traces, so they carry nothing.
      if (link.work_only != 0) {
        continue;
      }
      if (link.reserve != 0 && link.resource.value < peoples_foods.size() &&
          peoples_foods[link.resource.value] != 0) {
        continue;  // the people's food: the feeding refuses it to a reserve link
      }
      const float value = config.feed_values[link.resource.value] *
                          (link.reserve != 0 ? config.farming.reserve_feed_factor : 1.0F);
      if (!(value > 0.0F)) {
        continue;
      }
      const float ceiling = herd.units * link.max_share;
      float take_units = herd.units - got;
      take_units = take_units < ceiling ? take_units : ceiling;
      const float available_units = held_kg[link.resource.value] * value;
      take_units = take_units < available_units ? take_units : available_units;
      if (!(take_units > 0.0F)) {
        continue;
      }
      held_kg[link.resource.value] -= take_units / value;
      got += take_units;
    }
    covered[index] = got;
  }
}

FeedPurchase MaximalFeedPurchase(const ProductionConfig& config,
                                 const WorldState& world,
                                 bool* blocked_by_store) {
  FeedPurchase purchase;
  purchase.goods_kg.assign(config.feed_values.size(), 0.0F);
  if (blocked_by_store != nullptr) {
    *blocked_by_store = false;
  }

  // Every goods lot that carries a feed the herds eat, with its feed units a
  // point — the order the points are spent in.
  struct FeedLot {
    LimitLotId lot;
    float units_a_point = 0.0F;
  };

  std::vector<FeedLot> lots;
  for (std::uint32_t index = 0; index < config.limit.lots.size(); ++index) {
    const LimitLotDef& def = config.limit.lots[index];
    if (def.kind != LimitLotKind::kGoods) {
      continue;
    }
    float units = 0.0F;
    for (std::size_t resource = 0;
         resource < def.goods.size() && resource < config.feed_values.size();
         ++resource) {
      if (def.goods[resource] > 0 && config.feed_values[resource] > 0.0F) {
        units += static_cast<float>(def.goods[resource]) / static_cast<float>(kGramsPerKilogram) *
                 config.feed_values[resource];
      }
    }
    if (!(units > 0.0F)) {
      continue;
    }
    const LimitLotId lot{static_cast<std::uint16_t>(index)};
    // The door's own answer with every point of the year in hand: a lot
    // refused for the store alone is the granary's day, not the lot's.
    const OrderRefusal door =
        LimitLotRefusalToday(config, world, lot, std::numeric_limits<std::int32_t>::max());
    if (door == OrderRefusal::kNowhereToStore && blocked_by_store != nullptr) {
      *blocked_by_store = true;
    }
    if (door != OrderRefusal::kNone) {
      continue;
    }
    // A free lot (0 points) is priced as one point: it sorts first and is
    // taken once.
    const float points = def.points > 0 ? static_cast<float>(def.points) : 1.0F;
    lots.push_back(FeedLot{.lot = lot, .units_a_point = units / points});
  }
  // The most feed units a point first; equal lots in the catalogue's order.
  std::ranges::stable_sort(lots, [](const FeedLot& left, const FeedLot& right) {
    return left.units_a_point > right.units_a_point;
  });
  std::int32_t points_left = world.limit.points;
  for (const FeedLot& feed : lots) {
    const LimitLotDef& def = config.limit.lots[feed.lot.value];
    const std::int32_t copies = def.points > 0 ? points_left / def.points : 1;
    if (copies <= 0) {
      continue;
    }
    points_left -= copies * def.points;
    purchase.points += copies * def.points;
    for (std::size_t resource = 0;
         resource < def.goods.size() && resource < purchase.goods_kg.size();
         ++resource) {
      if (def.goods[resource] > 0 && config.feed_values[resource] > 0.0F) {
        purchase.goods_kg[resource] += static_cast<float>(copies) *
                                       static_cast<float>(def.goods[resource]) /
                                       static_cast<float>(kGramsPerKilogram);
        purchase.any = true;
      }
    }
  }
  if (purchase.any && blocked_by_store != nullptr) {
    *blocked_by_store = false;  // a lot IS buyable: the day's move is the lot
  }
  // THE CART'S LATEST DAY: counted as landing early, the purchase would name
  // fewer heads than the fodder is short of.
  purchase.land_day = LimitBaseDeliveryDays(config, world) + config.limit.delivery_delay_days_max;
  return purchase;
}

HerdFeedForecast ForecastHerdFeed(const ProductionConfig& config,
                                  const WorldState& world,
                                  bool one_more_horse,
                                  FeedHorizon horizon,
                                  const FeedPurchase* purchase) {
  HerdFeedForecast forecast;
  if (config.livestock.empty() || config.feed_values.empty()) {
    return forecast;
  }
  const SimDay today = world.calendar.day;
  // THE HORIZON: the first scythes of the NEXT year (boss [68] p. 1: to the
  // new cut, not to the pasture — the horses have none; [92]). From any day
  // of this year that is the same day: next year's.
  const SimDay year_start = today - (today % kDaysPerYear);
  const SimDay cut_end_offset =
      ((static_cast<SimDay>(HayCutEndMonth(config)) + 1U) * kDaysPerMonth) - 1U;
  const SimDay this_cut_end = year_start + cut_end_offset;
  const SimDay cut_from =
      year_start + (static_cast<SimDay>(config.farming.meadow_cut_month) * kDaysPerMonth);
  // YEAR 1 LOOKS ONLY TO THE FIRST CUT (boss [87], [88], option (б)): with no
  // book of a year before, a forecast past it stood on the meadows' ceiling ×
  // a share (0.76, econ's P10) and lit the yellow all year 1 in 27 villages of
  // 27, none of which starved (core's lamp probe on 0.37.57). Before the
  // first mowing: to its first day — does what lies reach the scythes; in the
  // mowing's window: nothing to forecast yet; after it: to next year's cut on
  // the hay actually in, as every later year.
  const bool first_year = world.calendar.date.year <= 1;
  // TO NEXT YEAR'S FIRST SCYTHES, not the end of its mowing: the new cut
  // feeds the herds from its first day, and a horizon to the window's end,
  // with that cut not in it, counted three months of need against nothing —
  // 173 of 365 short days of year 2's spring yellow lay inside year 3's
  // mowing (the check-run of 0.37.57, core's lamp probe).
  //
  // TO THE NEAREST FIRST SCYTHES, IN EVERY YEAR (boss, econ-boss-hay-term-
  // 2026-10-01 [7]; 0.37.96). Until then only year 1 stopped at its own cut:
  // from year 2 the horizon ran from 1 January to the first scythes of the
  // year AFTER, a year and a half and two calvings on, and the lamp of 1
  // January spoke of the January to come — «short for 111-153 heads» with
  // 82-93 in the yards — while the canon's bot handed twenty cows over the
  // same day and the stores never ran dry (hay at May's end 27, 89-99,
  // 165-191, 166-203 t over years 1-4). Before this year's first scythes the
  // question is «does what lies reach them»; from the mowing on, next
  // year's. Past the nearest scythes the lamp is silent.
  // THE HORSE BOUGHT TO STAY keeps the long look (FeedHorizon::
  // kNextYearsScythes): this year's cut in the income and the winter after.
  SimDay horizon_end = cut_from + kDaysPerYear;
  if (first_year && today <= this_cut_end) {
    horizon_end = today < cut_from ? cut_from : today;
  } else if (horizon == FeedHorizon::kNearestScythes && today < cut_from) {
    horizon_end = cut_from;
  }
  const auto horizon_days = static_cast<std::uint32_t>(horizon_end - today);
  forecast.horizon_days = static_cast<std::uint16_t>(horizon_days);
  // THE CUT STILL AHEAD THIS YEAR LANDS DAY BY DAY over the mowing's window,
  // from the first day of the meadows' cut month to the end of the cut, as
  // the mowing lays it (LayMownShare). Landed whole on the cut's last day it
  // lit the yellow in every January of 0.37.57's first pair: the start's hay
  // is sized to the first scythes (day 22). Year 1 expects none (above).
  const Grams expected_cut =
      !first_year && today <= this_cut_end ? ExpectedCutStillToCome(config, world) : 0;
  const std::uint32_t cut_first_day = today <= cut_from ? cut_from - today : 0U;
  const std::uint32_t cut_last_day = today <= this_cut_end ? this_cut_end - today : 0U;
  const float cut_kg_a_day =
      expected_cut > 0 ? static_cast<float>(expected_cut) / static_cast<float>(kGramsPerKilogram) /
                             static_cast<float>(cut_last_day - cut_first_day + 1U)
                       : 0.0F;

  const bool stable_built = StableBuilt(world, config);
  std::vector<ProjectedHerd> herds;
  bool horse_added = !one_more_horse;
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const HerdRow& herd = world.herds.rows[row];
    if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
      continue;  // a family herd eats from its family's pantry, not the farm's
    }
    ProjectedHerd projected = ProjectHerd(config, world, row, stable_built);
    if (!horse_added && herd.kind.value == config.horse_kind.value) {
      projected.adults += 1.0F;
      horse_added = true;
    }
    herds.push_back(projected);
  }
  if (!horse_added && config.horse_kind.value < config.livestock.size()) {
    ProjectedHerd team;
    team.kind = config.horse_kind;
    team.adults = 1.0F;
    herds.push_back(team);
  }
  // THE MOVES ALREADY MADE (econ §4а, boss [70]): stock bought on the limit
  // stands in from its day; its heads eat from the farm's stores.
  for (const LivestockArrivalRow& arrival : world.livestock_arrivals.rows) {
    if (arrival.kind.value >= config.livestock.size()) {
      continue;
    }
    ProjectedHerd coming;
    coming.kind = arrival.kind;
    const auto heads = static_cast<float>(arrival.head_count);
    // Young stock joins newborn_count and climbs the ladder (limit_state.h).
    (arrival.stage == LivestockArrivalStage::kAdultStart ? coming.adults : coming.newborns) = heads;
    coming.from_day = arrival.arrive_day > today ? arrival.arrive_day - today : 0U;
    herds.push_back(coming);
  }
  if (herds.empty()) {
    return forecast;
  }
  float standing = 0.0F;
  for (const ProjectedHerd& herd : herds) {
    standing += HeadsOf(herd);
  }
  const auto standing_heads = static_cast<std::int64_t>(std::ceil(standing));

  std::vector<float> held_kg = HerdFeedHeldKg(config, world);
  const std::vector<std::uint8_t> peoples_foods = PeoplesFoods(config, world);
  // The feed lots on the road land on their day, where a store would take
  // them (a lot with nowhere to go is refused at the door — kNowhereToStore).
  std::vector<std::pair<std::uint32_t, std::vector<float>>> landings;
  for (const LimitDeliveryRow& cart : world.limit_deliveries.rows) {
    std::vector<float> goods(held_kg.size(), 0.0F);
    bool any = false;
    for (std::size_t resource = 0; resource < cart.goods.size() && resource < goods.size();
         ++resource) {
      const ResourceId id = DefIdFromIndex<ResourceIdTag>(resource);
      if (cart.goods[resource] <= 0 || !(config.feed_values[resource] > 0.0F) ||
          !StoreHasRoomFor(config, world, id)) {
        continue;
      }
      goods[resource] =
          static_cast<float>(cart.goods[resource]) / static_cast<float>(kGramsPerKilogram);
      any = true;
    }
    if (any) {
      landings.emplace_back(cart.arrive_day > today ? cart.arrive_day - today : 0U,
                            std::move(goods));
    }
  }
  // AND THE PURCHASE ASKED ABOUT (FeedPurchase; 0.37.122), by the carts' own
  // rule: only what a store has room for lands.
  if (purchase != nullptr && purchase->any) {
    std::vector<float> goods(held_kg.size(), 0.0F);
    bool any = false;
    for (std::size_t resource = 0; resource < purchase->goods_kg.size() && resource < goods.size();
         ++resource) {
      if (purchase->goods_kg[resource] > 0.0F &&
          StoreHasRoomFor(config, world, DefIdFromIndex<ResourceIdTag>(resource))) {
        goods[resource] = purchase->goods_kg[resource];
        any = true;
      }
    }
    if (any) {
      landings.emplace_back(purchase->land_day, std::move(goods));
    }
  }

  // The night pasture's conditions by month, once: they walk the residents.
  std::vector<std::uint8_t> team_out_in(kMonthsPerYear, 0);
  for (std::uint8_t month = 0; month < kMonthsPerYear; ++month) {
    team_out_in[month] = TeamOutInMonth(config, world, month) ? 1U : 0U;
  }
  constexpr std::uint32_t kNeverOut = std::numeric_limits<std::uint32_t>::max();
  std::vector<std::uint32_t> out_day(held_kg.size(), kNeverOut);
  std::vector<FeedDayNeed> needs;
  std::vector<std::size_t> needs_of;  // index into `herds` per need
  std::vector<float> covered;
  std::vector<float> held_before;
  for (std::uint32_t day = 0; day < horizon_days; ++day) {
    const auto month = static_cast<std::uint8_t>(DateFromDay(today + day).month);
    if (cut_kg_a_day > 0.0F && day >= cut_first_day && day <= cut_last_day &&
        config.hay_resource.value < held_kg.size()) {
      held_kg[config.hay_resource.value] += cut_kg_a_day;
    }
    for (const auto& [landing_day, goods] : landings) {
      if (landing_day == day) {
        for (std::size_t resource = 0; resource < goods.size(); ++resource) {
          held_kg[resource] += goods[resource];
        }
      }
    }
    const bool team_out = month < team_out_in.size() && team_out_in[month] != 0;
    needs.clear();
    needs_of.clear();
    for (std::size_t index = 0; index < herds.size(); ++index) {
      ProjectedHerd& herd = herds[index];
      if (day < herd.from_day) {
        continue;
      }
      const LivestockDef& kind = config.livestock[herd.kind.value];
      // Today (day 0) is fed from what stands; the herds grow from tomorrow,
      // as ForecastHerdHeads counts them.
      if (day > 0) {
        ProjectHerdDay(config, month, herd);
      }
      HerdRow row;
      row.adult_count = RoundHeads(herd.adults);
      row.juvenile_count = RoundHeads(herd.juveniles);
      const bool grazing = herd.kind.value != config.horse_kind.value || team_out;
      const float units = FeedNeedUnits(config, kind, row, month, grazing);
      if (units > 0.0F) {
        needs.push_back(FeedDayNeed{.kind = herd.kind, .units = units});
        needs_of.push_back(index);
      }
    }
    // A FEED RUNS OUT when the day's drain takes it from held to nothing: a
    // feed the herds never had did not run out (static review of 0.37.57 —
    // every absent feed read «out on day 0», and the cows short of hay on day
    // 60 were said short of silage they never held).
    held_before = held_kg;
    DrainFeedDay(config, peoples_foods, needs, held_kg, covered);
    for (std::size_t resource = 0; resource < held_kg.size(); ++resource) {
      if (out_day[resource] == kNeverOut && held_before[resource] >= kOutKg &&
          held_kg[resource] < kOutKg) {
        out_day[resource] = day;
      }
    }
    float unfed_heads = 0.0F;
    for (std::size_t index = 0; index < needs.size(); ++index) {
      if (covered[index] + kFedSlackUnits >= needs[index].units) {
        continue;
      }
      const ProjectedHerd& herd = herds[needs_of[index]];
      unfed_heads += (needs[index].units - covered[index]) / needs[index].units * HeadsOf(herd);
      if (forecast.short_ahead) {
        continue;
      }
      // THE FIRST SHORT DAY: its herd, and among its kind's links the feed
      // that ran out earliest (boss [86] p. 3: the truth of what runs out).
      forecast.short_ahead = true;
      forecast.days_ahead = static_cast<std::uint16_t>(day);
      forecast.herd = herd.id;
      std::uint32_t earliest = kNeverOut;
      for (const FeedLinkDef& link : config.feed_links) {
        if (link.kind.value != herd.kind.value || link.work_only != 0 ||
            link.resource.value >= out_day.size()) {
          continue;
        }
        if (forecast.first_short.value == kInvalidDefIdValue ||
            out_day[link.resource.value] < earliest) {
          earliest = out_day[link.resource.value];
          forecast.first_short = link.resource;
        }
      }
    }
    const auto heads = static_cast<std::int64_t>(std::ceil(unfed_heads));
    forecast.heads_short = heads > forecast.heads_short ? heads : forecast.heads_short;
  }
  // NEVER MORE HEADS THAN STAND TODAY (0.37.96; boss, econ-boss-hay-term
  // [7] (б)): the calves to come are in the need above, and a chairman told
  // «short for 127 heads» with 92 in the yards is told a number he cannot
  // act on. The stock on the road counts: its heads are bought already.
  forecast.heads_short = std::min(forecast.heads_short, standing_heads);
  return forecast;
}

bool CutCanBeHurried(const WorldState& world) {
  return std::ranges::any_of(world.fields.rows, [](const FieldRow& field) {
    if (field.kind == LandKind::kArable || field.phase != FieldPhase::kHarvest ||
        !(field.work_days_remaining > 0.0F)) {
      return false;
    }
    // The avral on THIS cut (FieldRow::rush_phase), below its last step: the
    // door of the move (kDeclareRush) takes one step more.
    const std::int64_t step = field.rush_phase == field.phase ? field.rush_step : 0;
    return step < kMaxRushStep;
  });
}

FodderAdvice AdviseOnShortFodder(const ProductionConfig& config,
                                 const WorldState& world,
                                 const HerdFeedForecast& forecast) {
  FodderAdvice advice;
  if (!forecast.short_ahead) {
    return advice;
  }
  // 1. THE CUT IS A MOVE ONLY WHILE A MEADOW STANDS IN IT (the harvest rule
  // 3, part А; boss, econ-boss-hay-term-2026-10-01 [2]; 0.37.93). Until then
  // the hay's advice was the cut on every day of the year, and a meadow that
  // no longer winters in its cut would have left three villages of the canon
  // shedding horses under a move nobody can make: a meadow marked in November
  // gives its hay in June.
  // AND ONLY WHILE THE CUT CAN STILL BE HURRIED (CutCanBeHurried; 0.37.123):
  // a meadow in its cut whose avral stands at the last step has no move left
  // on it, and the ladder goes on.
  if (CutCanBeHurried(world)) {
    advice.advice = AlarmAdvice::kCutHay;
    return advice;
  }
  // 2. THE DISTRICT'S FEED, IF THE DOOR TAKES THE ORDER TODAY AND IT HELPS
  // (0.37.122). «Helps»: the forecast after it is not short, or is short
  // later, or of fewer heads — a purchase that moves none of the three feeds
  // nobody (a feed's share of the ration is capped, and the hole is in
  // another feed).
  bool blocked_by_store = false;
  const FeedPurchase purchase = MaximalFeedPurchase(config, world, &blocked_by_store);
  if (purchase.any) {
    const HerdFeedForecast after =
        ForecastHerdFeed(config, world, false, FeedHorizon::kNearestScythes, &purchase);
    const bool helps = !after.short_ahead || after.days_ahead > forecast.days_ahead ||
                       after.heads_short < forecast.heads_short;
    if (helps) {
      advice.advice = AlarmAdvice::kBuyFeed;
      // The feed it brings most of, by feed units: the lamp names one.
      float most_units = 0.0F;
      for (std::size_t resource = 0; resource < purchase.goods_kg.size(); ++resource) {
        const float units = purchase.goods_kg[resource] * config.feed_values[resource];
        if (units > most_units) {
          most_units = units;
          advice.advice_resource = DefIdFromIndex<ResourceIdTag>(resource);
          advice.advice_amount = GramsFromKilograms(purchase.goods_kg[resource]);
        }
      }
      if (after.short_ahead && after.heads_short > 0) {
        advice.advice_more = AlarmAdvice::kReduceHerd;
        advice.amount_more = after.heads_short;
      }
      return advice;
    }
  }
  // 3. THE GRANARY, when a feed lot is refused for want of a store alone —
  // its site under way or not yet marked: the door refuses either way
  // (kNowhereToStore), and «buy» on such a day was an order refused.
  if (!purchase.any && blocked_by_store) {
    advice.advice = AlarmAdvice::kGranaryForFeed;
    advice.advice_more = AlarmAdvice::kReduceHerd;
    advice.amount_more = forecast.heads_short;
    return advice;
  }
  // 4. FEWER HEADS: the alarm's own `amount` is the number.
  advice.advice = AlarmAdvice::kReduceHerd;
  return advice;
}

void CollectHerdForecastAlarms(const ProductionConfig& config,
                               const WorldState& world,
                               std::vector<Alarm>& alarms) {
  // TO THE NEAREST FIRST SCYTHES (0.37.96): the yellow speaks of a shortage
  // this side of the next cut and of none beyond it.
  const HerdFeedForecast forecast =
      ForecastHerdFeed(config, world, false, FeedHorizon::kNearestScythes);
  if (!forecast.short_ahead) {
    return;
  }
  Alarm alarm;
  alarm.kind = AlarmKind::kHerdHayShortAhead;
  alarm.herd = forecast.herd;
  alarm.resource = forecast.first_short;
  alarm.amount = forecast.heads_short;
  alarm.days_ahead = forecast.days_ahead;
  const FodderAdvice advice = AdviseOnShortFodder(config, world, forecast);
  alarm.advice = advice.advice;
  alarm.advice_resource = advice.advice_resource;
  alarm.advice_amount = advice.advice_amount;
  alarm.advice_more = advice.advice_more;
  alarm.amount_more = advice.amount_more;
  alarm.lamp = 0;
  alarms.push_back(alarm);
}

}  // namespace core
