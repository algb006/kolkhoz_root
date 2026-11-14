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

/// The chairman's avatar's closed day-off years (avatars.csv; #635): the
/// door's own number, so the lamp never names a day the door refuses.
std::uint8_t ClosedDayOffYears(const ProductionConfig& config, const WorldState& world) {
  return AvatarOf(config.avatars, world.chairman.avatar).day_off_working_closed_years;
}

/// THE LEAST LOTS THAT CLOSE THE SHORTAGE, found by halving (more feed never
/// feeds fewer): `maximal` itself when it is the least, or the purchase of
/// fewer lots taken in the same order. `short_after` answers whether the
/// herds are still short after a purchase; `maximal` must close it.
template <typename ShortAfter>
FeedPurchase LeastClosingPurchase(const ProductionConfig& config,
                                  const WorldState& world,
                                  const FeedPurchase& maximal,
                                  ShortAfter short_after) {
  std::uint32_t low = 0;  // short with this many lots
  std::uint32_t high = maximal.lots;
  while (high - low > 1) {
    const std::uint32_t middle = low + ((high - low) / 2U);
    (short_after(MaximalFeedPurchase(config, world, nullptr, middle)) ? low : high) = middle;
  }
  return high < maximal.lots ? MaximalFeedPurchase(config, world, nullptr, high) : maximal;
}

/// The feed a purchase brings most of, by feed units, and its grams: the
/// lamp names one (Alarm::advice_resource, advice_amount).
void NameTheFeedBought(const ProductionConfig& config,
                       const FeedPurchase& purchase,
                       FodderAdvice& advice) {
  float most_units = 0.0F;
  for (std::size_t resource = 0; resource < purchase.goods_kg.size(); ++resource) {
    const float units = purchase.goods_kg[resource] * config.feed_values[resource];
    if (units > most_units) {
      most_units = units;
      advice.advice_resource = DefIdFromIndex<ResourceIdTag>(resource);
      advice.advice_amount = GramsFromKilograms(purchase.goods_kg[resource]);
    }
  }
}

/// THE DOSE OF ONE ADVICE (#1573; FarmingConfig::hay_handover_dose_share):
/// of the least heads that feed the rest, this share, ROUNDED UP - one head
/// above the floors is still named, not rounded to none.
std::int64_t HandOverDose(const ProductionConfig& config, std::int64_t least) {
  if (least <= 0) {
    return 0;
  }
  const double dosed = std::ceil(static_cast<double>(least) *
                                 static_cast<double>(config.farming.hay_handover_dose_share));
  return std::clamp(static_cast<std::int64_t>(dosed), std::int64_t{1}, least);
}

/// Whether the shortage is within the heads rung's tolerance (#1573;
/// FarmingConfig::hay_handover_min_short_days): short on that many forecast
/// days or fewer, and no head dying of it - no head is named.
bool HandOverTolerated(const ProductionConfig& config, const HerdFeedForecast& forecast) {
  return !forecast.death_ahead && forecast.short_days <= config.farming.hay_handover_min_short_days;
}

}  // namespace

bool DayOffWorthWorkingForTheCut(const ProductionConfig& config,
                                 const WorldState& world,
                                 const HerdFeedForecast& forecast) {
  return forecast.short_ahead && forecast.by_pace &&
         TodayMayBeDeclaredWorking(world, ClosedDayOffYears(config, world)) &&
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
    if (DayOffMayBeDeclaredWorking(world, day, ClosedDayOffYears(config, world))) {
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
  // The student's closed years (#635) leave no such day: his avral's rung
  // opens where another chairman's day-off rung would stand.
  for (SimDay day = today; day <= month_end; ++day) {
    if (DayOffMayBeDeclaredWorking(world, day, ClosedDayOffYears(config, world))) {
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
    current.chairman.hay_cut_crew = 0;
    current.chairman.hay_cut_mower_days = 0.0F;
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
    // The day's crew (#1308): a counted noon is a dry working one, so its
    // crew is the cut's crew ahead - nought included: a cut walked off stays
    // walked off through the rain that follows, until a crew is placed again
    // (econ, boss-econ-1308-year1-pace [10]).
    const std::uint16_t crew = MeadowMowersNow(current);
    current.chairman.hay_cut_crew = crew;
    current.chairman.hay_cut_mower_days += static_cast<float>(crew);
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
  // ONLY A TYPE AN ORDER BUILDS in this era (UnitTypeDef::OrderableInEra):
  // the store lamp's advice named the church store until 0.37.261 by the
  // same kind of search (host-core-936-build-store-unbuildable-2026-10-16),
  // and this one would have too the day the church store held a feed.
  const auto world_era = static_cast<std::uint8_t>(world.epoch);
  const auto home_type_of = [&config, &is_module, world_era](std::size_t resource) {
    UnitTypeId found;
    for (std::size_t type = 0; type < config.unit_types.size(); ++type) {
      const bool home =
          std::ranges::any_of(config.unit_types[type].home_of,
                              [resource](ResourceId held) { return held.value == resource; });
      if (!config.unit_types[type].OrderableInEra(world_era)) {
        continue;
      }
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
                                 const HerdFeedForecast& forecast,
                                 const std::vector<float>* held) {
  FodderAdvice advice;
  if (!forecast.short_ahead) {
    return advice;
  }
  // ONE STOCK FOR EVERY PROBE BELOW (herd_forecast.h, ForecastHerdFeed's `held`).
  const std::vector<float> own =
      held != nullptr ? std::vector<float>{} : HerdFeedHeldKg(config, world);
  const std::vector<float>& stock = held != nullptr ? *held : own;
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
    // The heads rung's tolerance and dose hold here too (#1573): it is the
    // same rung, dated.
    if (forecast.grass_bound) {
      if (HandOverTolerated(config, forecast)) {
        advice.tolerated = true;
        return advice;
      }
      const HandOverAdvice heads = LeastHeadsToHandOver(config, world, &stock);
      if (heads.stock + heads.horses > 0) {
        advice.advice = AlarmAdvice::kReduceHerd;
        advice.hand_over_stock = HandOverDose(config, heads.stock);
        advice.hand_over_horses = HandOverDose(config, heads.horses);
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
  //
  // 2. BUT A CHEAP PURCHASE GOES BEFORE THE HEADS (#1573; boss, 5 November
  // 2026, on econ's hay-lamp-feed-before-cows-2026-11-05.md; Livestock
  // design, the ladder's rung 2): the least purchase that closes the
  // shortage WITH NO HEAD HANDED, when it costs no more than
  // FarmingConfig::hay_feed_before_heads_points of the year's points (the
  // door counts only the points there are). «Buy 10 t, keep the cows», no
  // head named. On the counterfactual pair of #1570 a kolkhoz obeying the
  // lamp handed 35 cows a village over years 2-3 for a shortage one lot
  // closed; one that kept them lost 4.4 days of feed and 0.6 of a head, and
  // milked 16 % more. A dear shortage - 2 October's 26 lots - still goes to
  // the heads.
  if (config.farming.hay_feed_before_heads_points > 0) {
    const auto short_after_kept = [&config, &world, &stock](const FeedPurchase& bought) {
      return ForecastHerdFeed(
                 config, world, false, FeedHorizon::kNearestScythes, &bought, nullptr, &stock)
          .short_ahead;
    };
    // THE LARGEST PURCHASE WITHIN THE PRICE FIRST, with no forecast (7 Nov
    // 2026: thirty_years' session day cost 2.10 s against main's 1.65 s when
    // the rung halved down from the year's largest purchase, a forecast a
    // probe, at every refresh). The lots are taken in one order, so each
    // purchase is the one before it and a lot more: if the dearest within
    // the price does not close the shortage, no cheaper one does, and one
    // forecast says so.
    const auto within_price = [&config](const FeedPurchase& purchase) {
      return purchase.any && purchase.points <= config.farming.hay_feed_before_heads_points;
    };
    FeedPurchase affordable = MaximalFeedPurchase(config, world, nullptr, 1U);
    if (within_price(affordable)) {
      for (std::uint32_t lots = 2;; ++lots) {
        FeedPurchase more = MaximalFeedPurchase(config, world, nullptr, lots);
        if (!within_price(more) || more.lots <= affordable.lots) {
          break;
        }
        affordable = std::move(more);
      }
      if (!short_after_kept(affordable)) {
        const FeedPurchase cheapest =
            LeastClosingPurchase(config, world, affordable, short_after_kept);
        advice.advice = AlarmAdvice::kBuyFeed;
        NameTheFeedBought(config, cheapest, advice);
        return advice;
      }
    }
  }
  // 3. THE HEADS, NOT FOR A FEW SHORT DAYS (#1573, rung 3's tolerance): with
  // the forecast short on hay_handover_min_short_days days or fewer and no
  // head dying of it, the lamp stays lit with its days and tonnes and names
  // no move - «a few short days in a late spring is what a farmer feeds
  // through» (econ §3). NOR THE DEAR FEED BELOW: a purchase over the cheap
  // rung's price for those days is the dearest cure for the least harm.
  // The rungs below closed with it: core's reading of «heads only if»,
  // confirmed by boss on 7 November 2026 (core-boss-1573-hay-rungs [2]).
  if (HandOverTolerated(config, forecast)) {
    advice.tolerated = true;
    return advice;
  }
  const HandOverAdvice heads = LeastHeadsToHandOver(config, world, &stock);
  // THE DOSE (#1573): no more than hay_handover_dose_share of them in one
  // advice, rounded up; the forecast is read again next evening, and a lot
  // or a cut may yet make the rest needless. The purchase below is still
  // counted with ALL of them gone.
  advice.hand_over_stock = HandOverDose(config, heads.stock);
  advice.hand_over_horses = HandOverDose(config, heads.horses);
  const std::int64_t above = advice.hand_over_stock + advice.hand_over_horses;
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
  const auto short_after = [&config, &world, &gone, &stock](const FeedPurchase& bought) {
    return ForecastHerdFeed(
               config, world, false, FeedHorizon::kNearestScythes, &bought, &gone, &stock)
        .short_ahead;
  };
  bool blocked_by_store = false;
  FeedPurchase purchase = MaximalFeedPurchase(config, world, &blocked_by_store);
  if (purchase.any && !short_after(purchase)) {
    purchase = LeastClosingPurchase(config, world, purchase, short_after);
    advice.advice = AlarmAdvice::kBuyFeed;
    NameTheFeedBought(config, purchase, advice);
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
  // AND WITH THE MAXIMAL PURCHASE COUNTED (#1689): the least heads below the
  // floors - cows first, then horses, as the pair above is ordered - that
  // must go for the rest to be fed WITH every lot the points buy, found by
  // halving (more heads gone never eat more). The pair above counts no
  // purchase; the lamp's line «комбикорм и солома этого не закроют» names
  // this one. Nought when nothing is buyable (the pair is then the answer).
  if (purchase.any) {
    const std::int64_t below = heads.below_floor_stock + heads.below_floor_horses;
    const auto short_with = [&](std::int64_t k) {
      const std::int64_t cows = std::min(k, heads.below_floor_stock);
      const HeadsHandedOver more{
          .horses = heads.horses + (k - cows), .cows = heads.stock + cows, .other = 0};
      return ForecastHerdFeed(
                 config, world, false, FeedHorizon::kNearestScythes, &purchase, &more, &stock)
          .short_ahead;
    };
    std::int64_t low = 0;
    std::int64_t high = below;
    while (low < high) {
      const std::int64_t mid = low + ((high - low) / 2);
      if (short_with(mid)) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    advice.below_floor_after_feed = low;
  }
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
  // ONE STOCK FOR THE LAMP AND ITS LADDER (ForecastHerdFeed's `held`).
  const std::vector<float> stock = HerdFeedHeldKg(config, world);
  const HerdFeedForecast forecast = ForecastHerdFeed(
      config, world, false, FeedHorizon::kNearestScythes, nullptr, nullptr, &stock);
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
  const FodderAdvice advice = AdviseOnShortFodder(config, world, forecast, &stock);
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
  alarm.below_floor_after_feed = advice.below_floor_after_feed;  // #1689
  alarm.advice_tolerated = advice.tolerated ? 1U : 0U;
  // THE HEADS ABOVE THE PLANS FIRST (#1289): named whenever the advice asks
  // for heads at all.
  if (alarm.advice == AlarmAdvice::kReduceHerd || alarm.advice_more == AlarmAdvice::kReduceHerd) {
    for (const HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.plan_heads != kNoHerdPlan &&
          TotalHeads(herd) > herd.plan_heads) {
        alarm.above_plan += TotalHeads(herd) - herd.plan_heads;
      }
    }
  }
  alarm.lamp = 0;
  alarms.push_back(alarm);
}

}  // namespace core
