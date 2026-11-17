/// @file
/// @brief The district's seed lot (seed_lot.h; #1377).

#include "seed_lot.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core_common/state_table_ops.h"
#include "district_limit.h"
#include "production_alarms.h"
#include "seed_room.h"
#include "stock_ops.h"

namespace core {

namespace {

/// The most-short seed today: the largest shortfall; a tie goes to the
/// nearest sowing (DaysToSowingEnd), then the lower id (boss [2] Q2).
/// Invalid when nothing is short.
ResourceId MostShortSeed(const ProductionConfig& config,
                         const WorldState& world,
                         const std::vector<Grams>& short_by) {
  ResourceId best;
  Grams best_short = 0;
  std::uint32_t best_days = 0;
  for (std::uint32_t index = 0; index < short_by.size(); ++index) {
    if (short_by[index] <= 0) {
      continue;
    }
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const std::uint32_t days = DaysToSowingEnd(config, world, resource);
    // Ascending ids: a later id wins only on strictly more, or an equal
    // shortfall with a strictly nearer sowing.
    const bool better = best.value == kInvalidDefIdValue || short_by[index] > best_short ||
                        (short_by[index] == best_short && days < best_days);
    if (better) {
      best = resource;
      best_short = short_by[index];
      best_days = days;
    }
  }
  return best;
}

/// The seed lot's price for `grams`: the catalogue's points for the most,
/// pro-rated and rounded up (boss [2] Q3: 2 t of a 5 t, 30-point lot is 12).
std::int32_t SeedLotPoints(const LimitCatalog& catalog, std::int32_t full_points, Grams grams) {
  if (catalog.seed_lot_grams_max <= 0 || full_points <= 0 || grams <= 0) {
    return 0;
  }
  const std::int64_t scaled = static_cast<std::int64_t>(full_points) * grams;
  return static_cast<std::int32_t>((scaled + catalog.seed_lot_grams_max - 1) /
                                   catalog.seed_lot_grams_max);
}

}  // namespace

LimitLotId SeedLotOf(const LimitCatalog& catalog) {
  for (std::uint32_t row = 0; row < catalog.lots.size(); ++row) {
    if (catalog.lots[row].kind == LimitLotKind::kSeed) {
      return DefIdFromRow<LimitLotIdTag>(row);
    }
  }
  return LimitLotId{};
}

SeedLotView SeedLotTermsToday(const ProductionConfig& config,
                              const WorldState& world,
                              LimitLotId lot,
                              std::int32_t points_left) {
  SeedLotView offer;
  offer.lot = lot;
  offer.refusal = LotOrderable(config.limit, lot, world.epoch);
  if (offer.refusal != OrderRefusal::kNone) {
    return offer;
  }
  // THE LAMP'S OWN SUM (production_alarms.h, SeedShortByResource), so the lot
  // cures exactly what «Семян не хватает» names.
  std::vector<Grams> short_by = SeedShortByResource(config, world);
  // LESS WHAT IS ON THE ROAD (P0 arm p1377, o0: a 16.8 t shortfall bought as
  // three 5 t lots and 1.8 t on four days running, a 0.3 t one as 0.5 t on two
  // days, 1531 lots in 16 villages): the district's carts not yet in the
  // stores carry seed the lamp does not see yet.
  for (const LimitDeliveryRow& cart : world.limit_deliveries.rows) {
    for (std::size_t index = 0; index < cart.goods.size() && index < short_by.size(); ++index) {
      short_by[index] = std::max<Grams>(0, short_by[index] - cart.goods[index]);
    }
  }
  offer.resource = MostShortSeed(config, world, short_by);
  if (offer.resource.value == kInvalidDefIdValue) {
    // No seed short: «район даёт на сев не больше 0 т».
    offer.refusal = OrderRefusal::kRuleForbids;
    return offer;
  }
  const Grams shortfall = short_by[offer.resource.value];
  offer.grams =
      std::clamp(shortfall, config.limit.seed_lot_grams_min, config.limit.seed_lot_grams_max);
  // THE YEAR'S CAP, PER CROP (boss [2] Q4): bought by the lot this year and
  // this lot together within the crop's seed need on the order day — the
  // need with its rot to the sowing, the lamp's need.
  const std::vector<Grams> bare = SeedHeldToSowing(config, world, world.calendar.day);
  const Grams need =
      offer.resource.value < bare.size()
          ? SeedNeedWithRot(config, world, offer.resource, bare[offer.resource.value])
          : 0;
  const std::vector<Grams>& book = world.limit.seed_lot_bought;
  const Grams bought = offer.resource.value < book.size() ? book[offer.resource.value] : 0;
  const Grams cap_left = need > bought ? need - bought : 0;
  if (offer.grams > cap_left) {
    // The refusal names what the cap still allows (boss [2] Q5).
    offer.grams = cap_left;
    offer.refusal = OrderRefusal::kRuleForbids;
    return offer;
  }
  offer.points = SeedLotPoints(config.limit, config.limit.lots[lot.value].points, offer.grams);
  // The store before the points, as for any lot: a refusal costs nothing.
  if (!SomeStoreAccepts(world, config, offer.resource)) {
    offer.refusal = OrderRefusal::kNowhereToStore;
    return offer;
  }
  offer.refusal = points_left < offer.points ? OrderRefusal::kLimitShort : OrderRefusal::kNone;
  return offer;
}

SeedLotView SeedLotOfferOf(const ProductionConfig& config, const WorldState& world) {
  const LimitLotId lot = SeedLotOf(config.limit);
  if (lot.value == kInvalidDefIdValue) {
    SeedLotView none;
    none.refusal = OrderRefusal::kNoSuchSubject;
    return none;
  }
  return SeedLotTermsToday(config, world, lot, world.limit.points);
}

void BuySeedLot(const ProductionConfig& config,
                WorldState& current,
                const SeedLotView& terms,
                std::uint32_t arrive_day) {
  current.limit.points -= terms.points;
  current.ledger.current.limit_points_spent += terms.points;
  ResourceAmounts& book = current.limit.seed_lot_bought;
  if (book.size() <= terms.resource.value) {
    book.resize(static_cast<std::size_t>(terms.resource.value) + 1U, 0);
  }
  book[terms.resource.value] += terms.grams;
  LimitDeliveryRow cart;
  cart.lot = terms.lot;
  cart.arrive_day = arrive_day;
  cart.goods.assign(std::max<std::size_t>(config.feed_values.size(),
                                          static_cast<std::size_t>(terms.resource.value) + 1U),
                    0);
  cart.goods[terms.resource.value] = terms.grams;
  AppendRow(current.limit_deliveries, cart);
}

}  // namespace core
