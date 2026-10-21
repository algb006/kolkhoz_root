// The district's milk cart (core_production/milk_cart.h).

#include "milk_cart.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "cart_trips.h"
#include "core_common/calendar.h"
#include "core_common/hauled_book.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "district_carts.h"
#include "haul_ways.h"
#include "stock_ops.h"
#include "trip_terms_writer.h"

namespace core {
namespace {

/// The milk leg's receiver (MR.1; boss, the #1056 thread [101], [103]): the
/// first built store that takes milk and HOUSES NO LIVESTOCK - the church
/// store, never a cattle yard's room. Until MR.2 it was «no kolkhoz herd
/// standing there», and an empty yard just built passed that test, so the
/// sheds' legs could unload into a room (econ's suspicion, boss-econ-mr1-red
/// seq 2). kNoRow when there is none.
std::uint32_t MilkLegReceiverRow(const ProductionConfig& config, const WorldState& world) {
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (!NumberedStoreAccepts(unit, config, config.milk_resource) ||
        unit.type.value >= config.unit_types.size()) {
      continue;
    }
    if (!(config.unit_types[unit.type.value].LivestockCapacityHeadAt(unit.level) > 0.0F)) {
      return row;
    }
  }
  return kNoRow;
}

/// Whether a kind gives milk (livestock.csv milk_l_per_year).
bool KindGivesMilk(const ProductionConfig& config, LivestockKindId kind) {
  return kind.value < config.livestock.size() &&
         config.livestock[kind.value].milk_l_per_year > 0.0F;
}

/// True when `herd` gives no milk and another kolkhoz herd that does stands in
/// its barn: that herd carries the barn's leg (#1117 item 3).
bool YieldsTheLegToMilkedHerd(const ProductionConfig& config,
                              const WorldState& world,
                              const HerdRow& herd) {
  if (KindGivesMilk(config, herd.kind)) {
    return false;
  }
  return std::ranges::any_of(world.herds.rows, [&](const HerdRow& other) {
    return other.household_owned == 0 && other.unit.value == herd.unit.value &&
           KindGivesMilk(config, other.kind);
  });
}

/// Books `grams` of milk as delivered to the district: against the position
/// or outside any, and in the year's ledger.
void BookDelivered(const ProductionConfig& config,
                   WorldState& current,
                   Grams grams,
                   bool against_position) {
  if (grams <= 0) {
    return;
  }
  AddToStock(against_position ? current.plan.delivered : current.plan.delivered_outside,
             config.milk_resource,
             grams);
  AddLedgerAmount(current.ledger.current.delivered, config.milk_resource, grams);
  if (against_position) {
    LowerPlanRequest(current, config.milk_resource, grams);
  }
}

/// THE BARNS' MILK (#1056 C1.1; FarmingConfig::milk_at_barn): under the switch
/// the kolkhoz herds' milk lies in their barns, which are not stores
/// (IsTakenFrom answers no), so the cart reads them first and the stores
/// after. Nothing without the switch: no barn holds milk then.
Grams TakeMilkFromBarns(WorldState& current, const ProductionConfig& config, Grams wanted) {
  if (config.farming.milk_at_barn < 0.5F) {
    return 0;
  }
  Grams taken = 0;
  for (UnitRow& unit : current.units.rows) {
    if (taken >= wanted) {
      break;
    }
    if (!IsTakenFrom(unit, config)) {
      taken += TakeFromUnit(unit, config.milk_resource, wanted - taken);
    }
  }
  return taken;
}

/// The cart's take: the barns first, then the stores (TakeFromStorage).
Grams TakeMilkForCart(WorldState& current, const ProductionConfig& config, Grams wanted) {
  const Grams from_barns = TakeMilkFromBarns(current, config, wanted);
  return from_barns + TakeFromStorage(current, config, config.milk_resource, wanted - from_barns);
}

}  // namespace

bool MilkPositionStands(const ProductionConfig& config, const WorldState& current) {
  return current.plan.announced != 0 && config.milk_resource.value < current.plan.due.size() &&
         current.plan.due[config.milk_resource.value] > 0 &&
         current.calendar.day % kDaysPerYear >= MilkSeasonFirstDay();
}

std::uint32_t MilkSeasonFirstDay() {
  return static_cast<std::uint32_t>(Month::kMarch) * kDaysPerMonth;
}

void ShipMilkLeftover(const ProductionConfig& config, WorldState& current) {
  if (config.milk_resource.value == kInvalidDefIdValue) {
    return;
  }
  // Every gram: the barns' (under milk_at_barn, what the leg did not carry)
  // and the stores' - nothing stays overnight.
  const Grams from_barns = TakeMilkFromBarns(current, config, std::numeric_limits<Grams>::max());
  const Grams left = TakeableGrams(current, config, config.milk_resource);
  const Grams taken = from_barns + TakeFromStorage(current, config, config.milk_resource, left);
  // OVER THE PLAN, ALWAYS (the milk with debt, boss seq 26/28). What the
  // issue left used to go against the position while one stood. That
  // quietly made up the short days, so the position read met while the
  // cart's own days were short, and the book and the verdict read milk
  // through two different doors. Only the cart's share and its debt
  // (ShipMilkShare) count against the position now.
  BookDelivered(config, current, taken, false);
}

void ShipMilkShare(const ProductionConfig& config, WorldState& current) {
  if (config.milk_resource.value == kInvalidDefIdValue || current.plan.milk_daily_share <= 0 ||
      !MilkPositionStands(config, current)) {
    return;
  }
  // THE SHARE AND THE DEBT (PlanState::milk_debt): the day's share plus
  // what earlier short days still owe, the debt first because it is older.
  // A short day's difference joins the debt.
  const Grams wanted = current.plan.milk_daily_share + current.plan.milk_debt;
  const Grams taken = TakeMilkForCart(current, config, wanted);
  BookDelivered(config, current, taken, true);
  current.plan.milk_debt = wanted - taken;
  // THE BOOK SEES IT (boss seq 7 of epoch1-5): the debt of today, and at the
  // turn what was left unpaid (YearLedger::milk_debt).
  current.ledger.current.milk_debt = current.plan.milk_debt;
}

namespace {

/// THE MILK LEG GOES ON FOOT (#1056 C1.3; econ's §C1.3 (ii-b), boss's word
/// after seq 15; transport design §2 «Как носят», «бидон в руке»): the cans
/// in hand, a walker's trip by the milk's form (haul.h, WalkerLoadGrams) -
/// whether or not a horse is free. The C1 arm carted it and the year's team
/// went short of its ration on 23 % more village-days (515 -> 633, novice
/// 27 x 3): 9 170 horse-hours the oat plan did not price. THE CART ONLY
/// WHERE THE WALK IS REFUSED: the standard day holds fewer round trips of
/// it than labor.csv `walker_min_trips_per_day` - the placement's own
/// threshold for a walker (assignment.h). Every epoch for now: Epoch I is
/// the only one built.
HaulRate MilkLegRate(
    const ProductionConfig& config, const WorldState& world, Vec2 barn, Vec2 store, bool& on_foot) {
  const HaulRate walked = WalkRateToward(config, world, barn, store, config.milk_resource);
  const float trips = walked.round_trip_hours > 0.0F
                          ? config.standard_day_hours / walked.round_trip_hours
                          : std::numeric_limits<float>::infinity();
  on_foot = !(config.walker_min_trips_per_day > 0.0F) || trips >= config.walker_min_trips_per_day;
  return on_foot ? walked
                 : RateToward(config, world, barn, store, TravelMode::kCart, config.milk_resource);
}

}  // namespace

void PriceMilkLegs(const ProductionConfig& config, WorldState& current) {
  const bool on =
      config.farming.milk_at_barn >= 0.5F && config.milk_resource.value != kInvalidDefIdValue;
  std::vector<std::uint32_t> barns_priced;
  for (HerdRow& herd : current.herds.rows) {
    // What the morning's crew already drained off yesterday evening's
    // estimate (SettleMilkLegs) stays done.
    const float done = herd.leg_days_written > herd.leg_days_remaining
                           ? herd.leg_days_written - herd.leg_days_remaining
                           : 0.0F;
    herd.leg_days_remaining = 0.0F;
    herd.leg_days_written = 0.0F;
    herd.leg_days_priced = 0.0F;
    herd.leg_resource = ResourceId{};
    herd.leg_on_foot = 0;
    if (!on || herd.household_owned != 0) {
      continue;
    }
    const std::uint32_t barn_row = FindRow(current.units, herd.unit);
    // ONE LEG A BARN: two milked herds in one barn share its one stock. The
    // leg is the BARN's, written on its first MILKED kolkhoz herd: a herd of
    // a kind that gives no milk yields the leg to one that does in the same
    // barn (#1117 item 3; boss, the #1056 thread [119]: before it the leg sat
    // on the horses' row while the cows stood billeted in the stable, and a
    // per-herd print read it as «horses»). With no milked herd left there -
    // the cows' yard stood this morning - the barn's leftover milk stays on
    // its first herd, as before.
    if (barn_row == kNoRow || std::ranges::find(barns_priced, barn_row) != barns_priced.end() ||
        YieldsTheLegToMilkedHerd(config, current, herd)) {
      continue;
    }
    const UnitRow& barn = current.units.rows[barn_row];
    // A HERD IN ITS OWN YARD HAS NO LEG (MR.1): it milks into the room.
    if (IsOwnMilkRoom(barn, config)) {
      continue;
    }
    const Grams milk = UnreservedOf(barn, config.milk_resource);
    const std::uint32_t store_row = MilkLegReceiverRow(config, current);
    if (milk <= 0 || store_row == kNoRow) {
      continue;
    }
    barns_priced.push_back(barn_row);
    // THE STORE THE LEG CARRIES TO (boss, the #1056 thread [8], [101]): the
    // first that takes milk and is no herd's own room - the church store.
    const Vec2 store = current.units.rows[store_row].position;
    bool on_foot = true;
    const HaulRate rate = MilkLegRate(config, current, barn.position, store, on_foot);
    herd.leg_on_foot = on_foot ? 1U : 0U;
    herd.leg_resource = config.milk_resource;
    herd.leg_to = rate.receiver;
    herd.leg_days_written = CartDaysFor(config, milk, rate);
    herd.leg_days_priced = herd.leg_days_written;
    // And the leg's trip facts, from the same rate (#1056 C3; trip_terms_writer.h).
    WorkAssignment leg;
    leg.kind = WorkKind::kHauling;
    leg.herd = current.herds.row_ids[static_cast<std::size_t>(&herd - current.herds.rows.data())];
    WriteTripTermsFor(config, current, LogisticsLoadKind::kMilkLeg, leg, barn.position, rate, milk);
    herd.leg_days_remaining = std::max(herd.leg_days_written - done, 0.0F);
  }
}

void SettleMilkLegs(const ProductionConfig& config, WorldState& current, bool evening) {
  for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
    HerdRow& herd = current.herds.rows[row];
    if (herd.leg_resource.value == kInvalidDefIdValue) {
      continue;
    }
    const std::uint32_t barn_row = FindRow(current.units, herd.unit);
    if (barn_row == kNoRow) {
      continue;
    }
    UnitRow& barn = current.units.rows[barn_row];
    const float done = herd.leg_days_written > herd.leg_days_remaining
                           ? herd.leg_days_written - herd.leg_days_remaining
                           : 0.0F;
    float converted = 1.0F;
    if (done > 0.0F && herd.leg_days_written > 0.0F) {
      const float share = std::min(done / herd.leg_days_written, 1.0F);
      const Grams lying = UnreservedOf(barn, herd.leg_resource);
      const Grams budget = GramsFromFloat(static_cast<float>(lying) * share);
      // INTO THE LEG'S OWN RECEIVER (MR.1): the door would pick any milk
      // store, another herd's room among them - a move no cart made.
      const std::uint32_t store_row = MilkLegReceiverRow(config, current);
      Grams moved = 0;
      if (store_row != kNoRow) {
        UnitRow& store = current.units.rows[store_row];
        const Grams room = FreeRoomGrams(store, config);
        moved = std::min(budget, room > 0 ? room : 0);
        AddToStock(store.stock, herd.leg_resource, moved);
      }
      AddToStock(barn.stock, herd.leg_resource, -moved);
      AddLedgerAmount(current.ledger.current.hauled_to_stores, herd.leg_resource, moved);
      // The carrier the leg was priced with (C1.3): a walk books no cart run.
      if (herd.leg_on_foot == 0) {
        const HaulRate rate = RateToward(
            config, current, barn.position, herd.leg_to, TravelMode::kCart, herd.leg_resource);
        BookCartRun(config, current, CartLoadSource::kStore, rate, moved);
      }
      // Who carried it (#671's instrument; hauled_book.h).
      WorkAssignment hauled;
      hauled.kind = WorkKind::kHauling;
      hauled.herd = current.herds.row_ids[row];
      BookHauled(current.ledger.current,
                 herd.leg_resource,
                 moved,
                 FootShareOf(current, hauled, SettlementHasCarts(current, config.horse_kind)));
      if (budget > 0) {
        converted = std::min(static_cast<float>(moved) / static_cast<float>(budget), 1.0F);
      }
      if (herd.leg_on_foot == 0) {  // U's numerator (#1056 C3)
        BookPaidTrips(
            config,
            current,
            CartLoadSource::kStore,
            RateToward(
                config, current, barn.position, herd.leg_to, TravelMode::kCart, herd.leg_resource),
            done * converted);
      }
      SpendHaulNotes(current, hauled, converted);
    }
    // The drained and delivered is settled; what the door did not take stays
    // drained for the next hour (as DeliverDrained keeps it).
    herd.leg_days_written = herd.leg_days_remaining + done * (1.0F - converted);
    if (evening) {
      // THE DAY'S LEG CLOSES and TOMORROW'S IS ESTIMATED: what is left in the
      // barn goes with the cart before the next milking (ShipMilkLeftover),
      // and tomorrow's leg is written now at today's price - the morning's
      // placement (hour 0, before the milking) crews a level-0 leg it can see,
      // where a leg priced only after the milking was left to the hour-1
      // top-up's idle hands (the first arm: 21-28 % of the milk left in the
      // barn on working days, 0.2-0.5 % on days off). The milking re-prices
      // it to the day's milk (PriceMilkLegs), keeping what was drained.
      herd.leg_days_remaining = herd.leg_days_priced;
      herd.leg_days_written = herd.leg_days_priced;
    }
  }
}

void CollectMilkDebtAlarms(const ProductionConfig& config,
                           const WorldState& world,
                           std::vector<Alarm>& alarms) {
  if (world.plan.milk_debt <= 0 || !MilkPositionStands(config, world)) {
    return;
  }
  // A DEBT STANDING MEANS THE CART TOOK EVERY LITRE: it is what the stores
  // could not give at the last milking, so nothing was left for the yards'
  // morning issue.
  Alarm alarm;
  alarm.kind = AlarmKind::kMilkAllToDebt;
  alarm.resource = config.milk_resource;
  alarm.amount = world.plan.milk_debt;
  alarms.push_back(alarm);
}

}  // namespace core
