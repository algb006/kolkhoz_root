// The herd day (herd_system.h).
//
// Everything here is a COHORT flow: the row holds counts by rung, never a
// list of animals, so aging, birth and culling are deterministic fractional
// streams with a carry (mobs canon). A herd that matures one head every
// three days matures exactly one head every three days, not zero forever.

#include "herd_system.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/fund_ladder.h"
#include "core_common/herd_age_band.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "district_plan.h"
#include "herd_life.h"
#include "livestock_homes.h"
#include "night_pasture.h"
#include "plan_alarms.h"
#include "seed_room.h"
#include "stable_horses.h"
#include "stock_ops.h"

namespace core {
namespace {

/// Feed units a day's feeding may fall short of the need and still count as
/// fed: the need is a float and the take whole grams (RunFeeding's share).
constexpr float kFeedToleranceUnits = 0.001F;

HerdPlace PlaceOf(WorldState& world, const ProductionConfig& config, const HerdRow& herd) {
  HerdPlace place;
  // OWNERSHIP decides the purse, not the address. A kolkhoz cow billeted at
  // a yard still eats the farm's fodder and still gives the farm its milk;
  // only the family's own goat lives out of the family's pantry.
  if (herd.household_owned == 0) {
    place.at_unit = true;
    // A kolkhoz herd with no unit of its own — the sixteen start horses,
    // standing in private yards until the farm yard is built — still eats
    // the farm's fodder, and the farm's fodder is at the stock yard where
    // the cut delivers it (stock_ops.h, FindStockYardRow). Without this
    // fallback they never saw a blade of hay.
    const std::uint32_t unit_row = herd.unit.value == kInvalidEntityIdValue
                                       ? FindStockYardRow(world, config)
                                       : FindRow(world.units, herd.unit);
    if (unit_row != kNoRow) {
      place.unit = &world.units.rows[unit_row];
    }
    return place;
  }
  const std::uint32_t family_row = FindRow(world.families, herd.household);
  if (family_row != kNoRow) {
    place.pantry = &world.families.rows[family_row].pantry;
  }
  return place;
}

/// Only the KOLKHOZ herds' fodder is booked as `feed`; what a family's goat
/// eats out of the family's pantry is `yard_feed` (save 70). This comment said
/// until 2026-09-19 that the pantry side was «already counted as `eaten` and
/// `issued`» — it was not: `eaten` is the family meal and `issued` a transfer
/// into the pantry, and the book's balance found the goats' hay in no column.
Grams TakeFeed(WorldState& world,
               const ProductionConfig& config,
               const HerdPlace& place,
               ResourceId resource,
               Grams wanted,
               bool reserve_feed = false) {
  if (place.pantry != nullptr) {
    // Booked in its own column (ledger_state.h, yard_feed): the family's
    // `eaten` is the family meal and does not hold a goat's hay.
    const Grams eaten = TakeFromAmounts(*place.pantry, resource, wanted);
    AddLedgerAmount(world.ledger.current.yard_feed, resource, eaten);
    return eaten;
  }
  if (!place.at_unit) {
    return 0;
  }
  // BELOW THE PLAN RESERVE, and not through it (the seed rung is left out and
  // FeedAllowance says why). Until 2026-09-13 the
  // herds took straight out of the stores, so in a year with no oat harvest
  // the horses ate the grain owed to the district and the plan fell short —
  // seven failed years of twenty on the canonical seed, every third year —
  // against the ladder's own line: "в плохой год район забирает первым, и
  // лошадь худеет раньше, чем срывается сдача" (resources design §6).
  if (place.feed_allowance != nullptr) {
    Grams allowed =
        resource.value < place.feed_allowance->size() ? (*place.feed_allowance)[resource.value] : 0;
    // A RESERVE FEED TAKES NONE OF THE PEOPLE'S FOOD (0.37.5; resources
    // design §6; PeoplesFoods): the horse's reserve barley is the people's
    // bread, and the herds ate 24.5 t of it on seed 1931 in three years once
    // the rungs held the oats, against 9.0 t before.
    if (reserve_feed && place.peoples_foods != nullptr &&
        resource.value < place.peoples_foods->size() &&
        (*place.peoples_foods)[resource.value] != 0) {
      allowed = 0;
    }
    wanted = wanted < allowed ? wanted : allowed;
  }
  Grams taken = place.unit != nullptr ? TakeFromUnit(*place.unit, resource, wanted) : 0;
  // Then the MANGER, and only then the general store. The hay of the whole
  // farm is delivered to one stock yard (stock_ops.h, FindStockYardRow), and
  // a herd standing at some OTHER barn — the team, once the kolkhoz yard is
  // raised — would otherwise starve two hundred metres from it. Phase 1 has
  // no logistics: everything the settlement holds is reachable, and this is
  // that stub said out loud.
  if (taken < wanted) {
    const std::uint32_t manger = FindStockYardRow(world, config);
    if (manger != kNoRow && &world.units.rows[manger] != place.unit) {
      taken += TakeFromUnit(world.units.rows[manger], resource, wanted - taken);
    }
  }
  if (taken < wanted) {
    taken += TakeFromStorage(world, config, resource, wanted - taken);
  }
  if (place.feed_allowance != nullptr && resource.value < place.feed_allowance->size()) {
    (*place.feed_allowance)[resource.value] -= taken;
  }
  AddLedgerAmount(world.ledger.current.feed, resource, taken);
  return taken;
}

/// WHAT THE STORES MAY GIVE THE HERDS TODAY: everything above the plan reserve
/// (core_common/fund_ladder.h). Computed once for the whole walk and spent by
/// it, so the first herd in row order cannot eat what the ladder holds for the
/// rest. Dense by ResourceId over the feed values.
///
/// THE PLAN RUNG AND NOT THE SEED RUNG, and the seed rung was measured before
/// it was left out. Boss's decision of 2026-09-13 named the plan: "стадо берёт
/// корм, не залезая в резерв плана". The ladder in resources design §6 puts the
/// seed fund above fodder too, and holding it from the herds as well was tried
/// on nine seeds: the floor went from 7 failed plan years to 15 on seed 1929
/// and from 6 to 17 on 1935 — horses kept off the oat seed all spring plough
/// slower — while the plan rung alone left the floor unchanged on every seed.
/// Whether the herds should stay below the seed fund is put to boss with those
/// numbers; until he says, they stay below the plan only.
///
/// THAT "UNCHANGED" WAS MEASURED ON A RUNG THAT WAS NOUGHT UNTIL THE REAPING.
/// Since 0.34.42 the plan rung holds the debt out of the carry-over from the
/// January letter (fund_ladder.h, PlanRungGrams), so the horses are kept off
/// the owed oats all spring — the very mechanism the seed-rung trial above
/// measured at 7 -> 15. Re-measured with it: econ's plan700 on nine seeds, 1
/// failed plan year on 1 seed against 2 on 2 on 0.34.40; thirty_years' own
/// verdict the same, 1 on 1.
///
/// AND THE SEED RUNG TOO SINCE 0.35.1 (boss, boss-core-epoch1-5 seq 31;
/// econ's first-harvest-restore.md, lever Д). The decision of 2026-09-13
/// («only the plan») stood on the measurement above, and that measurement was
/// taken in the world that handed the horses out twice (0.34.51): the
/// spring's ploughing was faster than its herd, so holding the horses off
/// the oat seed cost them a pace the model was giving away anyway. Measured
/// by econ in the corrected world: the first harvest's grain peak 24.7 t ->
/// 43.1 t (median of nine), below the floor on 1 seed of 9 instead of 6. The
/// price is the pace — the work ration at nought on 72 of 108 spring
/// seed-days, the ploughing at 1/0.7 — and it is the ladder as the design
/// writes it (resources design §6): the seed above the fodder.
/// @param ploughing_today Whether the plough or the harrow is out today: on
///        such a day the ploughing's oats are the plough's to eat, and are
///        not held (PloughFeedHold). The fodder fund's own read passes true —
///        the fund is the team's, the plough's share inside it.
ResourceAmounts FeedAllowance(const ProductionConfig& config,
                              const WorldState& world,
                              bool ploughing_today) {
  const std::vector<SeedNorm> seed_norms = SeedNormsOf(config);
  ResourceAmounts allowance =
      HeldAboveFodder(world, seed_norms, config.feed_values.size(), true, config.milk_resource);
  // AND WHAT THOSE RUNGS WILL LOSE TO ROT BEFORE THEY ARE USED (0.35.11;
  // core_common/fund_ladder.h, AddRungRotMargins — one home with the people's
  // issue). Held exactly, the herds ate down to the rung and the store's rot
  // took the rest out of the seed: on branch E3 seed 1931 the spring wheat
  // was sown on 2462 kg of its 2520.
  const ResourceAmounts rungs = allowance;
  const ResourceAmounts seed_part = SeedRungLeft(world, seed_norms, config.feed_values.size());
  AddRungRotMargins(
      world, seed_norms, rungs, seed_part, config.spoil_days, config.keeping_factor, allowance);
  // AND WHAT NEXT YEAR'S OWN HARVEST WILL NOT PAY (0.37.2; boss-core-epoch1-
  // queue [52]-[54]): the rotation gives oats 19 t one year and 3.7 t the
  // next. While the carts' horses ate hay the good year's carry-over lay, and
  // the lean year paid its position and the next spring's seed out of it;
  // once they ate their oats, a team took 14.6 t of the 19 and the lean year
  // shipped 1.65 t of its 2.5 (seed 1933, years 4-5). Held with the rot of
  // its wait, to the end of next year, where the last of it is used. One
  // hold with the people's issue (NextYearHold; boss [60], (г)).
  ResourceAmounts next_year = NextYearHold(config, world);
  // AND THE PLOUGH'S OATS ON A DAY NOBODY PLOUGHS (0.37.2; boss [59]-[60], (а)).
  //
  // THE PLOUGH STANDS ABOVE NEXT YEAR'S HOLD (0.37.3; boss [62]-[63], (д)):
  // the oats' ladder is the seed, this year's plan, this spring's ploughing,
  // next year's hold, the rest. On a ploughing day the plough may eat into
  // next year's hold as far as its own rung: below it, a lean spring gave
  // the plough nothing, and the team's ration on the ploughing days stood
  // at 0.185 (median of the years' means, 0426fb2) against 1.00 before the
  // hold existed. A sowing missed costs this harvest and the next one.
  const PloughFeedHold plough = PloughFeedHoldOf(config, world);
  if (plough.held && plough.resource.value < next_year.size() && ploughing_today) {
    Grams& held = next_year[plough.resource.value];
    held = held > plough.grams ? held - plough.grams : 0;
  }
  for (std::size_t index = 0; index < allowance.size() && index < next_year.size(); ++index) {
    allowance[index] += next_year[index];
  }
  if (!ploughing_today && plough.held && plough.resource.value < allowance.size()) {
    allowance[plough.resource.value] += plough.grams;
  }
  for (std::size_t index = 0; index < allowance.size(); ++index) {
    // UNRESERVED, as the plan rung counts it (fund_ladder.h, PlanRungGrams):
    // counted gross, a construction's reserve R of a planned crop came out as
    // an allowance of R the feeding could not take anyway, and the herd then
    // ate R out of the plan's grain (static review of 0.34.42).
    Grams stock = 0;
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    for (const UnitRow& unit : world.units.rows) {
      stock += UnreservedOf(unit, resource);
    }
    allowance[index] = stock > allowance[index] ? stock - allowance[index] : 0;
  }
  return allowance;
}

// -- the day, step by step ---------------------------------------------------

/// Room under the roof, per unit, spent by BilletHerds: each herd's own unit
/// first, then in row order the other units of its type. Since 0.37.30 the
/// order can decide which herd a second yard shelters first; it is fixed,
/// because determinism is not allowed to depend on it deciding nothing.
std::vector<float> RoofRoom(const WorldState& world, const ProductionConfig& config) {
  std::vector<float> room(world.units.rows.size(), 0.0F);
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (unit.type.value >= config.unit_types.size()) {
      continue;
    }
    room[row] = config.unit_types[unit.type.value].LivestockCapacityHeadAt(unit.level);
  }
  return room;
}

/// A head with no room is BILLETED at private yards, never slaughtered
/// (livestock design §6). A herd at a family yard has no numeric limit at
/// all — the yard holds what it holds, and phase 1 puts no ceiling on
/// private livestock.
///
/// Billeted heads per herd row, for the whole day, IN TWO PASSES (0.37.30;
/// boss-core-herd-defects [1] p. 3; livestock design: «свободное место
/// появилось — голову заводят под крышу»): every kolkhoz herd first takes
/// the room of its own unit; then, in row order, what is left of it takes
/// the room left at every other unit of the SAME TYPE. Until then a herd took
/// its own unit's room alone: the canon's chairman built a second yard by
/// year 2 on 27 seeds of 27, and the cows stood at 24 places all the same,
/// the rest on billet, where nothing calves. Two passes, because one would
/// let a herd earlier in the rows fill a yard whose own herd comes later.
/// The herd stays one row at its own unit; the heads another yard shelters
/// are housed, not moved.
std::vector<std::uint16_t> BilletHerds(const WorldState& world, std::vector<float> room) {
  const auto herds = static_cast<std::uint32_t>(world.herds.rows.size());
  std::vector<std::uint16_t> billeted(herds, 0);
  std::vector<float> left(herds, 0.0F);
  std::vector<std::uint32_t> unit_rows(herds, kNoRow);
  const auto take = [&room, &left](std::uint32_t herd_row, std::uint32_t unit_row) {
    const float housed = left[herd_row] < room[unit_row] ? left[herd_row] : room[unit_row];
    room[unit_row] -= housed;
    left[herd_row] -= housed;
  };
  for (std::uint32_t row = 0; row < herds; ++row) {
    const HerdRow& herd = world.herds.rows[row];
    if (herd.household_owned != 0) {
      continue;  // a family's own animals are home; there is nothing to billet
    }
    left[row] = static_cast<float>(TotalHeads(herd));
    const std::uint32_t unit_row =
        herd.unit.value == kInvalidEntityIdValue ? kNoRow : FindRow(world.units, herd.unit);
    // A kolkhoz herd with no roof of its own — the sixteen start horses — is
    // billeted whole. That is the start canon, not a failure state.
    if (unit_row != kNoRow && unit_row < room.size()) {
      unit_rows[row] = unit_row;
      take(row, unit_row);
    }
  }
  for (std::uint32_t row = 0; row < herds; ++row) {
    const std::uint32_t own = unit_rows[row];
    if (own == kNoRow) {
      billeted[row] = AsHeads(left[row]);
      continue;
    }
    const UnitTypeId type = world.units.rows[own].type;
    for (std::uint32_t unit_row = 0; unit_row < room.size() && left[row] > 0.0F; ++unit_row) {
      if (unit_row != own && world.units.rows[unit_row].type.value == type.value &&
          room[unit_row] > 0.0F) {
        take(row, unit_row);
      }
    }
    billeted[row] = AsHeads(left[row]);
  }
  return billeted;
}

/// @brief Share of the draught animals that went out to work today, 0..1.
///
/// The wage ration of question Q2 is per head and per day: a horse in the
/// traces gets oats, a horse standing in the yard gets hay. The core knows
/// how many horses went out — the harness the placements hold (work_seam.h,
/// CountHarness: the plough, the harrow, a carter's horse, a meadow's
/// mower) — but not WHICH ones, because a work order names a field and a
/// worker, never an animal. So the ration is spread: on a day when five
/// horses of sixteen are in the traces, oats may cover five sixteenths of
/// what they would cover on a full working day, and hay carries the rest.
///
/// Read from `current` after the labor sub-step of the same sequential slot
/// has written today's orders (manual/54-modules.md §3).
/// @param horse_backed_days Optional out: today's assignment-days that a horse
///        ACTUALLY pulled — the mechanisation numerator (epochs design §6,
///        boss's decision of 2026-09-12). Filled from the same count that
///        feeds the oats, because the two are one question asked twice: "how
///        much of the pool was in the traces today" (boss-core-epoch1-queue
///        [42], «одна дверь двух потребителей»). Until 0.37.2 that count was
///        the plough and the harrow alone, and a cart horse was in neither.
///
///        NOT the harnessed assignments themselves: more of them than horses
///        means the surplus pulled by hand, so the lesser of the two.
/// @param harnessed_days Optional out: today's harnessed assignments, with a
///        horse and without — the mechanisation denominator, from the SAME
///        count. Answered even in a village with no horse: its carters on
///        foot are harnessed work pulled by hand, and the share is then nil
///        rather than unmeasured.
float WorkingShare(const WorldState& world,
                   const ProductionConfig& config,
                   float* horse_backed_days = nullptr,
                   float* harnessed_days = nullptr) {
  // THE OUT-PARAMS ARE ANSWERED ON EVERY PATH, including the two that give
  // up early. The numerator was left untouched there once, and the doc's
  // "stays at nothing" was then a promise kept by the CALLER's initialiser —
  // true today and true only while every caller keeps writing one.
  if (horse_backed_days != nullptr) {
    *horse_backed_days = 0.0F;
  }
  const HarnessCount harness = CountHarness(world);
  std::uint32_t horses = 0;
  for (const HerdRow& herd : world.herds.rows) {
    if (config.horse_kind.value != kInvalidDefIdValue &&
        herd.kind.value == config.horse_kind.value) {
      horses += herd.adult_count;
    }
  }
  if (harnessed_days != nullptr) {
    // WHAT THE MORNING RELEASES IS NOT WORK PULLED BY HAND (static review of
    // 0.37.2): this runs at hour 0, the release of the work no horse is left
    // for at hour 1 (labor_system.cpp, ReleaseHorselessWork). A chairman's
    // standing order that puts twenty on the plough behind sixteen horses
    // frees four, and they work nothing — counted here, they would read as
    // four days the village ploughed by hand.
    const std::uint32_t excess = harness.in_traces > horses ? harness.in_traces - horses : 0U;
    const std::uint32_t released = excess < harness.releasable ? excess : harness.releasable;
    *harnessed_days = static_cast<float>(harness.harnessed - released);
  }
  if (config.horse_kind.value == kInvalidDefIdValue) {
    return 0.0F;
  }
  if (horses == 0) {
    return 0.0F;  // no horses, no traction, and the out-param already says so
  }
  // THE ASSIGNMENT, NOT THE HOURS ALREADY WORKED. The condition here asked
  // `worked_norm_days_today > 0` until 2026-09-12 and was therefore false
  // for everybody: this runs in the daily block at the first tick of a day,
  // when labor has just handed out today's orders and NOBODY has worked an
  // hour yet. The oats ration is "the horse's wage" (livestock design), and
  // in thirty measured years it paid zero — hay carried the whole keep and
  // no line anywhere said so.
  //
  // The ration is decided at the START of the day, for the horses that are
  // going into the traces, so the assignment is the right thing to read and
  // the hours are the wrong one. Found while wiring the mechanisation share,
  // which reads the same quantity and would have shipped as a constant nil.
  const std::uint32_t working = harness.in_traces;
  const float share = static_cast<float>(working) / static_cast<float>(horses);
  if (horse_backed_days != nullptr) {
    // Assignment-days: one per harness held today, and what the horses can
    // carry is the lesser of the two counts. Both halves of the ratio are
    // one count, the same unit, taken at the same hour, by the same module.
    *horse_backed_days = static_cast<float>(working < horses ? working : horses);
  }
  return share < 1.0F ? share : 1.0F;
}

/// @brief The two running sums whose quotient is the day's traction ration:
/// what the work-only feeds could have covered under their caps, and what
/// they actually did. Summed across every herd of the day's walk.
struct WorkRation {
  float room = 0.0F;

  float covered = 0.0F;

  /// What the takes' whole grams may have cost the cover, in feed units: a
  /// gram of each work-only feed taken, row by row (0.37.8; static review).
  float gram_loss = 0.0F;
};

/// Feed units a kilogram of this link's feed gives. A reserve ration covers
/// less than it weighs: the design says "with a lowered effect" and names no
/// number, so one multiplier stands in for all of them until polish question
/// P22n settles what it hides. ONE HOME for the herd day and the fodder fund
/// (FodderClaim), which must lay a ration out exactly as the day eats it.
float FeedLinkValue(const ProductionConfig& config, const FeedLinkDef& link) {
  if (link.resource.value >= config.feed_values.size()) {
    return 0.0F;
  }
  return config.feed_values[link.resource.value] *
         (link.reserve != 0 ? config.farming.reserve_feed_factor : 1.0F);
}

/// Feed units this link may cover of a day's `need_units`. The order says
/// what to spend FIRST; the cap says how much of it the animal can eat at
/// all. Without the cap the model lies twice over: a ruminant would live on
/// grain alone, and the first horse in the village would eat its whole year
/// of oats by itself. One home with FeedLinkValue's, and for the same reason.
float FeedLinkRoom(const FeedLinkDef& link, float need_units, float working_share) {
  return need_units * link.max_share * (link.work_only != 0 ? working_share : 1.0F);
}

/// Feeds one herd down the feeding order of feed_links.csv. ROW ORDER IS THE
/// PRIORITY — staple before reserve, own feed before bought concentrate,
/// fodder grain before bread grain — and nothing is sorted here: the order
/// arrives from the design db and sorting it again would put the canon in
/// two places.
/// @param working_share What WorkingShare said today; scales the cap of the
///        work-only feeds and nothing else.
/// @param work Optional out, two running sums whose quotient is the traction
///        ration: what the work-only feeds COULD have covered under their
///        caps, and what they actually did. Hay keeps a horse alive and
///        fodder grain makes it pull (world_state.h), and the `work_only`
///        column has said which is which since the first day without a
///        single reader.
///
///        Filled from THIS walk rather than from a second one: the caps and
///        the order are the feeding rule, and a second place computing the
///        same coverage would be that rule's second home.
///
///        ONE STRUCT AND NOT TWO `float*`, because two adjacent pointers of
///        one type are swappable by a typo that compiles: the quotient then
///        comes out inverted, lands above one, and the clamp pins it to
///        "fully fed" — the most flattering possible wrong answer. Named
///        fields cannot be swapped silently.
/// @return The day's feeding: the share of the need it covered, 0..1 (boss
///         seq 171 А: hunger is a share, not a yes-or-no). 1 when there is
///         no need.
float RunFeeding(const ProductionConfig& config,
                 LivestockKindId kind_id,
                 const HerdPlace& place,
                 float need_units,
                 float working_share,
                 WorldState& world,
                 WorkRation* work = nullptr) {
  if (!(need_units > 0.0F)) {
    return 1.0F;
  }
  float covered = 0.0F;
  float work_covered = 0.0F;
  float work_gram_loss = 0.0F;
  // DOES THIS KIND HAVE A WORK RATION AT ALL — asked in its own walk, before
  // the feeding one and not inside it. Inside, the answer depended on the
  // feeding getting AS FAR AS a work-only row: a herd filled up by
  // maintenance feeds listed earlier would break out of the loop first, the
  // flag would stay false, and that herd would drop out of BOTH halves of
  // the traction ratio without a trace. The shipped roster is safe only
  // because oats happen to be the horse's first row — and row order lives in
  // another repository's database, so a table edit there could silence this
  // measurement with nothing on this side to notice.
  bool has_work_feed = false;
  for (const FeedLinkDef& link : config.feed_links) {
    has_work_feed = has_work_feed || (link.kind.value == kind_id.value && link.work_only != 0);
  }
  for (const FeedLinkDef& link : config.feed_links) {
    if (covered >= need_units) {
      break;
    }
    if (link.kind.value != kind_id.value || link.resource.value >= config.feed_values.size()) {
      continue;
    }
    const float value = FeedLinkValue(config, link);
    if (!(value > 0.0F)) {
      continue;
    }
    const float room = FeedLinkRoom(link, need_units, working_share);
    float take_units = need_units - covered;
    take_units = take_units < room ? take_units : room;
    if (!(take_units > 0.0F)) {
      continue;
    }
    const Grams wanted = KilogramsToGrams(take_units / value);
    const Grams got = TakeFeed(world, config, place, link.resource, wanted, link.reserve != 0);
    // THE HAY BY KIND (save 108), on the same terms TakeFeed books `feed`:
    // the kolkhoz herds only (at_unit), a pantry's goat being `yard_feed`.
    if (place.pantry == nullptr && place.at_unit &&
        link.resource.value == config.hay_resource.value) {
      AddLedgerKindGrams(world.ledger.current.herd_hay_eaten, kind_id, got);
    }
    const float gained = static_cast<float>(got) / static_cast<float>(kGramsPerKilogram) * value;
    if (link.work_only != 0) {
      work_covered += gained;
      work_gram_loss += value / static_cast<float>(kGramsPerKilogram);
    }
    covered += gained;
  }
  // THE ROOM IS CAPPED AT WHAT THE ANIMAL CAN EAT, and the cap is the whole
  // reason this is summed here instead of straight into the caller. The
  // work-only caps of a horse add up to 1.3 of its need (oats 0.5, barley
  // 0.4, compound 0.4) — so a plain sum made the denominator half again as
  // large as any achievable numerator, and the ration could not reach 1.0
  // however the settlement fed. A ratio whose top is unreachable is not a
  // ratio: it reads as permanent underfeeding at a farm doing everything
  // right. Boss asked for "the answer about a reachable 1.0" before the
  // measurement said so, and he was right to.
  if (work != nullptr && has_work_feed) {
    // THE DENOMINATOR IS THE ACHIEVABLE WORK RATION, not the whole need and
    // not the roster's caps. The caps sum to 2.2 of a horse's need, so any
    // clamp of them by the need is the need again — and a ration measured
    // against the whole need makes 1.0 mean a horse living on grain alone,
    // which is not a horse. The agronomy bands were measured without this
    // rule, i.e. on a normally fed animal, so the rule's 1.0 has to mean the
    // NORMAL farm or it counts the horse twice (boss, 2026-09-12).
    //
    // AND ONLY FOR KINDS THAT HAVE A WORK RATION AT ALL. It was every herd
    // for one draft — cows, pigs and sheep included — so the denominator
    // carried the whole settlement's keep while the numerator carried the
    // horses' oats, and the ration read a third of what it was. The bug is
    // invisible in the quotient: it just looks like a poorly fed farm.
    const float full = need_units * working_share * config.farming.traction_full_ration_share;
    work->room += full;
    work->covered += work_covered < full ? work_covered : full;
    work->gram_loss += work_gram_loss;
  }
  // A hair of tolerance: the need is a float and the take is integer grams,
  // so an exactly-fed herd can land a milligram short of its own norm.
  const float share = std::min(1.0F, (covered + kFeedToleranceUnits) / need_units);
  // THE NEED LEFT UNCOVERED, priced in hay (save 108; ledger_state.h,
  // herd_feed_short): the kolkhoz herds only, as the hay above, and on
  // exactly the days the share says hungry — one test for both, so the two
  // cannot part at the float's edge (static review of 0.37.7).
  if (share < 1.0F && place.pantry == nullptr && place.at_unit &&
      config.hay_resource.value < config.feed_values.size() &&
      config.feed_values[config.hay_resource.value] > 0.0F) {
    AddLedgerKindGrams(
        world.ledger.current.herd_feed_short,
        kind_id,
        KilogramsToGrams((need_units - covered) / config.feed_values[config.hay_resource.value]));
  }
  return share;
}

/// THE TEAM'S TWO ALARMS' MEMORY, written once a day after the walk
/// (herd_state.h, TractionWatch; save 109). `work` is the walk's work ration,
/// the two days the herd day booked into the mechanisation share.
void WatchTheTeam(const ProductionConfig& config,
                  const WorkRation& work,
                  float horse_backed_days,
                  float harnessed_days,
                  WorldState& current) {
  TractionWatch& watch = current.traction_watch;
  // «УПРЯЖЬ НА СЕНЕ»: a working day short of the full work ration adds a day
  // and its grain; a full one clears both; a day with no room keeps them —
  // unless the team is gone, which clears them too: a streak of a dead team
  // would otherwise light on the first horses bought, before they worked a
  // day (static review of 0.37.8).
  //
  // SHORT BEYOND THE GRAMS' ROUNDING, ROW BY ROW (static review of 0.37.8):
  // each row's oats are taken in whole grams, truncated, so a team standing
  // in sixteen rows of one head — the start's, until the stable — comes up
  // to 16 g short on a full barn, far above RunFeeding's one-herd tolerance.
  // The walk says what its takes may have cost (WorkRation::gram_loss).
  bool team_left = false;
  for (const HerdRow& herd : current.herds.rows) {
    team_left = team_left || (herd.household_owned == 0 &&
                              herd.kind.value == config.horse_kind.value && herd.adult_count > 0);
  }
  if (!team_left) {
    watch.short_ration_days = 0;
    watch.work_grain_short = 0;
  } else if (work.room > 0.0F) {
    const float short_units = work.room - work.covered;
    if (short_units > work.gram_loss + kFeedToleranceUnits) {
      if (watch.short_ration_days < std::numeric_limits<std::uint16_t>::max()) {
        ++watch.short_ration_days;
      }
      // Priced in the horse's first work-only feed: the oats.
      for (const FeedLinkDef& link : config.feed_links) {
        if (link.kind.value == config.horse_kind.value && link.work_only != 0) {
          const float value = FeedLinkValue(config, link);
          if (value > 0.0F) {
            watch.work_grain_short += KilogramsToGrams(short_units / value);
          }
          break;
        }
      }
    } else {
      watch.short_ration_days = 0;
      watch.work_grain_short = 0;
    }
  }
  // «ЛОШАДЕЙ НЕ ХВАТАЕТ»: today's slot of the rolling week, written every
  // day, noughts included, so the week never holds a stale day.
  const std::uint32_t slot = current.calendar.day % kHarnessWeekDays;
  watch.week_harnessed[slot] = harnessed_days;
  watch.week_horse_backed[slot] = horse_backed_days;
}

/// What the day's produce is multiplied by. Two leaks, both of them the
/// design's: a hungry herd gives less at once, and a billeted head gives
/// less because part of what it makes settles in the yard it stands in.
float YieldFactor(const ProductionConfig& config, const HerdRow& herd) {
  // THE SHARE OF THE RATION, floored at the hungry factor (boss seq 171 А):
  // a herd fed a third gives a third, never less than a starving one. Until
  // 2026-09-19 any short day gave the hungry factor whole, so bought feed
  // that closed part of the ration gave nothing back.
  float factor =
      herd.unfed_days > 0.0F ? std::max(config.farming.unfed_produce_factor, herd.fed_share) : 1.0F;
  const auto total = static_cast<float>(TotalHeads(herd));
  if (total > 0.0F && herd.billeted_count > 0) {
    const float billeted_share = static_cast<float>(herd.billeted_count) / total;
    factor *= 1.0F - (billeted_share * (1.0F - config.farming.billet_yield_factor));
  }
  return factor;
}

/// One herd's milk of a day at `factor`: the females only, a litre a
/// kilogram (quantities.h). ONE HOME for what the herd gives and what the
/// plan asks of it (KolkhozMilkDayGrams).
Grams HerdMilkDayGrams(const LivestockDef& kind, const HerdRow& herd, float factor) {
  const auto females = static_cast<float>(kind.sexed != 0 ? herd.adult_count - herd.adult_male_count
                                                          : herd.adult_count);
  return KilogramsToGrams(kind.milk_l_per_year * females * factor /
                          static_cast<float>(kDaysPerYear));
}

void RunProduce(const ProductionConfig& config,
                const LivestockDef& kind,
                const HerdRow& herd,
                const HerdPlace& place,
                WorldState& world) {
  const float factor = YieldFactor(config, herd);
  const auto adults = static_cast<float>(herd.adult_count);
  const auto year = static_cast<float>(kDaysPerYear);
  // Milk counts females only; eggs, wool and manure count every adult.
  // A litre of milk is a kilogram in the store (quantities.h).
  DeliverProduce(world, config, place, config.milk_resource, HerdMilkDayGrams(kind, herd, factor));
  DeliverProduce(world,
                 config,
                 place,
                 config.egg_resource,
                 KilogramsToGrams(kind.egg_kg_per_year * adults * factor / year));
  DeliverProduce(world,
                 config,
                 place,
                 config.wool_resource,
                 KilogramsToGrams(kind.wool_kg_per_year * adults * factor / year));
  // Manure goes to the compost heap, not to the store, and only from a
  // kolkhoz herd: what a family's goat leaves stays in the family's yard
  // (livestock design §5).
  if (place.pantry != nullptr || !(kind.manure_kg_per_year > 0.0F)) {
    return;
  }
  const std::uint32_t heap = FindUnitRowOfType(world, config.compost_heap_type);
  if (heap == kNoRow) {
    return;
  }
  UnitRow& compost = world.units.rows[heap];
  const Grams daily = KilogramsToGrams(kind.manure_kg_per_year * adults * factor / year);
  // A heap whose capacity is its own outline takes whatever is brought:
  // the player drew how big it is, and the table has no number to clamp on.
  const Grams capacity = StorageCapacityGrams(compost, config);
  if (capacity < 0) {
    AddToStock(compost.stock, config.manure_resource, daily);
    AddLedgerAmount(world.ledger.current.herd_produce, config.manure_resource, daily);
    return;
  }
  const Grams held = StockOf(compost.stock, config.manure_resource);
  const Grams room = capacity > held ? capacity - held : 0;
  const Grams added = daily < room ? daily : room;
  AddToStock(compost.stock, config.manure_resource, added);
  AddLedgerAmount(world.ledger.current.herd_produce, config.manure_resource, added);
}

/// @brief Yards that keep nothing of a group, in row order — the queue a
/// grown head is walked to.
///
/// BUILT ONCE A DAY, and that is the whole point. The obvious shape — for
/// each surplus head, scan the families and for each family scan the herds —
/// is quadratic in a village, and it showed: with five hundred households
/// each keeping birds, the thirty-year run went from eighty seconds to
/// twenty-five minutes. One pass over the herds and one over the families
/// costs the same as the herd day already costs.
///
/// Row order in, row order out, so the gift lands on the same yard on every
/// machine and every replay.
struct GiftQueues {
  std::vector<FamilyId> without_stock;  ///< household_group 1.

  std::vector<FamilyId> without_bird;  ///< household_group 2.

  std::uint32_t next_stock = 0;

  std::uint32_t next_bird = 0;
};

GiftQueues CollectGiftQueues(const WorldState& world, const ProductionConfig& config) {
  GiftQueues queues;
  // groups[family row] is a two-bit mask of the groups that yard already
  // keeps. A herd with no heads left in it keeps nothing.
  std::vector<std::uint8_t> groups(world.families.rows.size(), 0);
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned == 0 || herd.kind.value >= config.livestock.size() ||
        TotalHeads(herd) == 0) {
      continue;
    }
    const std::uint8_t group = config.livestock[herd.kind.value].household_group;
    const std::uint32_t row = FindRow(world.families, herd.household);
    if (group != 0 && row != kNoRow) {
      groups[row] = static_cast<std::uint8_t>(groups[row] | (1U << group));
    }
  }
  for (std::uint32_t row = 0; row < groups.size(); ++row) {
    // No yard in a barrack, so no grown head is walked there (housing §9).
    if (world.families.rows[row].in_barrack != 0) {
      continue;
    }
    if ((groups[row] & (1U << 1U)) == 0U) {
      queues.without_stock.push_back(world.families.row_ids[row]);
    }
    if ((groups[row] & (1U << 2U)) == 0U) {
      queues.without_bird.push_back(world.families.row_ids[row]);
    }
  }
  return queues;
}

/// Hands one grown head to a yard that keeps nothing of its group — the
/// canon's second path for the young, and free ("neighbourly help, not
/// trade"). It is also how a village that grows finds livestock for the
/// households it did not start with: only the twenty-one start yards were
/// given goats, and by the thirtieth year there are five hundred families.
///
/// @param pending Gifts promised today. THE ROW IS NOT APPENDED HERE: the
/// herd day walks `world.herds.rows` by index and holds a reference into it,
/// and growing that vector mid-walk would leave the reference dangling. The
/// promises are kept in a list and appended once the walk is over.
/// @return true when a yard took it.
/// @param grown True when the head being walked over is an adult; a young one
/// arrives at its new yard young, and has to grow up there.
bool GiveToNeighbour(GiftQueues& queues,
                     std::vector<HerdRow>& pending,
                     const HerdRow& from,
                     const LivestockDef& kind,
                     bool grown,
                     float age_years) {
  const std::uint8_t group = kind.household_group;
  if (group != 1U && group != 2U) {
    return false;
  }
  std::vector<FamilyId>& queue = group == 1U ? queues.without_stock : queues.without_bird;
  std::uint32_t& next = group == 1U ? queues.next_stock : queues.next_bird;
  while (next < queue.size()) {
    const FamilyId family = queue[next];
    ++next;  // each yard is offered once a day: a head, not a herd
    if (family.value == from.household.value) {
      continue;
    }
    HerdRow gift;
    gift.kind = from.kind;
    gift.household = family;
    gift.household_owned = 1;
    if (grown) {
      gift.adult_count = 1;
      gift.adult_male_count = TargetMales(kind, 1);
      gift.adult_age_game_years_total = age_years;
      WidenAdultAgeBand(gift, 0, 1, age_years, age_years);
    } else {
      gift.juvenile_count = 1;
    }
    pending.push_back(gift);
    return true;
  }
  return false;
}

/// The yard is full and a head has grown into it. The canon's three paths,
/// in its own order (household design §2): the grown head takes an adult's
/// place and the adult goes to meat; failing that the surplus goes to a
/// neighbour who keeps nothing of its group; failing that it is slaughtered.
/// Here the first and the third are the same act seen from two ends — a head
/// leaves the yard for the block — so what the code chooses between is the
/// gift and the knife.
void PlaceSurplusHead(const ProductionConfig& config,
                      const LivestockDef& kind,
                      const HerdPlace& place,
                      GiftQueues& queues,
                      std::vector<HerdRow>& pending,
                      HerdRow& herd,
                      HerdId herd_id,
                      WorldState& world) {
  if (!(kind.household_cap_heads > 0.0F)) {
    return;
  }
  const auto cap = static_cast<std::uint16_t>(kind.household_cap_heads);
  const float adult_from_years = kind.adult_from_game_months / static_cast<float>(kMonthsPerYear);
  std::uint32_t slaughtered = 0;
  while (TotalHeads(herd) > cap) {
    bool grown = false;
    float age = 0.0F;
    if (herd.adult_count > cap) {
      // The canon's first path, and its main one: the head that grew up
      // takes an old one's place, and the OLD one goes — which is also what
      // keeps a yard's animals young instead of ageing together.
      grown = true;
      const auto adults = static_cast<float>(herd.adult_count);
      const float mean = herd.adult_age_game_years_total / adults;
      age = kind.life_game_years_max > mean ? kind.life_game_years_max : mean;
      CutOldestFromAdultAgeBand(herd, herd.adult_count, 1);  // the old one's end of the band
      herd.adult_count = static_cast<std::uint16_t>(herd.adult_count - 1);
      herd.adult_age_game_years_total -= age;
      const float youngest = static_cast<float>(herd.adult_count) * adult_from_years;
      herd.adult_age_game_years_total =
          herd.adult_age_game_years_total < youngest ? youngest : herd.adult_age_game_years_total;
    } else if (herd.newborn_count > 0) {
      herd.newborn_count = static_cast<std::uint16_t>(herd.newborn_count - 1);
    } else if (herd.juvenile_count > 0) {
      herd.juvenile_count = static_cast<std::uint16_t>(herd.juvenile_count - 1);
    } else {
      break;  // nothing left to place, and the cap is met by definition
    }
    // Paths two and three: a yard that keeps nothing of this group takes it,
    // free; and if there is no such yard, it is meat.
    if (!GiveToNeighbour(queues, pending, herd, kind, grown, age)) {
      world.ledger.current.herd_culled += 1;
      ++slaughtered;
      if (grown) {
        Slaughter(config, kind, place, 1, world);
      }
    }
  }
  herd.adult_male_count = TargetMales(kind, herd.adult_count);
  // SAID, AND BOOKED BY KIND (boss, boss-core-epoch1-5 seq 43 and 45; 0.35.3):
  // the heads the knife took here wrote herd_culled alone and said nothing.
  if (slaughtered > 0) {
    AddLedgerHeads(world.ledger.current.herd_surplus_slaughtered, herd.kind, slaughtered);
    SimEvent& event = EmitEvent(world, EventKind::kHerdSurplusSlaughtered);
    event.herd = herd_id;
    event.amount = static_cast<std::int64_t>(slaughtered);
  }
}

/// Whether the plough or the harrow is out today: the day the ploughing's
/// oats are the plough's to eat (FeedAllowance). One reading for the herd day
/// and the feed light (HerdFeedAllowance).
///
/// NOT THE AUTUMN FURROW (static review of 0.37.18): the hold is the SPRING
/// ploughing's oats, «ровно то, что зима есть не должна» (resources §6);
/// released on every zyab day from August to December, the pigs ate the
/// spring's oats in the autumn. One left part-turned by the turn is ploughed
/// out in the spring by the spring's own furrow (OpenPlowing clears the
/// mark), which eats as any spring furrow does.
bool PloughingToday(const WorldState& world) {
  return std::ranges::any_of(world.residents.rows, [&world](const ResidentRow& resident) {
    if (!IsHorseWork(resident.work.kind)) {
      return false;
    }
    const std::uint32_t row = FindRow(world.fields, resident.work.field);
    return row == kNoRow || world.fields.rows[row].autumn_furrowing == 0;
  });
}

}  // namespace

ResourceAmounts HerdFeedAllowance(const ProductionConfig& config, const WorldState& world) {
  return FeedAllowance(config, world, PloughingToday(world));
}

bool StableBuilt(const WorldState& world, const ProductionConfig& config) {
  if (config.stable_type.value == kInvalidDefIdValue) {
    return false;
  }
  for (const UnitRow& unit : world.units.rows) {
    if (unit.type.value == config.stable_type.value && unit.level >= 2) {
      return true;
    }
  }
  return false;
}

/// The day's fodder need, in feed units. Adults eat the norm, juveniles the
/// juvenile share of it, newborns at the dam nothing. In the pasture months
/// the grass covers its share of the need — the norm is about NEED, not
/// about a mandatory trip to the store.
float FeedNeedUnits(const ProductionConfig& config,
                    const LivestockDef& kind,
                    const HerdRow& herd,
                    std::uint8_t month,
                    bool grazing_tonight) {
  const float heads = static_cast<float>(herd.adult_count) +
                      static_cast<float>(herd.juvenile_count) * config.farming.juvenile_feed_factor;
  float need = heads * kind.feed_units_per_game_day;
  if (MonthInRange(month, config.farming.pasture_from_month, config.farming.pasture_to_month) &&
      grazing_tonight) {
    need *= 1.0F - kind.pasture_coverage_summer;
  }
  return need > 0.0F ? need : 0.0F;
}

void RunHerdDay(const ProductionConfig& config, WorldState& current) {
  if (config.livestock.empty()) {
    return;  // a table-less world keeps no animals
  }
  StableHorses(config, current);
  // AND EVERY OTHER KIND INTO ITS HOUSE (livestock_homes.h; 0.35.4): a herd a
  // limit lot founded stood billeted for good, and billeted it never bred.
  HouseHomelessHerds(config, current);
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  // The day's billet, every herd at once, before any herd's day moves a head
  // (BilletHerds: its own yard first, then the others of its type).
  const std::vector<std::uint16_t> billet = BilletHerds(current, RoofRoom(current, config));
  float horse_backed_days = 0.0F;
  float harnessed_days = 0.0F;
  const float working_share = WorkingShare(current, config, &horse_backed_days, &harnessed_days);
  // BOTH HALVES OF THE MECHANISATION SHARE, here and only here (epochs
  // design §6; ledger_state.h). The denominator was booked by core_labor for
  // one afternoon, from delivered norm-days — a different unit at a
  // different hour on the other side of the year's close, which let the
  // quotient pass 1 and made the drift invisible, because a quotient of two
  // wrong things still looks like a quotient.
  current.ledger.current.horse_backed_assignment_days += horse_backed_days;
  current.ledger.current.harnessed_assignment_days += harnessed_days;
  // The day's work ration of the working stock, summed over every herd the
  // walk below feeds and turned into the traction ration at the end of it.
  WorkRation work;
  // Every assignment-day: the numerator of the effort share (ledger_state.h),
  // no longer the traction's denominator since 0.37.2.
  for (const ResidentRow& resident : current.residents.rows) {
    if (resident.work.kind != WorkKind::kNone) {
      current.ledger.current.total_assignment_days += 1.0F;
    }
  }
  const bool stable_built = StableBuilt(current, config);
  // THE SUMMER DISCOUNT IS THE NIGHT PASTURE AND NOTHING ELSE, for the team.
  // Until 2026-09-17 every kind with a `pasture_coverage_summer` got it for
  // every pasture month unconditionally — and for the horses that was the
  // night pasture's whole gain, paid out with no yard, no order and no
  // children, from day zero, to a team standing in private yards.
  //
  // THE OTHER KINDS KEEP THEIRS UNCONDITIONALLY AND RIGHTLY: the ducks'
  // self-pasture and the flock's summer are ordinary grazing the design gives
  // them outright («на выпасе они кормятся почти даром»). Same coefficient,
  // different subject — which is why the flag is asked per kind and not per
  // season.
  const bool team_out = TeamOutTonight(config, current);
  const auto grazing_tonight = [&](LivestockKindId kind_id) {
    return kind_id.value != config.horse_kind.value || team_out;
  };
  ResourceAmounts feed_allowance = HerdFeedAllowance(config, current);
  const std::vector<std::uint8_t> peoples_foods = PeoplesFoods(config, current);
  std::vector<HerdRow> gifts;  // appended after the walk; see GiveToNeighbour
  GiftQueues queues = CollectGiftQueues(current, config);
  for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
    HerdRow& herd = current.herds.rows[row];
    if (herd.kind.value >= config.livestock.size()) {
      continue;
    }
    const LivestockDef& kind = config.livestock[herd.kind.value];
    // THE SIRE COUNT IS A THING THE HERD REMEMBERS, and this line used to say
    // the opposite: it re-derived the count from the herd's size every single
    // day. That was true while heads could only be born. Since the district
    // sells stock it is false and expensive — a head bought as a mare became
    // a stallion by the next morning, and the chairman's choice of sex, which
    // the design promises him, had no consequence whatever (boss, parcel 20).
    //
    // What is left here is the INVARIANT and nothing else: sires never stand
    // above adults, and a sexless kind has none. Every path that changes the
    // adults — maturation, the cull, age, hunger, the autumn slaughter —
    // carries the sires with it at its own site, where what happened is known.
    herd.adult_male_count =
        kind.sexed == 0 ? 0U : std::min(herd.adult_male_count, herd.adult_count);
    HerdPlace place = PlaceOf(current, config, herd);
    place.feed_allowance = &feed_allowance;
    place.peoples_foods = &peoples_foods;
    herd.billeted_count = row < billet.size() ? billet[row] : 0;
    // The yard's hens, ducks and pig feed themselves (question Q1): range,
    // scraps and the garden, and the winter handful of grain out of the
    // family's own ration, which the meal already counts. They are FED, not
    // starving on an empty manger — the flag says the store is not their
    // source, not that they eat nothing.
    const bool self_fed = herd.household_owned != 0 && kind.household_self_fed != 0;
    herd.fed_share =
        self_fed ? 1.0F
                 : RunFeeding(config,
                              herd.kind,
                              place,
                              FeedNeedUnits(config, kind, herd, month, grazing_tonight(herd.kind)),
                              working_share,
                              current,
                              &work);
    const bool fed = herd.fed_share >= 1.0F;
    herd.unfed_days = fed ? 0.0F : herd.unfed_days + 1.0F;
    if (fed) {
      herd.hunger_progress = 0.0F;  // a fed day clears the debt, not just the count
    } else {
      current.ledger.current.herd_hungry_head_days += static_cast<float>(TotalHeads(herd));
    }
    RunProduce(config, kind, herd, place, current);
    const HerdId herd_id = current.herds.row_ids[row];
    RunMaturation(config, kind, place, herd, herd_id, current);
    if (herd.household_owned != 0) {
      PlaceSurplusHead(config, kind, place, queues, gifts, herd, herd_id, current);
    }
    // The calving band is the WALK's to read: it owns the calendar, and
    // herd_life.h owns the animal. Handed down as a bare bool, exactly as
    // `stable_built` above it already is.
    const bool in_birth_season =
        MonthInRange(month, config.farming.birth_from_month, config.farming.birth_to_month);
    RunBirths(config,
              kind,
              herd.kind,
              herd,
              herd_id,
              in_birth_season,
              stable_built,
              current,
              current.ledger.current);
    RunAgeDeaths(kind, herd, herd_id, current);
    RunHungerDeaths(config, kind, herd, herd_id, current, current.ledger.current);
    RunAutumnSlaughter(config, kind, herd.kind, place, herd, herd_id, current, current.calendar);
  }
  // THE TRACTION RATION of the day (world_state.h): how much of what the
  // work-only feeds COULD have covered they actually did. Taken after the
  // walk, because the walk is where the feeding rule lives and a second
  // computation of the same coverage would be that rule's second home.
  //
  // No room means no working stock out today — and then the ration is not
  // nought, it is UNCHANGED: a horse that stood idle yesterday is neither
  // better nor worse fed for it, and writing a zero would tell the sowing
  // that the animals had been starved.
  if (work.room > 0.0F) {
    const float ration = work.covered / work.room;
    current.traction_ration = ration < 1.0F ? ration : 1.0F;
  }
  WatchTheTeam(config, work, horse_backed_days, harnessed_days, current);
  for (const HerdRow& gift : gifts) {
    AppendRow(current.herds, gift);
  }
}

namespace {

/// The working stock's day of one work feed, in the pieces the fund's two
/// sizes are built from — kept as pieces so that the year's size keeps the
/// exact arithmetic it had before the rung was sized (a float reordered is a
/// gram moved, and the accumulation limit's base reads the year's size).
struct WorkFeedDay {
  float units = 0.0F;  ///< feed units a day, the whole working stock, before the share
  float share = 0.0F;  ///< the work feed's share of the ration (feed_links.max_share)
  float value = 0.0F;  ///< feed units per kilogram of this resource
};

WorkFeedDay WorkFeedDayOf(const ProductionConfig& config,
                          const WorldState& current,
                          ResourceId resource) {
  WorkFeedDay day;
  day.value =
      resource.value < config.feed_values.size() ? config.feed_values[resource.value] : 0.0F;
  if (!(day.value > 0.0F)) {
    return day;
  }
  for (const FeedLinkDef& link : config.feed_links) {
    if (link.work_only != 0 && link.resource.value == resource.value) {
      day.share = link.max_share;
      break;
    }
  }
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  float& units = day.units;
  for (const HerdRow& herd : current.herds.rows) {
    if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
      continue;  // the fund is the kolkhoz's; a yard's animals feed themselves
    }
    bool works = false;
    for (const FeedLinkDef& link : config.feed_links) {
      works = works || (link.kind.value == herd.kind.value && link.work_only != 0);
    }
    if (!works) {
      continue;  // a cow has no work ration, so it holds nothing in this fund
    }
    // FALSE: the fodder fund does not count on the night pasture. It is the
    // chairman's order and the children that make it happen, and a reserve
    // sized against a gain that can stop is short in the year it stops
    // (herd_system.h).
    units += FeedNeedUnits(config, config.livestock[herd.kind.value], herd, month, false);
  }
  return day;
}

/// Days from today to the next reaping of `resource`: to the END of its
/// crop's reaping window while this year's has not come in (the team eats
/// until the new oats are in the store, and a window is where a reaping may
/// yet fall), to the START of next year's window once it has. The nearest of
/// the crops that give it; 0 when no crop does.
std::uint32_t DaysToNextReaping(const ProductionConfig& config,
                                const WorldState& current,
                                ResourceId resource) {
  const std::uint32_t today = current.calendar.day % kDaysPerYear;
  const ResourceAmounts& reaped = current.ledger.current.harvest;
  const bool in_this_year = resource.value < reaped.size() && reaped[resource.value] > 0;
  std::uint32_t nearest = 0;
  bool found = false;
  for (const CropDef& crop : config.crops) {
    if (crop.resource.value != resource.value) {
      continue;
    }
    const std::uint32_t window_start = crop.harvest_from_month * kDaysPerMonth;
    const std::uint32_t window_end = (crop.harvest_to_month + 1U) * kDaysPerMonth;
    std::uint32_t days = 0;
    if (in_this_year) {
      days = window_start + kDaysPerYear - today;  // next year's window
    } else {
      days = window_end > today ? window_end - today : window_start + kDaysPerYear - today;
    }
    nearest = found ? std::min(nearest, days) : days;
    found = true;
  }
  return nearest;
}

}  // namespace

ResourceAmounts NextYearHold(const ProductionConfig& config, const WorldState& world) {
  ResourceAmounts hold(config.feed_values.size(), 0);
  const auto days_to_next_turn =
      static_cast<std::uint32_t>(kDaysPerYear - (world.calendar.day % kDaysPerYear) + kDaysPerYear);
  for (std::size_t index = 0; index < hold.size(); ++index) {
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const bool grown = std::ranges::any_of(
        config.crops, [resource](const CropDef& crop) { return crop.resource == resource; });
    if (!grown) {
      continue;  // no field gives it, so no harvest owes it
    }
    const Grams unpaid = NextYearUnpaidGrams(config, world, resource, world.calendar.day);
    if (unpaid > 0) {
      hold[index] = HeldForDeliveryGrams(config, resource, unpaid, days_to_next_turn);
    }
  }
  return hold;
}

std::vector<std::uint8_t> PeoplesFoods(const ProductionConfig& config, const WorldState& world) {
  std::vector<std::uint8_t> foods(config.feed_values.size(), 0);
  for (std::size_t index = 0; index < foods.size(); ++index) {
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    foods[index] = AmountOf(world.ledger.closed.issued, resource) > 0 ||
                           AmountOf(world.ledger.current.issued, resource) > 0
                       ? 1U
                       : 0U;
  }
  return foods;
}

PloughFeedHold PloughFeedHoldOf(const ProductionConfig& config, const WorldState& world) {
  PloughFeedHold hold;
  if (config.horse_kind.value == kInvalidDefIdValue ||
      config.horse_kind.value >= config.livestock.size()) {
    return hold;
  }
  const FeedLinkDef* oats = nullptr;
  for (const FeedLinkDef& link : config.feed_links) {
    if (link.kind.value == config.horse_kind.value && link.work_only != 0 && link.reserve == 0) {
      oats = &link;
      break;
    }
  }
  if (oats == nullptr) {
    return hold;
  }
  hold.resource = oats->resource;
  const float value = FeedLinkValue(config, *oats);
  if (!(value > 0.0F)) {
    return hold;
  }
  // THE SEASON: from the feed's reaping this year to the end of the latest
  // spring sowing window (a winter crop's sowing follows the reaping and is
  // on the new feed).
  std::uint32_t spring_end_day = 0;
  for (const CropDef& crop : config.crops) {
    if (!crop.is_winter) {
      const std::uint32_t window_end =
          (static_cast<std::uint32_t>(crop.sow_to_month) + 1U) * kDaysPerMonth;
      spring_end_day = std::max(spring_end_day, window_end);
    }
  }
  // AND ONCE THE REAPING'S WINDOW IS PAST, reaped or not (static review of
  // 0.37.2): a year with no oats in — a failed crop, a lost slot — held
  // nothing from the spring to the turn, and the carts ate next spring's
  // ploughing in exactly the year it was scarcest.
  std::uint32_t reaping_end_day = kDaysPerYear;
  for (const CropDef& crop : config.crops) {
    if (crop.resource.value == oats->resource.value) {
      const std::uint32_t window_end =
          (static_cast<std::uint32_t>(crop.harvest_to_month) + 1U) * kDaysPerMonth;
      reaping_end_day = std::min(reaping_end_day, window_end);
    }
  }
  const std::uint32_t today = world.calendar.day % kDaysPerYear;
  const bool reaped = AmountOf(world.ledger.current.harvest, oats->resource) > 0;
  hold.held = reaped || today < spring_end_day || today >= reaping_end_day;
  // THE HORSE-DAYS: the last closed book's ploughing and harrowing, or in the
  // first year the arable under chains at the norms.
  if (world.calendar.day >= static_cast<SimDay>(kDaysPerYear)) {
    const YearLedger& book = world.ledger.closed;
    hold.book_year = book.year;
    hold.horse_days = book.work_days_by_kind[static_cast<std::size_t>(WorkKind::kPlowing)] +
                      book.work_days_by_kind[static_cast<std::size_t>(WorkKind::kHarrowing)];
  } else {
    for (const FieldRow& field : world.fields.rows) {
      if (field.kind == LandKind::kArable && field.rotation_assigned != 0) {
        hold.horse_days +=
            field.area_ga * (config.farming.plow_days_per_ha + config.farming.harrow_days_per_ha);
      }
    }
  }
  // The oats of one horse-day at full work: the need, the oats' share of it
  // no more than the full work ration's share, at the oats' value.
  const float need = config.livestock[config.horse_kind.value].feed_units_per_game_day;
  const float share = std::min(oats->max_share, config.farming.traction_full_ration_share);
  if (hold.held) {
    hold.grams = KilogramsToGrams(hold.horse_days * need * share / value);
  }
  return hold;
}

Grams FodderFundGrams(const ProductionConfig& config,
                      const WorldState& current,
                      ResourceId resource) {
  const WorkFeedDay day = WorkFeedDayOf(config, current, resource);
  if (!(day.value > 0.0F)) {
    return 0;
  }
  const float year_units = day.units * static_cast<float>(kDaysPerYear) * day.share;
  return GramsFromKilograms(year_units / day.value);
}

Grams FodderClaimGrams(const ProductionConfig& config,
                       const WorldState& current,
                       ResourceId resource) {
  const ResourceAmounts claim = FodderClaim(config, current);
  return resource.value < claim.size() ? claim[resource.value] : 0;
}

ResourceAmounts FodderClaim(const ProductionConfig& config, const WorldState& current) {
  ResourceAmounts claim(config.feed_values.size(), 0);
  // What each feed can still give the fund, spent kind by kind as the ration
  // is laid out, so two working kinds cannot both count the same sack.
  //
  // THROUGH THE HERD'S OWN DOOR (the static loop of 23 September): the walk
  // first read every sack in the stores, the plan's oats included, which the
  // herd day never eats (FeedAllowance) — so oats held for the district
  // "covered" the team, the barley went out in the issue, and the team was
  // left with neither. The seed oats DO count: the herds stay below the plan
  // rung only, pending boss's answer on the seed fund (FeedAllowance).
  //
  // FILLED BY THE HARVEST, NOT BY THE CALENDAR (boss seq 14, answer 2): no
  // more of a feed than its last reaping brought in. THE FIRST YEAR HAS NO
  // LAST REAPING, and the start stock stands in for it (boss seq 17: the
  // winter decision must live in year 1): before the first reaping of a feed
  // in the campaign, no cap. The cap was applied after the walk until the
  // same loop, and what it cut off a staple no reserve then took up.
  // The fund is the team's, the plough's share inside it: read as on a
  // ploughing day, nothing held from it for the plough.
  ResourceAmounts stock = FeedAllowance(config, current, true);
  stock.resize(claim.size(), 0);
  const ResourceAmounts& this_year = current.ledger.current.harvest;
  const ResourceAmounts& last_year = current.ledger.closed.harvest;
  for (std::size_t index = 0; index < stock.size(); ++index) {
    const Grams reaped_now = index < this_year.size() ? this_year[index] : 0;
    const Grams reaped_before = index < last_year.size() ? last_year[index] : 0;
    const bool first_year_unreaped =
        reaped_now == 0 && current.calendar.day < static_cast<SimDay>(kDaysPerYear);
    if (first_year_unreaped) {
      continue;
    }
    const Grams cap = reaped_now > 0 ? reaped_now : reaped_before;
    stock[index] = stock[index] < cap ? stock[index] : cap;
  }
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  const std::vector<std::uint8_t> peoples_foods = PeoplesFoods(config, current);
  for (std::uint32_t kind = 0; kind < config.livestock.size(); ++kind) {
    // THE TEAM'S NEED, ONCE (host's barley trace, boss-core-epoch1-2 seq 1):
    // the fund held each fund feed at its own cap — oats 0.5 AND barley 0.4
    // of the ration, at once — and barley, a reserve eaten only when oats
    // run out, was held about thirty times what the horses ate of it: all
    // the barley kept from the people in winter and let out in March. The
    // fund holds "the annual NEED of working stock" (resources design §6):
    // one work ration, the same achievable ration the traction ratio is
    // measured against, laid out down the feeding order below.
    float need_day = 0.0F;
    for (const HerdRow& herd : current.herds.rows) {
      if (herd.household_owned == 0 && herd.kind.value == kind) {
        // FALSE: the fund does not count on the night pasture — the
        // chairman's order and the children make it, and a reserve sized
        // against a gain that can stop is short in the year it stops.
        need_day += FeedNeedUnits(config, config.livestock[kind], herd, month, false);
      }
    }
    if (!(need_day > 0.0F)) {
      continue;
    }
    // Until the next reaping of the kind's STAPLE fund feed — the first in
    // the feeding order: when it comes in, the rung fills again.
    float days = -1.0F;
    for (const FeedLinkDef& link : config.feed_links) {
      if (link.kind.value == kind && link.fodder_fund != 0) {
        days = static_cast<float>(DaysToNextReaping(config, current, link.resource));
        break;
      }
    }
    if (!(days > 0.0F)) {
      continue;  // this kind has no fund feed
    }
    float owed = need_day * config.farming.traction_full_ration_share * days;
    // DOWN THE FEEDING ORDER, as the herd day eats it (FeedLinkValue,
    // FeedLinkRoom — the one home of both): the staple first, as far as it
    // lies in the stores and its cap lets it cover; a reserve only for what
    // the staple cannot.
    for (const FeedLinkDef& link : config.feed_links) {
      if (!(owed > 0.0F)) {
        break;
      }
      if (link.kind.value != kind || link.fodder_fund == 0 || link.resource.value >= claim.size()) {
        continue;
      }
      const float value = FeedLinkValue(config, link);
      if (!(value > 0.0F)) {
        continue;
      }
      const float room = FeedLinkRoom(link, need_day, 1.0F) * days;
      // NOT A RESERVE FEED OF THE PEOPLE'S FOOD, as the herd day eats none of
      // it (0.37.5; PeoplesFoods): held for a horse that may not eat it, the
      // barley would be locked from both.
      Grams available = stock[link.resource.value];
      if (link.reserve != 0 && link.resource.value < peoples_foods.size() &&
          peoples_foods[link.resource.value] != 0) {
        available = 0;
      }
      const float in_store =
          static_cast<float>(available) / static_cast<float>(kGramsPerKilogram) * value;
      float units = owed < room ? owed : room;
      units = units < in_store ? units : in_store;
      if (!(units > 0.0F)) {
        continue;
      }
      const Grams grams = GramsFromKilograms(units / value);
      claim[link.resource.value] += grams;
      stock[link.resource.value] -=
          grams < stock[link.resource.value] ? grams : stock[link.resource.value];
      owed -= units;
    }
  }
  return claim;
}

Grams KolkhozMilkDayGrams(const ProductionConfig& config, const WorldState& current) {
  Grams total = 0;
  for (const HerdRow& herd : current.herds.rows) {
    if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
      continue;
    }
    total += HerdMilkDayGrams(config.livestock[herd.kind.value], herd, YieldFactor(config, herd));
  }
  return total;
}

}  // namespace core
