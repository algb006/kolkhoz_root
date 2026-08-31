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
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "stock_ops.h"

namespace core {
namespace {

constexpr float kGameMonthsPerYear = 12.0F;

bool MonthInRange(std::uint8_t month, std::uint8_t from, std::uint8_t to) {
  return month >= from && month <= to;
}

std::uint16_t AsHeads(float value) {
  if (!(value > 0.0F)) {
    return 0;
  }
  return value > 65000.0F ? 65000U : static_cast<std::uint16_t>(value);
}

/// @brief Moves `count` whole heads out of a rung, never below zero.
std::uint16_t TakeHeads(std::uint16_t& rung, std::uint16_t count) {
  const std::uint16_t taken = count < rung ? count : rung;
  rung = static_cast<std::uint16_t>(rung - taken);
  return taken;
}

/// @brief One fractional stream with a carry: adds `rate` heads a day to the
/// accumulator and returns the whole heads that came due.
std::uint16_t DrawFlow(float& accumulator, float rate) {
  accumulator += rate;
  if (!(accumulator >= 1.0F)) {
    return 0;
  }
  const auto whole = static_cast<float>(static_cast<std::uint32_t>(accumulator));
  accumulator -= whole;
  return AsHeads(whole);
}

/// @brief How many adults the herd keeps as males. A SHARE, not a count:
/// "one bull" stops being right the moment the herd grows. A herd whose
/// share is zero keeps no sire at all and therefore never breeds — which is
/// the goat's case, and correct: a yard keeps two does and borrows a buck,
/// and phase 1 does not model the borrowing. A herd with a positive share
/// always keeps at least one, or it could never breed again.
std::uint16_t TargetMales(const LivestockDef& kind, std::uint16_t adults) {
  if (kind.sexed == 0 || adults == 0 || !(kind.males_share > 0.0F)) {
    return 0;
  }
  const auto wanted = AsHeads((static_cast<float>(adults) * kind.males_share) + 0.5F);
  const std::uint16_t at_least_one = wanted == 0 ? 1U : wanted;
  return at_least_one < adults ? at_least_one : adults;
}

std::uint16_t TotalHeads(const HerdRow& herd) {
  return static_cast<std::uint16_t>(herd.newborn_count + herd.juvenile_count + herd.adult_count);
}

/// Where a herd's feed comes from and where its produce goes. A kolkhoz herd
/// draws on the shared store and delivers to it (the phase-1 logistics stub
/// is instant); a herd at a family yard lives out of that family's pantry —
/// which is what "private livestock is ONE aggregate consumer, fed out of
/// the pantry the monthly issue fills" means in code (livestock design §11).
struct HerdPlace {
  ResourceAmounts* pantry = nullptr;  ///< Non-null for a household herd.

  /// The stock of the unit the herd stands at, if any. A herd eats out of
  /// its own barn before it sends to the shared store — which is not a
  /// nicety: hay is delivered to the stock yard by the harvest, and the yard
  /// is not a "storing" unit at all (its table capacity is in HEADS, not
  /// tonnes), so a herd that only knew the shared store would stand beside
  /// a full manger and starve.
  ResourceAmounts* unit_stock = nullptr;

  bool at_unit = false;
};

HerdPlace PlaceOf(WorldState& world, const HerdRow& herd) {
  HerdPlace place;
  // OWNERSHIP decides the purse, not the address. A kolkhoz cow billeted at
  // a yard still eats the farm's fodder and still gives the farm its milk;
  // only the family's own goat lives out of the family's pantry.
  if (herd.household_owned == 0) {
    place.at_unit = true;
    const std::uint32_t unit_row =
        herd.unit.value == kInvalidEntityIdValue ? kNoRow : FindRow(world.units, herd.unit);
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
  const Grams from_barn =
      place.unit_stock != nullptr ? TakeFromAmounts(*place.unit_stock, resource, wanted) : 0;
  const Grams taken =
      from_barn >= wanted
          ? from_barn
          : from_barn + TakeFromStorage(world, config, resource, wanted - from_barn);
  AddLedgerAmount(world.ledger.current.feed, resource, taken);
  return taken;
}

void DeliverProduce(WorldState& world,
                    const ProductionConfig& config,
                    const HerdPlace& place,
                    ResourceId resource,
                    Grams amount) {
  if (amount <= 0) {
    return;
  }
  if (place.pantry != nullptr) {
    AddToStock(*place.pantry, resource, amount);
    AddLedgerAmount(world.ledger.current.yard_produce, resource, amount);
    return;
  }
  AddLedgerAmount(world.ledger.current.herd_produce, resource, amount);
  const std::uint32_t store = FindStorageRow(world, config);
  if (store != kNoRow) {
    AddToStock(world.units.rows[store].stock, resource, amount);
  }
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
    room[row] = config.unit_types[unit.type.value].livestock_capacity_head;
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

/// Feeds one herd down the feeding order of feed_links.csv. ROW ORDER IS THE
/// PRIORITY — staple before reserve, own feed before bought concentrate,
/// fodder grain before bread grain — and nothing is sorted here: the order
/// arrives from the design db and sorting it again would put the canon in
/// two places.
/// @return true when the whole need was covered.
bool RunFeeding(const ProductionConfig& config,
                LivestockKindId kind_id,
                const HerdPlace& place,
                float need_units,
                WorldState& world) {
  if (!(need_units > 0.0F)) {
    return true;
  }
  float covered = 0.0F;
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
    const float room = need_units * link.max_share;
    float take_units = need_units - covered;
    take_units = take_units < room ? take_units : room;
    if (!(take_units > 0.0F)) {
      continue;
    }
    const Grams wanted = KilogramsToGrams(take_units / value);
    const Grams got = TakeFeed(world, config, place, link.resource, wanted);
    covered += static_cast<float>(got) / static_cast<float>(kGramsPerKilogram) * value;
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

/// Pays out a slaughter: meat, and whatever else the carcass gives.
void Slaughter(const ProductionConfig& config,
               const LivestockDef& kind,
               const HerdPlace& place,
               std::uint16_t heads,
               WorldState& world) {
  if (heads == 0) {
    return;
  }
  const auto count = static_cast<float>(heads);
  DeliverProduce(
      world, config, place, config.meat_resource, KilogramsToGrams(kind.meat_kg_per_head * count));
  DeliverProduce(world,
                 config,
                 place,
                 config.hide_resource,
                 KilogramsToGrams(kind.hide_pieces_per_head * count));
  DeliverProduce(world,
                 config,
                 place,
                 config.pelt_resource,
                 KilogramsToGrams(kind.pelt_pieces_per_head * count));
  DeliverProduce(
      world, config, place, config.down_resource, KilogramsToGrams(kind.down_kg_per_head * count));
}

/// Newborn -> juvenile -> adult, as two fractional streams. Half of what
/// reaches adulthood is male, and the herd keeps only its share of sires:
/// the surplus is the "males beyond the one sire" case of the structural
/// slaughter, and it carries its own fraction so that no head is lost to
/// rounding.
void RunMaturation(const ProductionConfig& config,
                   const LivestockDef& kind,
                   const HerdPlace& place,
                   HerdRow& herd,
                   WorldState& world) {
  const auto month_days = static_cast<float>(kDaysPerMonth);
  const float newborn_days = kind.newborn_game_months * month_days;
  const float juvenile_days = (kind.adult_from_game_months - kind.newborn_game_months) * month_days;
  if (newborn_days > 0.0F && herd.newborn_count > 0) {
    const std::uint16_t moved =
        DrawFlow(herd.newborn_progress, static_cast<float>(herd.newborn_count) / newborn_days);
    herd.juvenile_count =
        static_cast<std::uint16_t>(herd.juvenile_count + TakeHeads(herd.newborn_count, moved));
  }
  if (!(juvenile_days > 0.0F) || herd.juvenile_count == 0) {
    return;
  }
  const std::uint16_t moved =
      DrawFlow(herd.juvenile_progress, static_cast<float>(herd.juvenile_count) / juvenile_days);
  const std::uint16_t grown = TakeHeads(herd.juvenile_count, moved);
  if (grown == 0) {
    return;
  }
  herd.adult_count = static_cast<std::uint16_t>(herd.adult_count + grown);
  herd.adult_age_game_years_total +=
      static_cast<float>(grown) * kind.adult_from_game_months / kGameMonthsPerYear;
  const std::uint16_t males_target = TargetMales(kind, herd.adult_count);
  if (kind.sexed == 0) {
    herd.adult_male_count = 0;
    return;
  }
  const auto room_for_males = static_cast<float>(
      males_target > herd.adult_male_count ? males_target - herd.adult_male_count : 0);
  const std::uint16_t culled =
      DrawFlow(herd.cull_progress, (static_cast<float>(grown) * 0.5F) - room_for_males);
  const std::uint16_t gone = TakeHeads(herd.adult_count, culled);
  // The male count is derived AFTER the cull, not before it: taking the
  // surplus out of adult_count would otherwise leave a target computed on
  // the larger herd, and adult_male_count could stand above adult_count —
  // breaking the invariant herd_state.h states and making "females" come
  // out negative for a step.
  herd.adult_male_count = TargetMales(kind, herd.adult_count);
  if (gone > 0) {
    herd.adult_age_game_years_total -=
        static_cast<float>(gone) * kind.adult_from_game_months / kGameMonthsPerYear;
    world.ledger.current.herd_culled += gone;
    Slaughter(config, kind, place, gone, world);
  }
}

/// Offspring. Four gates, and every one of them is canon: an adult male for
/// a sexed kind, a roof (horses need a stable, which phase 1 does not have),
/// ROOM under that roof, and the calving season.
/// @param book The year's ledger. Passed rather than the whole world: this
/// function has no business reaching anywhere else in the state, and the
/// narrow parameter says so.
void RunBirths(const ProductionConfig& config,
               const LivestockDef& kind,
               LivestockKindId kind_id,
               HerdRow& herd,
               std::uint8_t month,
               YearLedger& book) {
  if (herd.household_owned != 0) {
    // A private yard does not grow on its own in phase 1. The canon says a
    // family's own cow arrives later — by a calf from the kolkhoz young, by
    // a neighbour's gift, by the Epoch-II market — and none of those three
    // exists yet (manual/66-food-model.md §10). Without this gate a yard's
    // eight hens become a hundred and twenty in a year, which is not a
    // balance, it is an absence of one.
    return;
  }
  if (herd.billeted_count > 0) {
    return;  // no room: they breed under a roof and only when there is space
  }
  if (kind.sexed != 0 && herd.adult_male_count == 0) {
    return;
  }
  if (kind_id.value == config.horse_kind.value && config.stable_type.value == kInvalidDefIdValue) {
    return;  // no stable, no foals — exactly what the start canon asks for
  }
  if (!MonthInRange(month, config.farming.birth_from_month, config.farming.birth_to_month)) {
    return;
  }
  const auto females = static_cast<float>(kind.sexed != 0 ? herd.adult_count - herd.adult_male_count
                                                          : herd.adult_count);
  if (!(females > 0.0F) || !(kind.births_per_game_year > 0.0F)) {
    return;
  }
  // The yearly rate falls inside the calving band, not across the year: the
  // total for the year is what the balance eats, and the season is canon.
  const auto band_days = static_cast<float>(
      (config.farming.birth_to_month - config.farming.birth_from_month + 1U) * kDaysPerMonth);
  const float rate = females * kind.births_per_game_year * kind.litter_heads / band_days;
  const std::uint16_t born = DrawFlow(herd.birth_progress, rate);
  if (born == 0) {
    return;
  }
  book.herd_births += born;
  // A kind with no newborn rung (poultry) hatches straight into juveniles.
  if (kind.newborn_game_months > 0.0F) {
    herd.newborn_count = static_cast<std::uint16_t>(herd.newborn_count + born);
  } else {
    herd.juvenile_count = static_cast<std::uint16_t>(herd.juvenile_count + born);
  }
}

/// Age takes the herd from the top of its lifespan band: the hazard is zero
/// at the lower end and certain at the upper one. The draw is the world's
/// sequential RNG, so a replay lands on the same animals.
void RunAgeDeaths(const LivestockDef& kind, HerdRow& herd, WorldState& world) {
  if (herd.adult_count == 0) {
    return;
  }
  const auto adults = static_cast<float>(herd.adult_count);
  herd.adult_age_game_years_total += adults / static_cast<float>(kDaysPerYear);
  const float band = kind.life_game_years_max - kind.life_game_years_min;
  if (!(kind.life_game_years_max > 0.0F)) {
    return;  // a kind whose lifespan the table does not name does not age out
  }
  const float mean_age = herd.adult_age_game_years_total / adults;
  float hazard_per_year = 0.0F;
  if (band > 0.0F) {
    hazard_per_year = (mean_age - kind.life_game_years_min) / band;
  } else if (mean_age >= kind.life_game_years_max) {
    hazard_per_year = 1.0F;  // a band of zero width is a hard age limit
  }
  hazard_per_year = hazard_per_year < 0.0F ? 0.0F : hazard_per_year;
  hazard_per_year = hazard_per_year > 1.0F ? 1.0F : hazard_per_year;
  const float expected = adults * hazard_per_year / static_cast<float>(kDaysPerYear);
  auto dead = static_cast<std::uint16_t>(expected);
  if (NextRandomUnitFloat(world.rng) < expected - static_cast<float>(dead)) {
    dead = static_cast<std::uint16_t>(dead + 1);
  }
  const std::uint16_t gone = TakeHeads(herd.adult_count, dead);
  if (gone == 0) {
    return;
  }
  world.ledger.current.herd_deaths_age += gone;
  herd.adult_age_game_years_total -= static_cast<float>(gone) * mean_age;
  herd.adult_age_game_years_total =
      herd.adult_age_game_years_total < 0.0F ? 0.0F : herd.adult_age_game_years_total;
  const std::uint16_t males_gone = TakeHeads(herd.adult_male_count, gone);
  (void)males_gone;  // the male count is re-derived from the share below
  herd.adult_male_count = TargetMales(kind, herd.adult_count);
}

/// A herd left hungry long enough starts to lose heads. The ladder is the
/// design's: produce drops from the first hungry day, deaths only after the
/// threshold. The cold ladder of question 99 is NOT here — every phase-1
/// herd stands under a roof and unit temperatures do not exist.
void RunHungerDeaths(const ProductionConfig& config,
                     const LivestockDef& kind,
                     HerdRow& herd,
                     YearLedger& book) {
  if (herd.unfed_days <= config.farming.unfed_death_after_days) {
    return;
  }
  // Death is a flow like every other one in this file and carries its
  // fraction the same way (HerdRow::hunger_progress). Truncating instead —
  // the shape this had until the delivery cycle caught it — took nobody at
  // all from a herd of under twenty head, and twenty is above the size of
  // most herds at the start. A starving barn stood for ever at half milk and
  // full strength: not a gentler rule, an absent one.
  //
  // The rate is the share of the WHOLE herd, and the toll falls on the young
  // first. That ordering is a choice this code has to make and the design
  // does not; it is the one a farm makes.
  const float share = config.farming.unfed_death_percent_per_day / 100.0F;
  const std::uint16_t owed_at_first =
      DrawFlow(herd.hunger_progress, static_cast<float>(TotalHeads(herd)) * share);
  std::uint16_t owed = owed_at_first;
  owed = static_cast<std::uint16_t>(owed - TakeHeads(herd.newborn_count, owed));
  owed = static_cast<std::uint16_t>(owed - TakeHeads(herd.juvenile_count, owed));
  const std::uint16_t adults_gone = TakeHeads(herd.adult_count, owed);
  // What the hunger actually took, not what it asked for: a herd with fewer
  // heads than the day owed loses only the heads it had.
  book.herd_deaths_hunger += static_cast<std::uint32_t>(owed_at_first - owed) + adults_gone;
  if (adults_gone > 0 && herd.adult_count > 0) {
    const float mean =
        herd.adult_age_game_years_total / static_cast<float>(herd.adult_count + adults_gone);
    herd.adult_age_game_years_total -= static_cast<float>(adults_gone) * mean;
  } else if (adults_gone > 0) {
    herd.adult_age_game_years_total = 0.0F;
  }
  // Re-derive the sires, like every other culler here does. Without it a
  // herd starved down to one or two adults and then rescued keeps a male
  // count from its fat years: females comes out non-positive, so it gives no
  // milk and bears nothing, for ever. The carry above is what made that
  // reachable — the old truncating rule could never take an adult rung down
  // into the range where it matters.
  herd.adult_male_count = TargetMales(kind, herd.adult_count);
}

/// The autumn pig slaughter (livestock design §6): everything but the sows
/// and the sire goes to meat. How many sows the herd keeps is the one number
/// the design does not name, so it is a table knob — ASSUMPTION, and the
/// balance run is what will move it.
void RunAutumnSlaughter(const ProductionConfig& config,
                        const LivestockDef& kind,
                        LivestockKindId kind_id,
                        const HerdPlace& place,
                        HerdRow& herd,
                        WorldState& world,
                        const CalendarState& calendar) {
  const auto month = static_cast<std::uint8_t>(calendar.date.month);
  if (kind_id.value != config.pig_kind.value || month != config.farming.pig_slaughter_month ||
      calendar.date.day_in_month != 0) {
    return;
  }
  const std::uint16_t sows =
      AsHeads(static_cast<float>(herd.adult_count) * config.farming.sow_keep_share);
  const std::uint16_t males = TargetMales(kind, herd.adult_count);
  const auto keep = static_cast<std::uint16_t>(sows + males);
  std::uint16_t gone = TakeHeads(herd.juvenile_count, herd.juvenile_count);
  if (herd.adult_count > keep) {
    const auto surplus = static_cast<std::uint16_t>(herd.adult_count - keep);
    const float mean = herd.adult_age_game_years_total / static_cast<float>(herd.adult_count);
    gone = static_cast<std::uint16_t>(gone + TakeHeads(herd.adult_count, surplus));
    herd.adult_age_game_years_total -= static_cast<float>(surplus) * mean;
    herd.adult_male_count = TargetMales(kind, herd.adult_count);
  }
  world.ledger.current.herd_culled += gone;
  Slaughter(config, kind, place, gone, world);
}

}  // namespace

void RunHerdDay(const ProductionConfig& config, WorldState& current) {
  if (config.livestock.empty()) {
    return;  // a table-less world keeps no animals
  }
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  std::vector<float> room = RoofRoom(current, config);
  for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
    HerdRow& herd = current.herds.rows[row];
    if (herd.kind.value >= config.livestock.size()) {
      continue;
    }
    const LivestockDef& kind = config.livestock[herd.kind.value];
    const HerdPlace place = PlaceOf(current, herd);
    RunBilleting(herd, room, current, herd.billeted_count);
    const bool fed =
        RunFeeding(config, herd.kind, place, FeedNeedUnits(config, kind, herd, month), current);
    herd.unfed_days = fed ? 0.0F : herd.unfed_days + 1.0F;
    if (fed) {
      herd.hunger_progress = 0.0F;  // a fed day clears the debt, not just the count
    } else {
      current.ledger.current.herd_hungry_head_days += static_cast<float>(TotalHeads(herd));
    }
    RunProduce(config, kind, herd, place, current);
    RunMaturation(config, kind, place, herd, current);
    RunBirths(config, kind, herd.kind, herd, month, current.ledger.current);
    RunAgeDeaths(kind, herd, current);
    RunHungerDeaths(config, kind, herd, current.ledger.current);
    RunAutumnSlaughter(config, kind, herd.kind, place, herd, current, current.calendar);
  }
}

}  // namespace core
