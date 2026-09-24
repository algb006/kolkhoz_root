// The district's goods loan (goods_loan.h). The order, the repayment at the
// turn, and the alarm; the cart is the district's (district_limit.cpp).

#include "goods_loan.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/limit_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "district_limit.h"
#include "district_plan.h"
#include "seed_room.h"
#include "stock_ops.h"

namespace core {
namespace {

/// Grams × (1 + markup), rounded to the gram. In double: a loan of tonnes
/// times a float markup would lose grams in a float.
Grams WithMarkup(const ProductionConfig& config, Grams grams) {
  const double factor = 1.0 + static_cast<double>(config.goods_loan_markup);
  return static_cast<Grams>(std::llround(static_cast<double>(grams) * factor));
}

/// Grams at `resource` in a dense vector, growing it to hold the index.
Grams& Slot(ResourceAmounts& amounts, ResourceId resource) {
  if (amounts.size() <= resource.value) {
    amounts.resize(static_cast<std::size_t>(resource.value) + 1U, 0);
  }
  return amounts[resource.value];
}

}  // namespace

Grams GoodsLoanCeiling(const ProductionConfig& config,
                       const WorldState& current,
                       ResourceId resource) {
  if (resource.value == kInvalidDefIdValue) {
    return 0;
  }
  const std::vector<Grams> need = SeedNeedByResource(config, current);
  return resource.value < need.size() ? need[resource.value] : 0;
}

OrderRefusal TakeGoodsLoan(const ProductionConfig& config,
                           WorldState& current,
                           const OrderRow& order) {
  const ResourceId resource = order.resource;
  // NOT IN THE TURN'S OWN HOUR (static review of 0.35.0): the order book is
  // read at the top of the production call, and the year's turn runs later
  // in the same call — a loan taken then would be the closing year's, marked
  // up again at once with its goods still on the cart, and its year's mark
  // cleared for a second loan. The district's books are closing: asked again
  // an hour later, it lends.
  if (current.calendar.day % kDaysPerYear == 0 && HourFromTick(current.calendar.tick) == 0U) {
    return OrderRefusal::kRuleForbids;
  }
  const Grams ceiling = GoodsLoanCeiling(config, current, resource);
  if (ceiling <= 0) {
    return OrderRefusal::kRuleForbids;  // no crop's seed, or no sowing to come
  }
  // ONE LOAN A RESOURCE A YEAR (boss seq 15): the year's mark, not the debt —
  // a loan paid off at the turn leaves nothing owed and still was this year's.
  if (AmountOf(current.plan.goods_loan_taken, resource) > 0) {
    return OrderRefusal::kRuleForbids;
  }
  // NOT TO A DEBTOR WHO OWES A SOWING ALREADY (boss, boss-core-epoch1-5 seq
  // 46, item 2; wage design §6; 0.35.9): a new loan of the resource only while
  // what is owed of it is less than one sowing's ceiling. econ's re-measure
  // found branches borrowing every year while the markup compounded, and the
  // debt ran past 70 t by year 20 with no brake at all.
  if (AmountOf(current.plan.goods_loan_owed, resource) >= ceiling) {
    return OrderRefusal::kRuleForbids;
  }
  // THE STORE BEFORE THE LOAN, as before a lot's points (boss seq 156): seed
  // no store takes would stand at the gate and be owed all the same.
  if (!SomeStoreAccepts(current, config, resource)) {
    return OrderRefusal::kNowhereToStore;
  }
  const Grams lent = order.amount > 0 && order.amount < ceiling ? order.amount : ceiling;

  LimitDeliveryRow cart;
  cart.lot = LimitLotId{};  // no lot: the district's loan, not a purchase
  cart.arrive_day = LimitCartArriveDay(config, current, order);
  Slot(cart.goods, resource) = lent;
  AppendRow(current.limit_deliveries, cart);

  Slot(current.plan.goods_loan_taken, resource) += lent;
  Slot(current.plan.goods_loan_owed, resource) += WithMarkup(config, lent);
  AddLedgerAmount(current.ledger.current.goods_loan_taken, resource, lent);
  return OrderRefusal::kNone;
}

void RepayGoodsLoans(const ProductionConfig& config, WorldState& current) {
  for (std::size_t index = 0; index < current.plan.goods_loan_owed.size(); ++index) {
    Grams& owed = current.plan.goods_loan_owed[index];
    if (owed <= 0) {
      continue;
    }
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    // FROM THE STORES ONLY, AND THE SEED STILL HELD: DeliverableAboveSeed
    // counts the heaps lying on the fields as well, and the repayment takes
    // from the stores alone — so with a heap lying and the seed in the barn,
    // the whole allowance came out of the barn's seed (static review of
    // 0.35.0). The heaps' share is taken off the allowance.
    Grams lying = 0;
    for (const FieldRow& field : current.fields.rows) {
      if (field.reaped_resource.value == resource.value && field.reaped_grams > 0) {
        lying += field.reaped_grams;
      }
    }
    const Grams above_seed = DeliverableAboveSeed(config, current, resource);
    const Grams can_pay = above_seed > lying ? above_seed - lying : 0;
    const Grams paid = TakeFromStorage(current, config, resource, owed < can_pay ? owed : can_pay);
    AddLedgerAmount(current.ledger.current.goods_loan_repaid, resource, paid);
    owed -= paid;
    // «ЕСЛИ И СЛЕДУЮЩИЙ ГОД ПЛОХОЙ — ДОЛГ НАКАПЛИВАЕТСЯ И ДУШИТ»: the unpaid rest
    // takes the markup again and carries on, with no ceiling (boss seq 15).
    if (owed > 0) {
      owed = WithMarkup(config, owed);
    }
  }
  current.plan.goods_loan_taken.assign(current.plan.goods_loan_taken.size(), 0);
}

void CollectGoodsLoanAlarms(const WorldState& world, std::vector<Alarm>& alarms) {
  for (std::size_t index = 0; index < world.plan.goods_loan_owed.size(); ++index) {
    if (world.plan.goods_loan_owed[index] <= 0) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kGoodsLoanOwed;
    alarm.resource = DefIdFromIndex<ResourceIdTag>(index);
    alarm.amount = world.plan.goods_loan_owed[index];
    alarms.push_back(alarm);
  }
}

}  // namespace core
