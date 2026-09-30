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
#include "plan_alarms.h"
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
  // THE PLAN DOOR'S RULE (0.36.23; boss-core-epoch1-resume [54]): the loan
  // lends the seed today's stores must keep — the sowings before the seed's
  // next harvest — and not next year's, which the harvest gives. As of
  // today: a loan is never taken in the turn's own hour (TakeGoodsLoan).
  //
  // AND WITH THE ROT TO THE SOWING — THE LAMP'S OWN DOOR (0.37.42; boss-core-
  // epoch1-resume-2026-09-30 [40], econ's boss-econ-loan-cap [3]). The seed
  // lamp (kSeedShort) measures the norm and what the stores' rot takes of it
  // by the sowing (SeedNeedWithRot); the ceiling lent the bare norm, so a loan
  // taken at the lamp's word left the rot's share short and the lamp burned
  // on after the cart came, with no second loan in the year: on host's novice
  // run, seed 1939's wheat, 2.52 t lent against 2.66 t short, 13 days lit.
  const std::vector<Grams> need = SeedHeldToSowing(config, current, current.calendar.day);
  const Grams bare = resource.value < need.size() ? need[resource.value] : 0;
  return bare > 0 ? SeedNeedWithRot(config, current, resource, bare) : 0;
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
    // NEXT YEAR'S POSITION KEPT BACK WHEN THE FIELDS WILL NOT PAY IT (0.36.39;
    // boss-core-epoch1-resume [99], item 2; boss-core-epoch1-queue [3]; wage
    // design, «Товарный заём», c2ba44ad): the order is seed, then this year's
    // plan and next year's, then the debt. econ (neglect-floor §10.4–10.5):
    // after a winter slot was lost, 2.72 t of rye went back on the loan at the
    // turn of year 3 and the rye of year 4 failed. Asked as the alarm asks it,
    // as of the closing year's last day — the chains are not turned yet
    // (RunYearStart). A position the chains cover is paid by next year's
    // harvest, and keeps nothing back. The first draft skipped the repayment
    // outright, and the static review found the debt growing for ever at a
    // full barn: the barn never changes the hectares.
    //
    // ONE HOLD FOR THE THREE WHO EAT IT (0.37.4; boss-core-epoch1-queue [49]
    // (а), [62]-[63]; econ-boss-rye-hold [1]): the loan keeps what the herds
    // and the people's issue keep — what the new year's own harvest will not
    // pay of its position AND of the seed the year after sows from it
    // (plan_alarms.h, NextYearUnpaidGrams), with the rot of its year in the
    // barn (district_plan.h, HeldForDeliveryGrams). The position alone
    // was kept, to the gram: in the rye's unreaped year 4 the autumn sowing
    // took its 630 kg of seed first and 417 kg of 1116 shipped (pd 0.37 on 7
    // seeds of 9, 0426fb2), and seed 1938 kept 1.12 t for 1.12 owed and
    // shipped 1.00 to the barn's rot. A position the new year's harvest pays
    // with its seed keeps nothing back, as a covered one did before.
    const Grams kept_for_plan = HeldForDeliveryGrams(
        config,
        resource,
        NextYearUnpaidGrams(config, current, resource, SeedDayAtTheTurn(current)),
        kDaysPerYear);
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
    // At the turn (RepayGoodsLoans runs in RunYearStart, before the rotation
    // turns): the seed as of the closing year's last day (SeedDayAtTheTurn).
    const Grams above_seed =
        DeliverableAboveSeed(config, current, resource, SeedDayAtTheTurn(current));
    const Grams held_back = lying + kept_for_plan;
    const Grams can_pay = above_seed > held_back ? above_seed - held_back : 0;
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

void CollectGoodsLoanAlarms(const ProductionConfig& config,
                            const WorldState& world,
                            std::vector<Alarm>& alarms) {
  for (std::size_t index = 0; index < world.plan.goods_loan_owed.size(); ++index) {
    const Grams owed = world.plan.goods_loan_owed[index];
    if (owed <= 0) {
      continue;
    }
    Alarm alarm;
    alarm.kind = AlarmKind::kGoodsLoanOwed;
    alarm.resource = DefIdFromIndex<ResourceIdTag>(index);
    alarm.amount = owed;
    // THE LAMP WHEN THE DEBT HAS CROSSED A TURN (Alarm::lamp; boss-core-
    // epoch1-queue-2026-09-29 [36]): a loan taken this year is the chairman's
    // own decision and is repaid at the turn after the plan — a line for the
    // window; what the turn could not pay carries on with the markup again
    // and grows with no ceiling, the loss the kind was written for. Carried
    // is what stands above this year's own loans with their markup (each
    // loan books its markup when taken, TakeGoodsLoan). A kilogram's slack
    // covers the rounding of the markup loan by loan. Measured before the
    // lamp (0.37.18, canon KD): 121.6 days a run with the chairman, 22.0 of
    // days 0-30.
    //
    // AND ONLY PAST THE SECOND TURN (0.37.47; boss-core-epoch1-resume-2026-
    // 09-30 [53]-[55], econ [54]). A debt carried over ONE turn is the
    // repayment's own order at work — the seed and next year's position
    // first (labor-payment §6.1, 27 September) — and the player has no move
    // against it: the lamp so lit burned all year 3 in 27 runs of 27, canon
    // and novice (0.37.45's pair). That is the book's line now. The lamp is
    // for the debt next year's harvest did not pay either: what stands above
    // this year's loans AND above last year's with their two markups (booked
    // at the loan, taken again at the turn) is older than last year.
    const Grams taken = AmountOf(world.plan.goods_loan_taken, alarm.resource);
    const Grams this_years = WithMarkup(config, taken);
    const Grams last_years = WithMarkup(
        config, WithMarkup(config, AmountOf(world.ledger.closed.goods_loan_taken, alarm.resource)));
    alarm.lamp = owed > this_years + last_years + kGramsPerKilogram ? 1U : 0U;
    // THE HARVEST THAT REPAYS IT (0.37.50; boss [66]): the coming turn's —
    // this campaign year's, or on the turn's own day before the turn has run,
    // the closing year's (DaysToPlanTurn), whose harvest the turn repays from
    // within the hour.
    const std::uint16_t year = world.calendar.date.year;
    alarm.repay_harvest_year =
        DaysToPlanTurn(world) == 0 && year > 1 ? static_cast<std::uint16_t>(year - 1) : year;
    alarms.push_back(alarm);
  }
}

}  // namespace core
