// The district's limit in the simulation (district_limit.h).

#include "district_limit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "stock_ops.h"

namespace core {
namespace {

/// The random stream the district's delays are drawn from. Its own stream id,
/// so a delay never shifts the draws of any other system of the world.
constexpr std::uint64_t kDeliveryDelayStream = 0x4C494D4954ULL;  // "LIMIT"

bool CarriesAnything(const ResourceAmounts& goods) {
  return std::any_of(goods.begin(), goods.end(), [](Grams grams) { return grams > 0; });
}

}  // namespace

OrderRefusal LotOrderable(const LimitCatalog& catalog, LimitLotId lot, Epoch epoch) {
  if (lot.value == kInvalidDefIdValue || lot.value >= catalog.lots.size()) {
    return OrderRefusal::kNoSuchSubject;
  }
  const LimitLotDef& def = catalog.lots[lot.value];
  if (def.era > static_cast<std::uint8_t>(epoch)) {
    return OrderRefusal::kGateClosed;
  }
  // Only goods are bought here (livestock, machines, people and "choice"
  // have their own windows — STUB), and only a lot with a price and at least
  // one written amount (boss, parcel 211).
  if (def.kind != LimitLotKind::kGoods || def.points < 0 || !CarriesAnything(def.goods)) {
    return OrderRefusal::kRuleForbids;
  }
  return OrderRefusal::kNone;
}

float LimitReputationMultiplier(float reputation) {
  // District design §5, band for band. The bands are the design's table and
  // not balance knobs: no row carries them, and boss named them from the
  // document (parcel 211).
  if (!(reputation > 20.0F)) {
    return 0.7F;
  }
  if (reputation <= 40.0F) {
    return 0.85F;
  }
  if (reputation <= 60.0F) {
    return 1.0F;
  }
  if (reputation <= 80.0F) {
    return 1.2F;
  }
  return 1.4F;
}

std::int32_t YearLimitPoints(const LimitCatalog& catalog,
                             FarmStatusTier tier,
                             bool plan_fully_met,
                             float overfulfil_percent,
                             float reputation) {
  const auto tier_index = static_cast<std::size_t>(tier);
  double points = tier_index < catalog.base_points.size()
                      ? static_cast<double>(catalog.base_points[tier_index])
                      : 0.0;
  if (plan_fully_met) {
    points += static_cast<double>(catalog.plan_met_points);
  }
  if (overfulfil_percent > 0.0F) {
    const double over = std::floor(static_cast<double>(overfulfil_percent)) *
                        static_cast<double>(catalog.overfulfil_points_per_percent);
    points += std::min(over, static_cast<double>(catalog.overfulfil_points_max));
  }
  points *= static_cast<double>(LimitReputationMultiplier(reputation));
  const double rounded = std::round(points);
  if (!(rounded > 0.0)) {
    return 0;
  }
  constexpr double kMostPoints = 1.0e6;
  return static_cast<std::int32_t>(std::min(rounded, kMostPoints));
}

OrderRefusal OrderLimitLot(const ProductionConfig& config,
                           WorldState& current,
                           const OrderRow& order) {
  const OrderRefusal refusal = LotOrderable(config.limit, order.lot, current.epoch);
  if (refusal != OrderRefusal::kNone) {
    return refusal;
  }
  const LimitLotDef& def = config.limit.lots[order.lot.value];
  if (current.limit.points < def.points) {
    return OrderRefusal::kLimitShort;
  }
  current.limit.points -= def.points;
  current.ledger.current.limit_points_spent += def.points;

  // THE DELAY IS DETERMINED BY THE WORLD, NOT RANDOM: the world's generator
  // state at this moment and the order's own tick pick it, so the same
  // campaign delivers the same lot on the same day on one worker and on many
  // (determinism, CLAUDE.md §10). It is the CURRENT state, not the campaign's
  // seed: any system that draws more or fewer numbers earlier in the tick
  // moves the day a later order's cart arrives. That is deterministic and is
  // not a promise that an unrelated change keeps the day (delivery analysis,
  // RACE-001 / UB-002, 2026-09-14).
  const std::uint32_t spread = config.limit.delivery_delay_days_max + 1U;
  RngState rng = SeedRngState(current.rng.state ^ order.issued_tick, kDeliveryDelayStream);
  const std::uint32_t delay = NextRandomBelow(rng, spread);

  LimitDeliveryRow cart;
  cart.lot = order.lot;
  cart.arrive_day =
      static_cast<std::uint32_t>(current.calendar.day) + config.limit.delivery_days + delay;
  cart.goods = def.goods;
  AppendRow(current.limit_deliveries, cart);
  return OrderRefusal::kNone;
}

void ArriveLimitDeliveries(const ProductionConfig& config, WorldState& current) {
  std::vector<LimitDeliveryId> emptied;
  for (std::uint32_t row = 0; row < current.limit_deliveries.rows.size(); ++row) {
    if (current.limit_deliveries.rows[row].arrive_day > current.calendar.day) {
      continue;
    }
    // By index and re-read: the door writes unit rows, not cart rows, but the
    // cart's own goods are what it subtracts from, so they are read afresh.
    for (std::size_t resource = 0; resource < current.limit_deliveries.rows[row].goods.size();
         ++resource) {
      const Grams left = current.limit_deliveries.rows[row].goods[resource];
      if (left <= 0) {
        continue;
      }
      const Grams placed =
          DeliverToStores(current, config, DefIdFromIndex<ResourceIdTag>(resource), left);
      current.limit_deliveries.rows[row].goods[resource] = left - placed;
    }
    // WHAT DID NOT FIT WAITS AT THE GATE (boss, parcel 211) and is offered
    // again at tomorrow's last tick; an empty cart leaves.
    if (!CarriesAnything(current.limit_deliveries.rows[row].goods)) {
      emptied.push_back(current.limit_deliveries.row_ids[row]);
    }
  }
  for (const LimitDeliveryId cart : emptied) {
    RemoveRow(current.limit_deliveries, cart);
  }
}

void TurnLimitYear(const ProductionConfig& config, WorldState& current, bool plan_fully_met) {
  // The closing year's book is still `current` here: the ledger turns in the
  // events slot, later in this same tick (core_world/world.cpp, RotateLedger),
  // which is also where the new year's grant is booked.
  current.ledger.current.limit_points_burned += current.limit.points;
  // STUB: the farm's status tier is kLagging until the economic readiness
  // index exists, and the overfulfilment term is zero until the core can
  // deliver above the plan (boss, parcel 211).
  current.limit.points = YearLimitPoints(config.limit,
                                         FarmStatusTier::kLagging,
                                         plan_fully_met,
                                         0.0F,
                                         current.chairman.raikom_reputation);
}

void GrantFirstLimitYear(const ProductionConfig& config, WorldState& current) {
  current.limit.points = YearLimitPoints(
      config.limit, FarmStatusTier::kLagging, false, 0.0F, current.chairman.raikom_reputation);
  current.ledger.current.limit_points_granted = current.limit.points;
}

}  // namespace core
