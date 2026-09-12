// The herd day (herd_system.h).
//
// Everything here is a COHORT flow: the row holds counts by rung, never a
// list of animals, so aging, birth and culling are deterministic fractional
// streams with a carry (mobs canon). A herd that matures one head every
// three days matures exactly one head every three days, not zero forever.

#include "herd_system.h"

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "herd_life.h"
#include "stable_horses.h"
#include "stock_ops.h"

namespace core {
namespace {

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
      place.unit_stock = &world.units.rows[unit_row].stock;
    }
    return place;
  }
  const std::uint32_t family_row = FindRow(world.families, herd.household);
  if (family_row != kNoRow) {
    place.pantry = &world.families.rows[family_row].pantry;
  }
  return place;
}

/// Only the KOLKHOZ herds' fodder is booked as `feed`: what a family's goat
/// eats came out of that family's pantry, and the pantry side of the year is
/// already counted as `eaten` and `issued`. Booking it twice would make the
/// settlement's food balance stop closing.
Grams TakeFeed(WorldState& world,
               const ProductionConfig& config,
               const HerdPlace& place,
               ResourceId resource,
               Grams wanted) {
  if (place.pantry != nullptr) {
    return TakeFromAmounts(*place.pantry, resource, wanted);
  }
  if (!place.at_unit) {
    return 0;
  }
  Grams taken =
      place.unit_stock != nullptr ? TakeFromAmounts(*place.unit_stock, resource, wanted) : 0;
  // Then the MANGER, and only then the general store. The hay of the whole
  // farm is delivered to one stock yard (stock_ops.h, FindStockYardRow), and
  // a herd standing at some OTHER barn — the team, once the kolkhoz yard is
  // raised — would otherwise starve two hundred metres from it. Phase 1 has
  // no logistics: everything the settlement holds is reachable, and this is
  // that stub said out loud.
  if (taken < wanted) {
    const std::uint32_t manger = FindStockYardRow(world, config);
    if (manger != kNoRow && &world.units.rows[manger].stock != place.unit_stock) {
      taken += TakeFromAmounts(world.units.rows[manger].stock, resource, wanted - taken);
    }
  }
  if (taken < wanted) {
    taken += TakeFromStorage(world, config, resource, wanted - taken);
  }
  AddLedgerAmount(world.ledger.current.feed, resource, taken);
  return taken;
}

// -- the day, step by step ---------------------------------------------------

/// Room under the roof, per unit, spent by the herds standing there in row
/// order. Phase 1 keeps one herd per unit, so the order never decides
/// anything; it is fixed all the same, because determinism is not allowed to
/// depend on that staying true.
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
void RunBilleting(const HerdRow& herd,
                  std::vector<float>& room,
                  const WorldState& world,
                  std::uint16_t& billeted) {
  billeted = 0;
  if (herd.household_owned != 0) {
    return;  // a family's own animals are home; there is nothing to billet
  }
  const auto heads = static_cast<float>(TotalHeads(herd));
  const std::uint32_t unit_row =
      herd.unit.value == kInvalidEntityIdValue ? kNoRow : FindRow(world.units, herd.unit);
  if (unit_row == kNoRow || unit_row >= room.size() || !(room[unit_row] > 0.0F)) {
    // A kolkhoz herd with no roof of its own — the sixteen start horses —
    // is billeted whole. That is the start canon, not a failure state.
    billeted = AsHeads(heads);
    return;
  }
  const float housed = heads < room[unit_row] ? heads : room[unit_row];
  room[unit_row] -= housed;
  billeted = AsHeads(heads - housed);
}

/// @brief Share of the draught animals that went out to work today, 0..1.
///
/// The wage ration of question Q2 is per head and per day: a horse in the
/// traces gets oats, a horse standing in the yard gets hay. The core knows
/// how many horses went out — one adult per horse work order, which is the
/// start canon's own arithmetic (livestock design §5) — but not WHICH ones,
/// because a work order names a field and a worker, never an animal. So the
/// ration is spread: on a day when five horses of sixteen are in the traces,
/// oats may cover five sixteenths of what they would cover on a full working
/// day, and hay carries the rest.
///
/// Read from `current` after the labor sub-step of the same sequential slot
/// has written today's orders (manual/54-modules.md §3).
/// @param horse_backed_days Optional out: today's assignment-days that a horse
///        ACTUALLY pulled — the mechanisation numerator (epochs design §6,
///        boss's decision of 2026-09-12). Filled from the same single walk
///        that feeds the oats, because the two are one question asked twice:
///        "how much of the pool was in the traces today". A second walk of
///        its own would be the same fact with two homes, and the two would
///        part company the first time one of them learned about a mower.
///
///        NOT the horse-work man-days themselves. `IsHorseWork` is a
///        `constexpr` over the KIND of work and calls ploughing horse work
///        in a village with an empty stable, so those man-days measure the
///        rotation. What is booked is the share of them the horses could
///        actually carry: more ploughmen than horses means the surplus
///        pulled by hand.
float WorkingShare(const WorldState& world,
                   const ProductionConfig& config,
                   float* horse_backed_days = nullptr) {
  // THE OUT-PARAM IS ANSWERED ON EVERY PATH, including the two that give up
  // early. It was left untouched there, and the doc's "stays at nothing" was
  // then a promise kept by the CALLER's initialiser — true today and true
  // only while every caller keeps writing one.
  if (horse_backed_days != nullptr) {
    *horse_backed_days = 0.0F;
  }
  if (config.horse_kind.value == kInvalidDefIdValue) {
    return 0.0F;
  }
  std::uint32_t horses = 0;
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.kind.value == config.horse_kind.value) {
      horses += herd.adult_count;
    }
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
  std::uint32_t working = 0;
  for (const ResidentRow& resident : world.residents.rows) {
    if (IsHorseWork(resident.work.kind)) {
      ++working;
    }
  }
  const float share = static_cast<float>(working) / static_cast<float>(horses);
  if (horse_backed_days != nullptr) {
    // Assignment-days: one per man ordered out today, and what the horses
    // can carry is the lesser of the two counts. The denominator is counted
    // in the same walk (see the caller) so that both halves of the ratio
    // are the same unit, taken at the same hour, by the same module.
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
};

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
/// @return true when the whole need was covered.
bool RunFeeding(const ProductionConfig& config,
                LivestockKindId kind_id,
                const HerdPlace& place,
                float need_units,
                float working_share,
                WorldState& world,
                WorkRation* work = nullptr) {
  if (!(need_units > 0.0F)) {
    return true;
  }
  float covered = 0.0F;
  float work_covered = 0.0F;
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
    // A reserve ration covers less than it weighs: the design says "with a
    // lowered effect" and names no number, so one multiplier stands in for
    // all of them until polish question P22n settles what it hides.
    const float value = config.feed_values[link.resource.value] *
                        (link.reserve != 0 ? config.farming.reserve_feed_factor : 1.0F);
    if (!(value > 0.0F)) {
      continue;
    }
    // The order says what to spend FIRST; the cap says how much of it the
    // animal can eat at all. Without the cap the model lies twice over: a
    // ruminant would live on grain alone, and the first horse in the village
    // would eat its whole year of oats by itself.
    const float room = need_units * link.max_share * (link.work_only != 0 ? working_share : 1.0F);
    float take_units = need_units - covered;
    take_units = take_units < room ? take_units : room;
    if (!(take_units > 0.0F)) {
      continue;
    }
    const Grams wanted = KilogramsToGrams(take_units / value);
    const Grams got = TakeFeed(world, config, place, link.resource, wanted);
    const float gained = static_cast<float>(got) / static_cast<float>(kGramsPerKilogram) * value;
    if (link.work_only != 0) {
      work_covered += gained;
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
  }
  // A hair of tolerance: the need is a float and the take is integer grams,
  // so an exactly-fed herd can land a milligram short of its own norm.
  return covered + 0.001F >= need_units;
}

/// What the day's produce is multiplied by. Two leaks, both of them the
/// design's: a hungry herd gives less at once, and a billeted head gives
/// less because part of what it makes settles in the yard it stands in.
float YieldFactor(const ProductionConfig& config, const HerdRow& herd) {
  float factor = herd.unfed_days > 0.0F ? config.farming.unfed_produce_factor : 1.0F;
  const auto total = static_cast<float>(TotalHeads(herd));
  if (total > 0.0F && herd.billeted_count > 0) {
    const float billeted_share = static_cast<float>(herd.billeted_count) / total;
    factor *= 1.0F - (billeted_share * (1.0F - config.farming.billet_yield_factor));
  }
  return factor;
}

void RunProduce(const ProductionConfig& config,
                const LivestockDef& kind,
                const HerdRow& herd,
                const HerdPlace& place,
                WorldState& world) {
  const float factor = YieldFactor(config, herd);
  const auto adults = static_cast<float>(herd.adult_count);
  const auto females = static_cast<float>(kind.sexed != 0 ? herd.adult_count - herd.adult_male_count
                                                          : herd.adult_count);
  const auto year = static_cast<float>(kDaysPerYear);
  // Milk counts females only; eggs, wool and manure count every adult.
  // A litre of milk is a kilogram in the store (quantities.h).
  DeliverProduce(world,
                 config,
                 place,
                 config.milk_resource,
                 KilogramsToGrams(kind.milk_l_per_year * females * factor / year));
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
                      WorldState& world) {
  if (!(kind.household_cap_heads > 0.0F)) {
    return;
  }
  const auto cap = static_cast<std::uint16_t>(kind.household_cap_heads);
  const float adult_from_years = kind.adult_from_game_months / static_cast<float>(kMonthsPerYear);
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
      if (grown) {
        Slaughter(config, kind, place, 1, world);
      }
    }
  }
  herd.adult_male_count = TargetMales(kind, herd.adult_count);
}

}  // namespace

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
                    std::uint8_t month) {
  const float heads = static_cast<float>(herd.adult_count) +
                      static_cast<float>(herd.juvenile_count) * config.farming.juvenile_feed_factor;
  float need = heads * kind.feed_units_per_game_day;
  if (MonthInRange(month, config.farming.pasture_from_month, config.farming.pasture_to_month)) {
    need *= 1.0F - kind.pasture_coverage_summer;
  }
  return need > 0.0F ? need : 0.0F;
}

void RunHerdDay(const ProductionConfig& config, WorldState& current) {
  if (config.livestock.empty()) {
    return;  // a table-less world keeps no animals
  }
  StableHorses(config, current);
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  std::vector<float> room = RoofRoom(current, config);
  float horse_backed_days = 0.0F;
  const float working_share = WorkingShare(current, config, &horse_backed_days);
  // BOTH HALVES OF THE MECHANISATION SHARE, here and only here (epochs
  // design §6; ledger_state.h). The denominator was booked by core_labor for
  // one afternoon, from delivered norm-days — a different unit at a
  // different hour on the other side of the year's close, which let the
  // quotient pass 1 and made the drift invisible, because a quotient of two
  // wrong things still looks like a quotient.
  current.ledger.current.horse_backed_assignment_days += horse_backed_days;
  // The day's work ration of the working stock, summed over every herd the
  // walk below feeds and turned into the traction ration at the end of it.
  WorkRation work;
  for (const ResidentRow& resident : current.residents.rows) {
    if (resident.work.kind != WorkKind::kNone) {
      current.ledger.current.total_assignment_days += 1.0F;
    }
  }
  const bool stable_built = StableBuilt(current, config);
  std::vector<HerdRow> gifts;  // appended after the walk; see GiveToNeighbour
  GiftQueues queues = CollectGiftQueues(current, config);
  for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
    HerdRow& herd = current.herds.rows[row];
    if (herd.kind.value >= config.livestock.size()) {
      continue;
    }
    const LivestockDef& kind = config.livestock[herd.kind.value];
    // The sire count is a function of the herd, not a thing it remembers:
    // re-derived here so that every path into this row — a merge, a rescue
    // from starvation, a table edit — leaves the same invariant behind, and
    // "females" can never come out negative.
    herd.adult_male_count = TargetMales(kind, herd.adult_count);
    const HerdPlace place = PlaceOf(current, config, herd);
    RunBilleting(herd, room, current, herd.billeted_count);
    // The yard's hens, ducks and pig feed themselves (question Q1): range,
    // scraps and the garden, and the winter handful of grain out of the
    // family's own ration, which the meal already counts. They are FED, not
    // starving on an empty manger — the flag says the store is not their
    // source, not that they eat nothing.
    const bool self_fed = herd.household_owned != 0 && kind.household_self_fed != 0;
    const bool fed = self_fed || RunFeeding(config,
                                            herd.kind,
                                            place,
                                            FeedNeedUnits(config, kind, herd, month),
                                            working_share,
                                            current,
                                            &work);
    herd.unfed_days = fed ? 0.0F : herd.unfed_days + 1.0F;
    if (fed) {
      herd.hunger_progress = 0.0F;  // a fed day clears the debt, not just the count
    } else {
      current.ledger.current.herd_hungry_head_days += static_cast<float>(TotalHeads(herd));
    }
    RunProduce(config, kind, herd, place, current);
    RunMaturation(config, kind, place, herd, current);
    if (herd.household_owned != 0) {
      PlaceSurplusHead(config, kind, place, queues, gifts, herd, current);
    }
    const HerdId herd_id = current.herds.row_ids[row];
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
    RunAutumnSlaughter(config, kind, herd.kind, place, herd, current, current.calendar);
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
  for (const HerdRow& gift : gifts) {
    AppendRow(current.herds, gift);
  }
}

}  // namespace core
