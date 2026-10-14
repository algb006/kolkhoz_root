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
#include "core_common/day_off.h"
#include "core_common/fund_ladder.h"
#include "core_common/horse_lamp_memory.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/module_rules.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "district_limit.h"
#include "herd_floors.h"
#include "herd_life.h"
#include "herd_system.h"
#include "night_pasture.h"
#include "stock_ops.h"

namespace core {
namespace {

/// 1 + year x 12 + month of `day` (ChairmanState::hay_day_worked_month).
std::uint16_t MonthCodeOf(SimDay day) {
  return static_cast<std::uint16_t>(1U + (day / kDaysPerMonth));
}

/// Whether a meadow stands in its cut inside the cut's months.
bool AMeadowIsInItsCut(const ProductionConfig& config, const WorldState& world) {
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  if (month < config.farming.meadow_cut_month || month > HayCutLastMonth(config)) {
    return false;
  }
  return std::ranges::any_of(world.fields.rows, [](const FieldRow& field) {
    return field.kind != LandKind::kArable && field.phase == FieldPhase::kHarvest &&
           field.work_days_remaining > 0.0F;
  });
}

/// Whether this year's cut is open with grass still standing in it: a meadow
/// in its cut with mowing left, from the cut's first month on. (Not before
/// it: a meadow found in last year's cut's phase in January - a world of
/// before 0.37.93 - is no open cut, and the ladder goes on past it.)
bool GrassStandsInAnOpenCut(const ProductionConfig& config, const WorldState& world) {
  // AND NOT AFTER THE CUT'S LAST MONTH (found by thirty_years' guard on the
  // candidate, 14 October 2026): a meadow not mown through stands in its
  // cut's phase until the season closes it (the first snow, 0.37.244), the
  // cut's moves end with August - and with no upper month here the lamp
  // stood lit and silent through September and October with the ladder's
  // lower rungs shut behind this very test.
  // With the upper month it is the question above, word for word - asked
  // there, so the two cannot part again.
  return AMeadowIsInItsCut(config, world);
}

/// THE MEADOW THE AVRAL IS NAMED ON (#852; econ's complexity pass of 14
/// October 2026, Risk 1.2: «`cut_hay` names a work that is not in the list»):
/// kDeclareRush takes a field, so the advice names one - of the meadows in
/// their cut with mowing left and the avral below its last step (the door's
/// own test, CutCanBeHurried), the one with the most mowing left; ties to the
/// first in row order. Invalid when none is, which AvralMayBeNamedOnTheCut
/// has already refused.
FieldId MeadowToHurry(const WorldState& world) {
  FieldId meadow;
  float most_left = 0.0F;
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const FieldRow& field = world.fields.rows[row];
    const std::int64_t step = field.rush_phase == field.phase ? field.rush_step : 0;
    if (field.kind == LandKind::kArable || field.phase != FieldPhase::kHarvest ||
        !(field.work_days_remaining > most_left) || step >= kMaxRushStep) {
      continue;
    }
    most_left = field.work_days_remaining;
    meadow = world.fields.row_ids[row];
  }
  return meadow;
}

/// Whether an avral stands on a meadow's cut today.
bool AvralStandsOnAMeadowsCut(const WorldState& world) {
  return std::ranges::any_of(world.fields.rows, [](const FieldRow& field) {
    return field.kind != LandKind::kArable && field.phase == FieldPhase::kHarvest &&
           field.rush_phase == FieldPhase::kHarvest && field.rush_step > 0;
  });
}

}  // namespace

bool DayOffWorthWorkingForTheCut(const ProductionConfig& config,
                                 const WorldState& world,
                                 const HerdFeedForecast& forecast) {
  return forecast.short_ahead && forecast.by_pace && TodayMayBeDeclaredWorking(world) &&
         world.weather.precipitation != Precipitation::kRain && AMeadowIsInItsCut(config, world) &&
         world.chairman.hay_day_worked_month != MonthCodeOf(world.calendar.day);
}

SimDay DayOffAheadForTheCut(const ProductionConfig& config,
                            const WorldState& world,
                            const HerdFeedForecast& forecast) {
  const SimDay today = world.calendar.day;
  if (!forecast.short_ahead || !forecast.by_pace || !AMeadowIsInItsCut(config, world) ||
      world.chairman.hay_day_worked_month == MonthCodeOf(today)) {
    return 0;
  }
  // AFTER today: today's own day off is the rung above when it dawned dry,
  // and is not left when it dawned wet.
  const SimDay month_end = today - (today % kDaysPerMonth) + kDaysPerMonth - 1U;
  for (SimDay day = today + 1U; day <= month_end; ++day) {
    // THE DOOR's OWN QUESTION OF THAT DAY (#852): a holiday is a day off the
    // door will refuse on its morning, and a dated advice must not name it.
    if (DayOffMayBeDeclaredWorking(world, day)) {
      return day;
    }
  }
  return 0;
}

bool AvralMayBeNamedOnTheCut(const ProductionConfig& config, const WorldState& world) {
  if (!CutCanBeHurried(config, world) ||
      world.chairman.hay_cut_avral_days >= config.farming.hay_avral_days_a_cut) {
    return false;
  }
  const SimDay today = world.calendar.day;
  if (world.chairman.hay_day_worked_month == MonthCodeOf(today)) {
    return true;  // the month's day off is worked
  }
  // A day off still ahead in this calendar month - or today's, not yet
  // declared - comes first.
  const SimDay month_end = today - (today % kDaysPerMonth) + kDaysPerMonth - 1U;
  // A HOLIDAY IS NOT SUCH A DAY (#852): the door refuses it, the day-off rung
  // does not name it, and counted here it shut the avral's rung behind a move
  // nobody may make - the lamp lit with nothing to say.
  for (SimDay day = today; day <= month_end; ++day) {
    if (DayOffMayBeDeclaredWorking(world, day)) {
      return false;
    }
  }
  return true;
}

void SettleHayCutDay(const ProductionConfig& config, WorldState& current) {
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  if (month == config.farming.meadow_cut_month && current.calendar.date.day_in_month == 0) {
    current.chairman.hay_cut_dry_work_days = 0;  // the cut opened this morning
    current.chairman.hay_cut_avral_days = 0;
  }
  // A day an avral stands on a meadow's cut (ChairmanState::hay_cut_avral_days).
  const bool avral = AvralStandsOnAMeadowsCut(current);
  if (avral && current.chairman.hay_cut_avral_days < std::numeric_limits<std::uint16_t>::max()) {
    ++current.chairman.hay_cut_avral_days;
  }
  const SimDay today = current.calendar.day;
  const bool working = !IsDayOffIn(current, today);
  if (AMeadowIsInItsCut(config, current) && working &&
      current.weather.precipitation != Precipitation::kRain &&
      current.chairman.hay_cut_dry_work_days < std::numeric_limits<std::uint16_t>::max()) {
    ++current.chairman.hay_cut_dry_work_days;
  }
  // THE DAY OFF WORKED ON THE LAMP'S ADVICE: today was the chairman's
  // cancelled day off, and the cut's pace was short this evening - the
  // lamp's own condition. A day off cancelled for a load in a month the hay
  // is not short does not spend the month's one.
  if (current.chairman.cancelled_day_off == today && today != 0) {
    const HerdFeedForecast forecast =
        ForecastHerdFeed(config, current, false, FeedHorizon::kNearestScythes);
    if (forecast.short_ahead && forecast.by_pace) {
      current.chairman.hay_day_worked_month = MonthCodeOf(today);
    }
  }
}

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
  return StoreTypeOrItsParent(config, world, store);
}

UnitTypeId StoreTypeOrItsParent(const ProductionConfig& config,
                                const WorldState& world,
                                UnitTypeId store) {
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
  // 0. A DRY DAY OFF IN THE CUT, WORKED (lever 1; econ's four-levers §2: «a
  // quarter of the summer's days are days off, and on each a dry village
  // lays nought where it lays 27-28 t on a working day»): today is a day off
  // the door would let be declared working, no rain stops the scythes this
  // morning, the forecast reads the cut by its pace and finds it short, and
  // no day off was worked on this advice this calendar month. The door is
  // kCancelDayOff with `enable` 1, TODAY's - the day's own morning and not
  // its eve: the lamp can read today's rain, not tomorrow's, and an advice
  // the door would refuse is not given. The cut's own move rides as the
  // second.
  // BEFORE EVERY MOVE: THE AVRAL THAT HAS STOOD ITS DAYS IS LIFTED (econ's
  // four-levers §2d; boss, 13 October 2026). Capped only in its NAMING, an
  // avral once declared stood on - nine days in five villages of arm 3 - and
  // its price in rest and satisfaction ran with it. It does not end by
  // itself: the lamp says so and the chairman lifts it (kDeclareRush, 0).
  if (AvralStandsOnAMeadowsCut(world) &&
      world.chairman.hay_cut_avral_days >= config.farming.hay_avral_days_a_cut) {
    advice.advice = AlarmAdvice::kLiftRush;
    return advice;
  }
  // THE CUT'S MOVES ARE NAMED ONLY WHILE THE PACE IS THE BOUND (econ's
  // four-levers §2e; boss, 13 October 2026): with the grass the smaller term
  // the meadows end mown through, and a day off worked or an avral lays the
  // same hay earlier at the price of rest and satisfaction - in years 3-4 of
  // arm C the run's chairman worked 5-7 days off a year for hay nobody lacked.
  // A DATED DAY OFF IS NOT KEPT AGAINST THE FORECAST («a dated move stands»
  // was tried on 13 October 2026 and withdrawn by boss): with the prior
  // naming a day off at every cut's opening dawn it meant «the month's day
  // off is worked in every lit year» - 51 days off in ten years against 31.
  // THE KNOWN MISS that buys: one village-year of thirty-six (seed 1933, year
  // 3) left 11.7 ha, 20 t, uncut - rain on three of the cut's last four days
  // in a village with nine more cows to tend than its base.
  const bool pace_is_the_bound = !forecast.grass_bound;
  if (pace_is_the_bound && DayOffWorthWorkingForTheCut(config, world, forecast)) {
    advice.advice = AlarmAdvice::kDeclareDayWorking;
    return advice;
  }
  // 1. THE AVRAL ON THE CUT - ONLY AFTER THE MONTH'S DAY OFF, AND FOR A FEW
  // DAYS (econ's four-levers §2c; boss, 13 October 2026): named when the
  // month's day off has been worked on this lamp's advice or none is left in
  // the month, the forecast is still short, and the avral has stood on this
  // cut for fewer than FarmingConfig::hay_avral_days_a_cut days. Named every
  // day the lamp was lit inside the first summer's cut, it cost the villages
  // that followed it 6-8 points of rest by August's end.
  if (pace_is_the_bound && AvralMayBeNamedOnTheCut(config, world)) {
    advice.advice = AlarmAdvice::kCutHay;
    advice.advice_field = MeadowToHurry(world);
    return advice;
  }
  // WHILE A CUT IS OPEN AND GRASS STILL STANDS IN IT, THE LAMP NAMES ONLY THE
  // CUT'S OWN MOVES (boss, 13 October 2026, on lever 1's first pair): the
  // lower rungs - the heads above the floors, the granary, bought feed, the
  // remainder below the floors - are named from the day the year's cut
  // closes or its grass is mown through, on the hay actually in. The
  // ladder's first rung IS the cut, and a head handed over in June eats
  // grass, not hay, until the stall months. Lit inside year 1's cut for the
  // first time, the lamp went on down its ladder on ordinary days and the
  // run's chairman handed twenty horses over nine villages in the first
  // summer (189 -> 169). With neither move left - the avral at its last
  // step, no dry day off to work - it names none: the lamp stays lit with its
  // days and tonnes.
  // BUT A DAY OFF STILL AHEAD IN THE MONTH IS NAMED WITH ITS DATE (boss, 13
  // October 2026, on arm 3: 45-79 village-days a summer lit with no move at
  // all): the move is that day's own morning, and the lamp says so.
  if (GrassStandsInAnOpenCut(config, world)) {
    // THE GRASS IS THE BOUND AND THE HAY IS STILL SHORT: the move that is
    // true is the ladder's own «сдать головы», DATED to the day after the
    // cut's last - the day the lower rungs open by the calendar. No head is
    // named below the floors here; with none above them the lamp names none.
    if (forecast.grass_bound) {
      const HandOverAdvice heads = LeastHeadsToHandOver(config, world);
      if (heads.stock + heads.horses > 0) {
        advice.advice = AlarmAdvice::kReduceHerd;
        advice.hand_over_stock = heads.stock;
        advice.hand_over_horses = heads.horses;
        advice.advice_day = forecast.cut_end_day + 1U;
      }
      return advice;
    }
    const SimDay coming = DayOffAheadForTheCut(config, world, forecast);
    if (coming != 0) {
      advice.advice = AlarmAdvice::kDeclareDayWorking;
      advice.advice_day = coming;
    }
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
  // THE THRESHOLD INSIDE THE CUT (FarmingConfig::hay_pace_short_share): read
  // by the pace, the lamp is lit when what covers the need is under that
  // share of it. At 1.0 - the shipped value - that is any shortage at all.
  // (Two thresholds, 0.9 to light and 1.0 to go out, were tried on 13 October
  // 2026 and dropped: no lit day fewer, 3 % of the first summer's hay less.)
  if (forecast.by_pace && forecast.need_units > 0.0F &&
      forecast.covered_units >= config.farming.hay_pace_short_share * forecast.need_units) {
    return;
  }
  Alarm alarm;
  alarm.kind = AlarmKind::kHerdHayShortAhead;
  alarm.herd = forecast.herd;
  // THE LAMP NAMES THE HAY (0.37.250): the shortage is the hay ladder's. The
  // row of the feeding order found empty first rides beside it.
  alarm.resource = config.hay_resource;
  alarm.first_short_resource = forecast.first_short;
  alarm.short_grams = forecast.short_hay_grams;
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
  alarm.advice_day = advice.advice_day;
  alarm.field = advice.advice_field;
  alarm.hand_over_stock = advice.hand_over_stock;
  alarm.hand_over_horses = advice.hand_over_horses;
  alarm.below_floor_stock = advice.below_floor_stock;
  alarm.below_floor_horses = advice.below_floor_horses;
  alarm.lamp = 0;
  alarms.push_back(alarm);
}

}  // namespace core
