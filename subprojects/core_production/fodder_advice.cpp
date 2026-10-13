#include "herd_forecast.h"

// THE HAY LAMP'S ADVICE: the ladder of moves and the alarm that carries it.
// Moved out of herd_forecast.cpp on 13 October 2026, byte for byte - that
// file had outgrown the thousand lines a file may hold (CLAUDE.md §5).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/fund_ladder.h"
#include "core_common/horse_lamp_memory.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/module_rules.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "district_limit.h"
#include "herd_floors.h"
#include "herd_life.h"
#include "herd_system.h"
#include "night_pasture.h"
#include "stock_ops.h"

namespace core {

UnitTypeId FeedStoreTypeToBuild(const ProductionConfig& config, const WorldState& world) {
  // THE STORE: the first unit type that is a module and the home of a feed
  // some goods lot of the district carries (resource_stores.csv `storage` -
  // the granary for compound feed); a free-standing home when no module is.
  const auto is_module = [&config](std::size_t type) {
    return type < config.unit_parent.size() && config.unit_parent[type].value != kInvalidDefIdValue;
  };
  const auto home_type_of = [&config, &is_module](std::size_t resource) {
    UnitTypeId found;
    for (std::size_t type = 0; type < config.unit_types.size(); ++type) {
      const bool home =
          std::ranges::any_of(config.unit_types[type].home_of,
                              [resource](ResourceId held) { return held.value == resource; });
      if (home && (is_module(type) || found.value == kInvalidDefIdValue)) {
        found = DefIdFromIndex<UnitTypeIdTag>(type);
        if (is_module(type)) {
          return found;
        }
      }
    }
    return found;
  };
  UnitTypeId store;
  for (const LimitLotDef& lot : config.limit.lots) {
    for (std::size_t resource = 0;
         store.value == kInvalidDefIdValue && lot.kind == LimitLotKind::kGoods &&
         resource < lot.goods.size() && resource < config.feed_values.size();
         ++resource) {
      if (lot.goods[resource] > 0 && config.feed_values[resource] > 0.0F) {
        store = home_type_of(resource);
      }
    }
  }
  if (store.value >= config.unit_parent.size() ||
      config.unit_parent[store.value].value == kInvalidDefIdValue) {
    return store;
  }
  // ITS PARENT FIRST while none stands sound: the module's door refuses
  // without one (kNoParent; module_rules.h, the door's own test).
  const UnitTypeId parent = config.unit_parent[store.value];
  const bool parent_stands = std::ranges::any_of(world.units.rows, [parent](const UnitRow& unit) {
    return unit.type.value == parent.value && StandsSoundAsParent(unit);
  });
  return parent_stands ? store : parent;
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
  if (CutCanBeHurried(config, world)) {
    advice.advice = AlarmAdvice::kCutHay;
    return advice;
  }
  // THE HEADS OF «FEWER HEADS» ARE THE LEAST THAT FEED THE REST (0.37.137;
  // LeastHeadsToHandOver) — not the forecast's heads_short, which counts the
  // heads unfed on the worst day: the whole herd on the day the hay is out.
  // With that number a chairman following the advice handed over 111 heads
  // by the median for a tenth of the hay. BY THE FLOORS since 0.37.142
  // (herd_floors.h): the horses above the ploughing's, the cows above the
  // milk plan's, and what is below them named apart and in no sum.
  //
  // 2. THE HEADS ABOVE THE FLOORS, BEFORE THE DISTRICT'S FEED (0.37.142;
  // boss, boss-all-carts-carry-people-go-2026-10-02 [138]; Livestock design
  // §6, «Лестница совета лампы сена — четыре ступени»). Until then the feed
  // came first, and on nine villages of the canon the lamp of June, year 3,
  // said «buy 270 t of compound feed — every point of the year — and hand
  // five horses and nineteen cows besides»: bought feed keeping heads the
  // farm has no work or plan for. The floors say what the farm needs; what
  // stands above them goes first, and bought feed is for what is needed.
  const HandOverAdvice heads = LeastHeadsToHandOver(config, world);
  advice.hand_over_stock = heads.stock;
  advice.hand_over_horses = heads.horses;
  const std::int64_t above = heads.stock + heads.horses;
  // «HAND OVER» IS NAMED ONLY WITH A HEAD IN IT (0.37.143; host, host-boss-
  // pin-0-37-133-2026-10-02 [43]). On 0.37.142 a herd at its floors got
  // kReduceHerd with nought and nought — as the first move here, and as the
  // second behind the granary: «hand over 0 heads», an order's word with no
  // order in it, which no guard of «a move the door refuses» could catch —
  // the door is never asked. With nobody above the floors the ladder goes on
  // to the feed, and ends at «no move» with the remainder below the floors.
  if (above > 0 && heads.below_floor_stock + heads.below_floor_horses == 0) {
    advice.advice = AlarmAdvice::kReduceHerd;
    return advice;
  }
  // 3. THE FLOORS EXHAUSTED: THE DISTRICT'S FEED, IF THE DOOR TAKES THE ORDER
  // TODAY AND IT CLOSES THE SHORTAGE with every head above the floors gone —
  // THE LEAST LOTS THAT DO, found by halving (more feed never feeds fewer).
  // Until 0.37.142 the amount named was every lot the year's points cover:
  // the defect of «hand over N heads» before 0.37.137 over again. A PURCHASE
  // AFTER WHICH THE HERDS ARE STILL SHORT IS NOT NAMED AT ALL (boss [138]):
  // «buy 27 lots and hand over all the same» is advice it harms to follow.
  const HeadsHandedOver gone{.horses = heads.horses, .cows = heads.stock, .other = 0};
  const auto short_after = [&config, &world, &gone](const FeedPurchase& bought) {
    return ForecastHerdFeed(config, world, false, FeedHorizon::kNearestScythes, &bought, &gone)
        .short_ahead;
  };
  bool blocked_by_store = false;
  FeedPurchase purchase = MaximalFeedPurchase(config, world, &blocked_by_store);
  if (purchase.any && !short_after(purchase)) {
    std::uint32_t low = 0;  // short with this many lots: none is the floors exhausted
    std::uint32_t high = purchase.lots;
    while (high - low > 1) {
      const std::uint32_t middle = low + ((high - low) / 2U);
      (short_after(MaximalFeedPurchase(config, world, nullptr, middle)) ? low : high) = middle;
    }
    if (high < purchase.lots) {
      purchase = MaximalFeedPurchase(config, world, nullptr, high);
    }
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
    // The heads above the floors go all the same — the purchase is counted
    // with them gone.
    if (above > 0) {
      advice.advice_more = AlarmAdvice::kReduceHerd;
      advice.amount_more = above;
    }
    return advice;
  }
  // NO PURCHASE CLOSES IT: what would have to go below the floors is named,
  // apart (Alarm::below_floor_stock, below_floor_horses).
  advice.below_floor_stock = heads.below_floor_stock;
  advice.below_floor_horses = heads.below_floor_horses;
  // THE GRANARY, when a feed lot is refused for want of a store alone — its
  // site under way or not yet marked: the door refuses either way
  // (kNowhereToStore), and «buy» on such a day was an order refused.
  if (!purchase.any && blocked_by_store) {
    advice.advice = AlarmAdvice::kGranaryForFeed;
    advice.advice_unit_type = FeedStoreTypeToBuild(config, world);
    if (above > 0) {
      advice.advice_more = AlarmAdvice::kReduceHerd;
      advice.amount_more = above;
    }
    return advice;
  }
  // 4. FEWER HEADS: the heads above the floors, and the remainder below
  // them beside. The alarm's own `amount` is the lamp's, not the advice's.
  // NOBODY ABOVE THE FLOORS — NO MOVE (kNone): the remainder below them
  // stands in its own pair, and the chairman is told what the floors cost,
  // not told to hand over nought.
  advice.advice = above > 0 ? AlarmAdvice::kReduceHerd : AlarmAdvice::kNone;
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
  alarm.days_to_loss = forecast.days_ahead;
  const FodderAdvice advice = AdviseOnShortFodder(config, world, forecast);
  alarm.advice = advice.advice;
  alarm.advice_resource = advice.advice_resource;
  alarm.advice_amount = advice.advice_amount;
  alarm.advice_unit_type = advice.advice_unit_type;
  alarm.advice_more = advice.advice_more;
  alarm.amount_more = advice.amount_more;
  alarm.hand_over_stock = advice.hand_over_stock;
  alarm.hand_over_horses = advice.hand_over_horses;
  alarm.below_floor_stock = advice.below_floor_stock;
  alarm.below_floor_horses = advice.below_floor_horses;
  alarm.lamp = 0;
  alarms.push_back(alarm);
}

}  // namespace core
