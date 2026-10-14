// A farm's own store of coarse feed (feed_home.h): the binding, the take, the
// ricks' drain and the door's choice of a rick.

#include "feed_home.h"

#include <algorithm>
#include <limits>

#include "core_common/calendar.h"
#include "core_common/day_off.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "field_haul.h"
#include "herd_forecast.h"
#include "herd_system.h"
#include "night_pasture.h"
#include "stock_ops.h"

namespace core {
namespace {

/// A rick the herds EAT from: a built outline whose type is hay's home. Not
/// HomeOutlineAccepts - a rick being emptied is nobody's home for a delivery
/// and still the rick its farm eats down.
bool IsRickToEatFrom(const UnitRow& unit, const ProductionConfig& config) {
  if (unit.dead != 0 || unit.level == 0 || unit.type.value >= config.unit_types.size() ||
      StorageCapacityGrams(unit, config) >= 0) {
    return false;
  }
  const auto& homes = config.unit_types[unit.type.value].home_of;
  return std::ranges::any_of(
      homes, [&config](ResourceId held) { return held.value == config.hay_resource.value; });
}

/// The way a load takes in this village: the cart's, or the carrier's when no
/// horse is harnessed (HeapRickRow's own choice until this step).
TravelMode LoadMode(const ProductionConfig& config, const WorldState& world) {
  return SettlementHasCarts(world, config.horse_kind) ? TravelMode::kCart : TravelMode::kWalk;
}

/// The rick a herd fed at `farm_row` eats from: the nearest by road, the
/// first in row order on a tie. With one rick in the village no road is
/// measured unless `road_km` asks for it.
std::uint32_t NearestRickToEatFrom(const ProductionConfig& config,
                                   const WorldState& world,
                                   std::uint32_t farm_row,
                                   float* road_km) {
  std::uint32_t only = kNoRow;
  std::uint32_t count = 0;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (IsRickToEatFrom(world.units.rows[row], config)) {
      only = count == 0 ? row : only;
      ++count;
    }
  }
  if (count == 0 || (count == 1 && road_km == nullptr)) {
    return only;
  }
  const Vec2 farm = world.units.rows[farm_row].position;
  const TravelMode mode = LoadMode(config, world);
  std::uint32_t nearest = kNoRow;
  float nearest_km = std::numeric_limits<float>::max();
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (!IsRickToEatFrom(world.units.rows[row], config)) {
      continue;
    }
    const float km = RoadKm(world, mode, world.units.rows[row].position, farm);
    if (km < nearest_km) {
      nearest_km = km;
      nearest = row;
    }
  }
  if (road_km != nullptr) {
    *road_km = nearest_km;
  }
  return nearest;
}

float KgOf(Grams grams) {
  return static_cast<float>(grams) / static_cast<float>(kGramsPerKilogram);
}

/// The bound feeds, dense by ResourceId over the feed values.
std::vector<std::uint8_t> BoundFeeds(const ProductionConfig& config) {
  std::vector<std::uint8_t> bound(config.feed_values.size(), 0);
  for (std::size_t resource = 0; resource < bound.size(); ++resource) {
    bound[resource] =
        IsBoundFeed(config, DefIdFromIndex<ResourceIdTag>(static_cast<std::uint32_t>(resource)))
            ? 1
            : 0;
  }
  return bound;
}

/// One unit's stock of the bound feeds as a drain carries it, kilograms.
struct FeedPool {
  std::uint32_t unit_row = kNoRow;
  std::vector<float> kg;
};

FeedPool& PoolOf(std::vector<FeedPool>& pools,
                 const WorldState& world,
                 const std::vector<std::uint8_t>& bound,
                 std::uint32_t unit_row) {
  for (FeedPool& pool : pools) {
    if (pool.unit_row == unit_row) {
      return pool;
    }
  }
  FeedPool pool;
  pool.unit_row = unit_row;
  pool.kg.assign(bound.size(), 0.0F);
  for (std::size_t resource = 0; resource < bound.size(); ++resource) {
    if (bound[resource] != 0) {
      pool.kg[resource] =
          KgOf(UnreservedOf(world.units.rows[unit_row],
                            DefIdFromIndex<ResourceIdTag>(static_cast<std::uint32_t>(resource))));
    }
  }
  pools.push_back(pool);
  return pools.back();
}

/// A kolkhoz herd bound to a rick, as the drain walks it.
struct BoundHerd {
  std::uint32_t herd_row = kNoRow;
  std::uint32_t farm_row = kNoRow;
  std::uint32_t rick_row = kNoRow;
  /// Whether its kind eats a bound feed at all (a link that is not the
  /// horse's wage): a herd that does not says nothing of a rick.
  bool eats_bound = false;
};

/// A rick with herds bound to it and the first day of the drain its herds
/// were out of their bound feeds and not fed in full; `days` when none was.
struct RickDays {
  std::uint32_t rick_row = kNoRow;
  std::uint32_t short_day = 0;
};

/// THE RICKS' DRAIN: the herds standing today fed day by day by the
/// forecast's own drain (DrainFeedDay), each off ITS pool for the bound feeds
/// - its own unit's stock, then its rick's - and off the village's allowance
/// for the rest. No offspring, no cut to come, no cart on the road, as
/// StoredHayShortWithin: the need is the smaller for it.
/// @param stop_at_first Stops on the first day a rick is short - the door's
///        choice asks only which rick runs out first.
std::vector<RickDays> DrainRicks(const ProductionConfig& config,
                                 const WorldState& world,
                                 std::uint32_t days,
                                 bool stop_at_first) {
  std::vector<RickDays> ricks;
  if (days == 0 || config.livestock.empty() ||
      config.hay_resource.value >= config.feed_values.size()) {
    return ricks;
  }
  const std::vector<std::uint8_t> bound = BoundFeeds(config);
  std::vector<BoundHerd> herds;
  std::vector<FeedPool> pools;
  // The pools are held by row, and a vector that grows moves them: reserved
  // for every unit a herd could name, twice (its farm and its rick).
  pools.reserve(world.herds.rows.size() * 2U);
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const HerdRow& herd = world.herds.rows[row];
    if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
      continue;
    }
    BoundHerd entry;
    entry.herd_row = row;
    entry.farm_row = FarmRowOf(world, config, herd);
    if (entry.farm_row == kNoRow) {
      continue;  // nothing to bind to: the old chain, and no rick's matter
    }
    entry.rick_row = NearestRickToEatFrom(config, world, entry.farm_row, nullptr);
    if (entry.rick_row == kNoRow) {
      continue;
    }
    for (const FeedLinkDef& link : config.feed_links) {
      if (link.kind.value == herd.kind.value && link.work_only == 0 &&
          link.resource.value == config.hay_resource.value) {
        entry.eats_bound = true;  // hay: the one feed a rick is short of (below)
      }
    }
    PoolOf(pools, world, bound, entry.farm_row);
    PoolOf(pools, world, bound, entry.rick_row);
    herds.push_back(entry);
    if (std::ranges::none_of(
            ricks, [&entry](const RickDays& rick) { return rick.rick_row == entry.rick_row; })) {
      ricks.push_back(RickDays{.rick_row = entry.rick_row, .short_day = days});
    }
  }
  if (herds.empty()) {
    return ricks;
  }
  std::ranges::sort(ricks, {}, &RickDays::rick_row);
  // The village's allowance for the feeds with a numbered home, as the herd
  // feeding draws on it.
  const ResourceAmounts allowance = HerdFeedAllowance(config, world);
  std::vector<float> held_kg(config.feed_values.size(), 0.0F);
  for (std::size_t resource = 0; resource < held_kg.size() && resource < allowance.size();
       ++resource) {
    if (config.feed_values[resource] > 0.0F) {
      held_kg[resource] = KgOf(allowance[resource]);
    }
  }
  const std::vector<std::uint8_t> peoples_foods = PeoplesFoods(config, world);
  std::vector<float> covered;
  std::vector<float> village_bound(bound.size(), 0.0F);
  for (std::uint32_t day = 0; day < days; ++day) {
    const auto month = static_cast<std::uint8_t>(DateFromDay(world.calendar.day + day).month);
    const bool team_out = TeamOutInMonth(config, world, month);
    bool some_short = false;
    for (const BoundHerd& entry : herds) {
      const HerdRow& herd = world.herds.rows[entry.herd_row];
      const bool grazing = herd.kind.value != config.horse_kind.value || team_out;
      const FeedDayNeed need{
          .kind = herd.kind,
          .units = FeedNeedUnits(config, config.livestock[herd.kind.value], herd, month, grazing)};
      if (!(need.units > 0.0F)) {
        continue;
      }
      FeedPool& own = PoolOf(pools, world, bound, entry.farm_row);
      FeedPool& rick = PoolOf(pools, world, bound, entry.rick_row);
      const bool one_pool = entry.farm_row == entry.rick_row;
      // The herd's pool stands in the village's cells for the bound feeds
      // while this herd is drained, and the village's own go back after.
      for (std::size_t resource = 0; resource < bound.size(); ++resource) {
        if (bound[resource] != 0) {
          village_bound[resource] = held_kg[resource];
          held_kg[resource] = own.kg[resource] + (one_pool ? 0.0F : rick.kg[resource]);
        }
      }
      DrainFeedDay(config, peoples_foods, std::span<const FeedDayNeed>(&need, 1), held_kg, covered);
      bool pool_out = true;
      for (std::size_t resource = 0; resource < bound.size(); ++resource) {
        if (bound[resource] == 0) {
          continue;
        }
        const float before = own.kg[resource] + (one_pool ? 0.0F : rick.kg[resource]);
        const float eaten = before - held_kg[resource];
        const float from_own = eaten < own.kg[resource] ? eaten : own.kg[resource];
        own.kg[resource] -= from_own;
        if (!one_pool) {
          rick.kg[resource] -= eaten - from_own;
        }
        // OUT OF HAY, and of hay alone - the village's own question
        // (StoredHayShortWithin). Arm A's trace: the team's rick held no hay
        // and 6-19 t of STRAW, a feed a horse's day takes a fifth of at most;
        // asked «every bound feed its kind eats», the pool was never out, the
        // rick never short, and the herd ate 4 t of hay a day from the far
        // rick with stacks lying - 48 such village-days with no urgent word.
        if (resource == config.hay_resource.value) {
          pool_out = held_kg[resource] < kOutKg;
        }
        held_kg[resource] = village_bound[resource];
      }
      // SHORT, as the village's question words it (StoredHayShortWithin): the
      // bound feeds are out AND a herd that eats them is not fed in full.
      if (entry.eats_bound && pool_out && covered[0] + kFedSlackUnits < need.units) {
        for (RickDays& short_rick : ricks) {
          if (short_rick.rick_row == entry.rick_row && short_rick.short_day > day) {
            short_rick.short_day = day;
            some_short = true;
          }
        }
      }
    }
    if (stop_at_first && some_short) {
      break;
    }
  }
  return ricks;
}

/// The days from today to the hay lamp's horizon: the next cut's opening and
/// the days until its hay is in a store (herd_forecast.cpp, the yellow
/// lamp's own end).
std::uint32_t DaysToTheLampsHorizon(const ProductionConfig& config, const WorldState& world) {
  const SimDay today = world.calendar.day;
  const SimDay year_start = today - (today % kDaysPerYear);
  const SimDay cut_from =
      year_start + (static_cast<SimDay>(config.farming.meadow_cut_month) * kDaysPerMonth);
  const SimDay horizon_end = (today < cut_from ? cut_from : cut_from + kDaysPerYear) +
                             config.farming.hay_new_cut_in_store_days;
  return static_cast<std::uint32_t>(horizon_end - today);
}

}  // namespace

bool IsBoundFeed(const ProductionConfig& config, ResourceId resource) {
  if (config.resource_stores_read == 0 || resource.value == kInvalidDefIdValue ||
      config.hay_resource.value == kInvalidDefIdValue) {
    return false;
  }
  for (const auto& type : config.unit_types) {
    bool hay_home = false;
    bool its_home = false;
    for (const ResourceId held : type.home_of) {
      hay_home = hay_home || held.value == config.hay_resource.value;
      its_home = its_home || held.value == resource.value;
    }
    if (hay_home && its_home) {
      return true;
    }
  }
  return false;
}

std::uint32_t FarmRowOf(const WorldState& world,
                        const ProductionConfig& config,
                        const HerdRow& herd) {
  if (herd.household_owned != 0) {
    return kNoRow;
  }
  return herd.unit.value == kInvalidEntityIdValue ? FindStockYardRow(world, config)
                                                  : FindRow(world.units, herd.unit);
}

FeedHome FeedHomeOf(const ProductionConfig& config,
                    const WorldState& world,
                    std::uint32_t farm_row) {
  FeedHome home;
  float road_km = 0.0F;
  home.rick_row = NearestRickToEatFrom(config, world, farm_row, &road_km);
  home.road_km = home.rick_row == kNoRow ? 0.0F : road_km;
  return home;
}

BoundTake TakeBoundFeedAt(const ProductionConfig& config,
                          WorldState& world,
                          std::uint32_t farm_row,
                          std::uint32_t rick_row,
                          ResourceId resource,
                          Grams wanted,
                          std::uint32_t* far_row) {
  BoundTake take;
  take.from_own_unit = TakeFromUnit(world.units.rows[farm_row], resource, wanted);
  Grams left = wanted - take.from_own_unit;
  if (left > 0 && rick_row != kNoRow && rick_row != farm_row) {
    take.from_its_rick = TakeFromUnit(world.units.rows[rick_row], resource, left);
    left -= take.from_its_rick;
  }
  if (left <= 0) {
    return take;
  }
  // FORM (b)'s FALLBACK, the old chain with no cart on the road: the first
  // stock yard's manger, then every store in row order. The first unit that
  // gave is named for the day's count.
  std::uint32_t first_far = kNoRow;
  const std::uint32_t manger = FindStockYardRow(world, config);
  if (manger != kNoRow && manger != farm_row && manger != rick_row) {
    const Grams got = TakeFromUnit(world.units.rows[manger], resource, left);
    if (got > 0) {
      first_far = manger;
    }
    take.from_far += got;
    left -= got;
  }
  for (std::uint32_t row = 0; row < world.units.rows.size() && left > 0; ++row) {
    if (row == farm_row || row == rick_row || row == manger ||
        !IsRickToEatFrom(world.units.rows[row], config)) {
      continue;
    }
    const Grams got = TakeFromUnit(world.units.rows[row], resource, left);
    if (got > 0 && first_far == kNoRow) {
      first_far = row;
    }
    take.from_far += got;
    left -= got;
  }
  if (left > 0) {
    // Whatever numbered store holds it (a hay barn, a granary of the pool
    // without the homes' table): the door's own take, unnamed.
    take.from_far += TakeFromStorage(world, config, resource, left);
  }
  if (far_row != nullptr && take.from_far > 0) {
    *far_row = first_far;
  }
  return take;
}

BoundTake TakeBoundFeed(const ProductionConfig& config,
                        WorldState& world,
                        std::uint32_t farm_row,
                        ResourceId resource,
                        Grams wanted) {
  return TakeBoundFeedAt(config,
                         world,
                         farm_row,
                         NearestRickToEatFrom(config, world, farm_row, nullptr),
                         resource,
                         wanted,
                         nullptr);
}

std::vector<ShortRick> ShortRicksWithin(const ProductionConfig& config,
                                        const WorldState& world,
                                        std::uint32_t days) {
  std::vector<ShortRick> short_ricks;
  for (const RickDays& rick : DrainRicks(config, world, days, false)) {
    if (rick.short_day < days) {
      short_ricks.push_back(ShortRick{.rick_row = rick.rick_row, .days_left = rick.short_day});
    }
  }
  return short_ricks;
}

std::vector<RickGap> RickGapsToTheHorizon(const ProductionConfig& config, const WorldState& world) {
  const std::uint32_t horizon = DaysToTheLampsHorizon(config, world);
  std::vector<RickGap> gaps;
  for (const RickDays& rick : DrainRicks(config, world, horizon, false)) {
    gaps.push_back(RickGap{.rick_row = rick.rick_row, .gap_days = horizon - rick.short_day});
  }
  return gaps;
}

std::uint32_t RickForLoadAt(const ProductionConfig& config,
                            const WorldState& world,
                            Vec2 from,
                            ResourceId resource,
                            RickChoice* choice) {
  std::uint32_t only = kNoRow;
  std::uint32_t count = 0;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (unit.dead == 0 && HomeOutlineAccepts(unit, config, resource)) {
      only = row;
      ++count;
    }
  }
  if (count <= 1) {
    if (choice != nullptr) {
      *choice = RickChoice{.rick_row = only, .nearest_row = only};
    }
    return only;  // kNoRow with none: the store door, as every other load
  }
  // THE NEAREST BY ROAD UNLESS A RICK IS SHORT WITHIN THE URGENT HORIZON (arm A
  // of step two; boss, 14 October 2026): the first pair of the body balanced
  // the ricks to the hay lamp's horizon - «the largest gap» - and 37-58 % of
  // the hay went past the nearer rick; all hauling rose by 5.6 % on nine
  // villages over four years, against a limit of 5. The horizon is the urgent
  // rule's own (day_off.h, UrgentHayHorizonDays): the rick a stack is sent
  // ahead of all work for is the rick its hay is priced to. The first rick to
  // run out wins; the ricks short on that day tie by the road.
  const std::uint32_t horizon =
      UrgentHayHorizonDays(world, static_cast<std::uint32_t>(config.hay_cart_urgent_days));
  const std::vector<RickDays> ricks = DrainRicks(config, world, horizon, true);
  std::uint32_t first_short = horizon;
  for (const RickDays& rick : ricks) {
    first_short = rick.short_day < first_short ? rick.short_day : first_short;
  }
  const TravelMode mode = LoadMode(config, world);
  std::uint32_t chosen = kNoRow;
  float chosen_km = std::numeric_limits<float>::max();
  std::uint32_t nearest = kNoRow;
  float nearest_km = std::numeric_limits<float>::max();
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (unit.dead != 0 || !HomeOutlineAccepts(unit, config, resource)) {
      continue;
    }
    const float km = RoadKm(world, mode, from, unit.position);
    if (km < nearest_km) {
      nearest_km = km;
      nearest = row;
    }
    const bool has_the_gap =
        first_short < horizon && std::ranges::any_of(
                                     ricks,
                                     [row, first_short](const RickDays& r) {
                                       return r.rick_row == row && r.short_day == first_short;
                                     });
    if (has_the_gap && km < chosen_km) {
      chosen_km = km;
      chosen = row;
    }
  }
  if (chosen == kNoRow) {
    chosen = nearest;
    chosen_km = nearest_km;
    // THE OVERFLOW (arm A3; econ and boss, 14 October 2026): the nearest rick
    // takes the hay UNTIL IT HOLDS ITS HERDS' HAY TO THE LAMP's HORIZON; what
    // is carted after that goes to the rick that does not, the largest gap
    // first, ties by the road. Arm A2's print: with the nearest rick alone, on
    // the day the last stack of year 2's cut was carted the cattle yard's rick
    // held 560 t more than its herds ate to the next cut and the horse yard's
    // 243 t less, on nine villages - and those 243 t were eaten from the far
    // rick in the spring. The split is mended at the carting, in the autumn
    // and winter the carts run anyway, past the nearer rick only by the
    // overflow («the largest gap» for every load cost 5.6 % of all hauling).
    const std::uint32_t lamp = DaysToTheLampsHorizon(config, world);
    const std::vector<RickDays> to_the_lamp = DrainRicks(config, world, lamp, false);
    const auto gap_of = [&to_the_lamp, lamp](std::uint32_t row) -> std::uint32_t {
      for (const RickDays& rick : to_the_lamp) {
        if (rick.rick_row == row) {
          return lamp - rick.short_day;
        }
      }
      return 0;  // a rick no herd is bound to lacks nothing
    };
    if (nearest != kNoRow && gap_of(nearest) == 0) {
      std::uint32_t widest = 0;
      float widest_km = std::numeric_limits<float>::max();
      for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
        const UnitRow& unit = world.units.rows[row];
        if (row == nearest || unit.dead != 0 || !HomeOutlineAccepts(unit, config, resource)) {
          continue;
        }
        const std::uint32_t gap = gap_of(row);
        if (gap == 0 || gap < widest) {
          continue;
        }
        const float km = RoadKm(world, mode, from, unit.position);
        if (gap > widest || km < widest_km) {
          widest = gap;
          widest_km = km;
          chosen = row;
          chosen_km = km;
        }
      }
    }
  }
  if (choice != nullptr) {
    *choice = RickChoice{
        .rick_row = chosen, .nearest_row = nearest, .road_km = chosen_km, .nearest_km = nearest_km};
  }
  return chosen;
}

std::uint32_t RickForLoadFrom(const ProductionConfig& config,
                              const WorldState& world,
                              const FieldRow& field) {
  return RickForLoadAt(config, world, FieldHeapPoint(world, field), field.reaped_resource, nullptr);
}

std::uint32_t HeapRickRow(const ProductionConfig& config,
                          const WorldState& world,
                          const FieldRow& field) {
  const ResourceId resource = field.reaped_resource;
  // An unnamed hay resource compares equal to an unnamed load by two 0xFFFF
  // sentinels (0.17.79): named, or it is nobody's manger.
  // A MEADOW's HAY, or A FIELD's STRAW on any land (routing stage В4, item 4):
  // the straw lying at the field goes to a rick by the same door.
  const bool meadow_hay =
      field.kind != LandKind::kArable && resource.value == config.hay_resource.value;
  const bool straw = resource.value == config.straw_resource.value;
  if (resource.value == kInvalidDefIdValue || !(meadow_hay || straw)) {
    return kNoRow;
  }
  // THE RICK THE DOOR CHOSE (RickForLoadFrom; routing stage В3, step two).
  // Until 0.37.252 this was «the first stock yard in row order», a door that
  // asked no distance; until step two, the nearest rick by road whatever its
  // farm's need.
  //
  // CHOSEN IN THE EVENING AND KEPT THROUGH THE DAY on the field's own row
  // (FieldRow::haul_to, the trip's receiver the evening writes beside the
  // price): the hours' deliveries go where the day's carting was priced to,
  // and the ricks' drain is not walked once a field an hour. A rick that no
  // longer stands, or is being emptied, stops matching and the rule is asked
  // again that hour - the load does not wait.
  std::uint32_t only = kNoRow;
  std::uint32_t count = 0;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (unit.dead != 0 || !HomeOutlineAccepts(unit, config, resource)) {
      continue;
    }
    if (unit.position.x == field.haul_to.x && unit.position.y == field.haul_to.y) {
      return row;
    }
    only = row;
    ++count;
  }
  return count <= 1 ? only : RickForLoadFrom(config, world, field);
}

void ChooseRickForHeap(const ProductionConfig& config,
                       const WorldState& world,
                       FieldRow& field,
                       bool afresh) {
  const bool meadow_hay =
      field.kind != LandKind::kArable && field.reaped_resource.value == config.hay_resource.value;
  const bool straw = field.reaped_resource.value == config.straw_resource.value;
  if (field.reaped_resource.value == kInvalidDefIdValue || !(meadow_hay || straw)) {
    return;
  }
  std::uint32_t count = 0;
  bool stands = false;
  for (const UnitRow& unit : world.units.rows) {
    if (unit.dead != 0 || !HomeOutlineAccepts(unit, config, field.reaped_resource)) {
      continue;
    }
    ++count;
    stands = stands || (unit.position.x == field.haul_to.x && unit.position.y == field.haul_to.y);
  }
  // One rick is the answer whatever the row holds; two and more are asked
  // afresh in the evening, and in the day only when the row names none.
  if (count < 2 || (stands && !afresh)) {
    return;
  }
  const std::uint32_t rick = RickForLoadFrom(config, world, field);
  if (rick != kNoRow) {
    field.haul_to = world.units.rows[rick].position;
  }
}

std::int64_t PackFarRickAmount(float road_km, Grams grams) {
  float tens = road_km * kFarRickTensOfMetresPerKm;
  tens = tens < 0.0F ? 0.0F : tens;
  const std::int64_t way =
      tens >= static_cast<float>(kFarRickWayMax) ? kFarRickWayMax : static_cast<std::int64_t>(tens);
  std::int64_t kilograms = grams > 0 ? grams / kGramsPerKilogram : 0;
  kilograms = kilograms > kFarRickKilogramsMax ? kFarRickKilogramsMax : kilograms;
  return way | (kilograms << kFarRickKilogramsShift);
}

}  // namespace core
