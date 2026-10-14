// Whose transport to the district (district_carts.h): the district's
// calendar, the plan's requests, the own delivery's row and the bonus.

#include "district_carts.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "cart_trips.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/ledger_state.h"
#include "core_common/limit_state.h"
#include "core_common/state_table_ops.h"
#include "district_plan.h"
#include "field_haul.h"
#include "stock_ops.h"

namespace core {

namespace {

/// The grams of position `index` already on an own delivery not yet carted:
/// they are owed no second time, and the stores hold them for it.
Grams OutboundPendingGrams(const WorldState& world, std::uint32_t index) {
  Grams pending = 0;
  for (const LimitDeliveryRow& row : world.limit_deliveries.rows) {
    if (row.outbound != 0 && index < row.goods.size() && row.goods[index] > 0) {
      pending += row.goods[index];
    }
  }
  return pending;
}

}  // namespace

SimDay DistrictCollectionDay(SimDay day) {
  return day - (day % kDaysPerMonth) + kDaysPerMonth - 1U;
}

bool DistrictCollectsToday(const WorldState& world) {
  return world.calendar.day == DistrictCollectionDay(world.calendar.day) && !world.weather.mud;
}

void SettleDistrictCollection(const ProductionConfig& config, WorldState& current) {
  if (!DistrictCollectsToday(current) || current.plan.requested.empty()) {
    return;
  }
  if (current.plan.delivered.size() < current.plan.due.size()) {
    current.plan.delivered.resize(current.plan.due.size(), 0);
  }
  for (std::uint32_t index = 0; index < current.plan.requested.size(); ++index) {
    Grams& asked = current.plan.requested[index];
    if (asked <= 0 || index >= current.plan.delivered.size()) {
      continue;
    }
    // WHAT LIES IN THE STORES THIS MORNING, above the seed (the order's own
    // ceiling, DeliverPlanNow): the rest of the request waits for the next
    // collection. What an own delivery already holds stays for its carters.
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const Grams wanted =
        std::min(asked,
                 DeliverableFromStores(config, current, resource, current.calendar.day) -
                     OutboundPendingGrams(current, index));
    if (wanted <= 0) {
      continue;
    }
    const Grams taken = TakeFromStorage(current, config, resource, wanted);
    current.plan.delivered[index] += taken;
    AddLedgerAmount(current.ledger.current.delivered, resource, taken);
    asked -= taken;
  }
}

OrderRefusal RequestPlanDelivery(
    const ProductionConfig& config, WorldState& current, ResourceId only, Grams amount, bool own) {
  if (current.plan.announced == 0 || current.plan.due.empty()) {
    return OrderRefusal::kNoPlanYet;
  }
  if (current.plan.delivered.size() < current.plan.due.size()) {
    current.plan.delivered.resize(current.plan.due.size(), 0);
  }
  if (current.plan.requested.size() < current.plan.due.size()) {
    current.plan.requested.resize(current.plan.due.size(), 0);
  }
  LimitDeliveryRow outgoing;
  outgoing.own_carts = 1;
  outgoing.outbound = 1;
  outgoing.arrive_day = current.calendar.day;
  outgoing.goods.assign(current.plan.due.size(), 0);
  Grams raised = 0;
  for (std::uint32_t index = 0; index < current.plan.due.size(); ++index) {
    if (only.value != kInvalidDefIdValue && index != only.value) {
      continue;
    }
    // A POSITION IS WHAT THE DISTRICT ASKED FOR (DeliverPlanNow's rule).
    if (current.plan.due[index] <= 0) {
      continue;
    }
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    // WHAT AN OWN DELIVERY ALREADY CARRIES is owed no second time: a daily
    // order would otherwise put the same grain on a row a day.
    const Grams pending = OutboundPendingGrams(current, index);
    const Grams owed = current.plan.due[index] - current.plan.delivered[index] - pending;
    if (!own) {
      // THE DISTRICT's CART: the request rises by the amount, or to what is
      // owed; it is collected on the month's last day (the mud slips it).
      const Grams more = amount > 0 ? amount : owed - current.plan.requested[index];
      if (more > 0) {
        current.plan.requested[index] += more;
        raised += more;
      }
      continue;
    }
    // OWN CARTS: what lies above the seed now goes on the outgoing row and
    // leaves the stores as the carters carry it.
    const Grams wanted =
        std::min(amount > 0 ? amount : owed,
                 DeliverableFromStores(config, current, resource, current.calendar.day) - pending);
    if (wanted > 0) {
      outgoing.goods[index] = wanted;
      raised += wanted;
      // WHAT OWN CARTS TAKE ON LEAVES THE DISTRICT's REQUEST: the same grain
      // is asked of one transport, not of both.
      current.plan.requested[index] -= std::min(current.plan.requested[index], wanted);
    }
  }
  if (raised <= 0) {
    return OrderRefusal::kRuleForbids;  // nothing owed, or nothing of it in the stores
  }
  if (own) {
    AppendRow(current.limit_deliveries, outgoing);
  }
  return OrderRefusal::kNone;
}

float LotOwnCartDays(const ProductionConfig& config, const WorldState& world, LimitLotId lot) {
  if (lot.value >= config.limit.lots.size()) {
    return 0.0F;
  }
  // The lot as its row would stand at the district centre, priced by the
  // seam's own door (cart_trips.h) at today's road, bed and horses.
  LimitDeliveryRow as_row;
  as_row.lot = lot;
  as_row.own_carts = 1;
  as_row.goods = config.limit.lots[lot.value].goods;
  Grams grams = 0;
  for (const Grams part : as_row.goods) {
    grams += part > 0 ? part : 0;
  }
  const HaulRate rate = DistrictLotHaulRate(config, world, &as_row);
  return grams > 0 && rate.load > 0 ? CartDaysFor(config, grams, rate) : 0.0F;
}

std::int32_t OwnDeliveryBonusPoints(const ProductionConfig& config,
                                    const WorldState& world,
                                    bool plan_fully_met,
                                    float overfulfil_points) {
  const YearLedger& book = world.ledger.current;
  Grams delivered = 0;
  Grams own = 0;
  for (std::size_t index = 0; index < book.delivered.size(); ++index) {
    delivered += book.delivered[index];
    own += index < book.delivered_own.size() ? book.delivered_own[index] : 0;
  }
  if (delivered <= 0 || own <= 0 || !(config.own_delivery_bonus_share > 0.0F)) {
    return 0;
  }
  // THE SHARE OWN CARTS CARRIED, of the plan's and of the above-plan tonnes
  // alike: the core does not book which tonne was above the plan, so one share
  // reads both (named simplification).
  const double share = static_cast<double>(own) / static_cast<double>(delivered);
  double points = 0.0;
  if (plan_fully_met) {
    points += static_cast<double>(config.limit.plan_met_points) * share;
  }
  points += static_cast<double>(overfulfil_points) * share;
  points *= static_cast<double>(config.own_delivery_bonus_share);
  const double rounded = std::round(points);
  return rounded > 0.0 ? static_cast<std::int32_t>(rounded) : 0;
}

}  // namespace core
