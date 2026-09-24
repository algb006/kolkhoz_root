// One head's own life (herd_life.h): born, grown, dead of age, dead of
// hunger, under the knife — and what is left of it afterwards.
//
// Everything here is a COHORT flow: the row holds counts by rung, never a
// list of animals, so aging, birth and culling are deterministic fractional
// streams with a carry (mobs canon). A herd that matures one head every
// three days matures exactly one head every three days, not zero forever.

#include "herd_life.h"

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/herd_age_band.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "field_haul.h"
#include "stock_ops.h"

namespace core {
namespace {

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

/// @brief The herd's yearly death hazard, averaged over the ages IN it.
///
/// THE MEAN IS NOT A HERD, and reading the hazard at the mean age was the
/// second reconciliation pass's own finding (69-reconciliation.md, open item
/// "the herd age model"). One number per herd is all the row keeps, so the
/// hazard used to be evaluated at the mean and charged to every head alike.
/// That is wrong in both directions at once and unstable besides:
///   * a herd whose mean sits below the lifespan band loses NOBODY, though a
///     third of it may be past its years;
///   * a herd whose mean sits inside the band loses half of itself a year,
///     yearlings and all;
///   * and since the dead were removed AT the mean, the mean never came
///     down. Ageing pushed it up a year every year, the hazard climbed with
///     it, and any herd that could not breed fast enough spiralled to
///     nothing — the thirty-nine start cows reached seven.
///
/// So the hazard is INTEGRATED over the ages the herd is assumed to hold:
/// spread evenly across the whole adult band, which is what the canon
/// asserts at the founding ("the herd and the team are not the same age") and
/// what breeding keeps true afterwards. The clipping is the point — the part
/// of the spread below the band contributes nothing and the part above it
/// contributes certainty, and neither is visible at the mean.
///
/// Since 0.35.16 the spread is not assumed: it is the herd's own age band,
/// youngest to oldest (core_common/herd_age_band.h).
///
/// @param mean_age  The band's middle, game years.
/// @param half_width Half the band, game years.
/// @return Deaths per adult per game year, 0..1.
float AverageAgeHazard(float mean_age, float half_width, float low, float high) {
  const float band = high - low;
  if (!(band > 0.0F)) {
    return mean_age >= high ? 1.0F : 0.0F;  // zero width: a hard age limit
  }
  const float first = mean_age - half_width;
  const float last = mean_age + half_width;
  if (!(last > first)) {
    const float point = (mean_age - low) / band;
    return point < 0.0F ? 0.0F : (point > 1.0F ? 1.0F : point);
  }
  // Below `low` the hazard is zero and contributes nothing; inside the band
  // it is linear, so its integral is the midpoint value times the width;
  // above `high` it is one.
  const float ramp_from = first > low ? first : low;
  const float ramp_to = last < high ? last : high;
  float total = 0.0F;
  if (ramp_to > ramp_from) {
    total += (((ramp_from + ramp_to) * 0.5F) - low) / band * (ramp_to - ramp_from);
  }
  const float over_from = first > high ? first : high;
  if (last > over_from) {
    total += last - over_from;
  }
  const float hazard = total / (last - first);
  return hazard < 0.0F ? 0.0F : (hazard > 1.0F ? 1.0F : hazard);
}

}  // namespace

std::uint16_t AsHeads(float value) {
  if (!(value > 0.0F)) {
    return 0;
  }
  return value > 65000.0F ? 65000U : static_cast<std::uint16_t>(value);
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
  // AND NO "AT LEAST ONE" SINCE 2026-09-16 (boss, parcel 20). The floor was
  // right while heads could only be BORN — a herd that could never keep a
  // sire could never breed again, and nothing outside the herd could mend
  // that. The district's livestock window is that something: a farm with no
  // producer orders one, and it is the chairman's move to make.
  //
  // THE FLOOR WAS ALSO TAKING THE OTHER HALF OF HIS CHOICE. The order carries
  // the sex of the head bought, and while this returned one for any herd with
  // adults, a bought mare became a stallion by the next morning. Measured:
  // the way out of a dead team needs a stallion AND a mare, and with the
  // floor in place asking for either changed nothing at all.
  const auto wanted = AsHeads((static_cast<float>(adults) * kind.males_share) + 0.5F);
  return wanted < adults ? wanted : adults;
}

/// @brief The sires left after `gone` adults leave a herd of `adults_before`,
///        in proportion, rounded to the nearest head and never above the
///        survivors.
///
/// WHY PROPORTION AND NOT A RE-DERIVE. The male count is a CARRIED quantity
/// since 2026-09-16: births add to it, purchases add exactly what was bought,
/// and nothing recomputes it from the herd's size — because recomputing is
/// what erased the chairman's choice every morning. A carried count needs the
/// losses taken out of it honestly, or the two numbers drift apart: taking
/// every death out of the females leaves the last survivor of a dying herd
/// always male, and taking none out leaves a herd starved down to two adults
/// carrying a male count from its fat years — which is the exact bug the
/// re-derive at the hunger path was written to prevent, and the reason it
/// cannot simply be deleted.
std::uint16_t MalesAfterLoss(std::uint16_t males, std::uint16_t adults_before, std::uint16_t gone) {
  if (males == 0 || gone == 0 || adults_before == 0) {
    return males;
  }
  if (gone >= adults_before) {
    return 0;
  }
  const auto left = static_cast<std::uint16_t>(adults_before - gone);
  const float share = static_cast<float>(males) / static_cast<float>(adults_before);
  const auto kept = AsHeads((static_cast<float>(left) * share) + 0.5F);
  return kept < left ? kept : left;
}

std::uint16_t TotalHeads(const HerdRow& herd) {
  return static_cast<std::uint16_t>(herd.newborn_count + herd.juvenile_count + herd.adult_count);
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
  // THROUGH THE DOOR, into its home and within the ceiling (boss, parcel
  // 408). Until 2026-09-15 the kolkhoz herd's produce went into the first
  // numbered store whatever it was and however full: milk and meat into the
  // church. What has no home or no room is lost the same day, and said so —
  // milk with no ice house sours.
  const Grams placed = DeliverToStores(world, config, resource, amount);
  AddLedgerAmount(world.ledger.current.lost_no_room, resource, amount - placed);
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
                   HerdId herd_id,
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
  const float entry_age = kind.adult_from_game_months / static_cast<float>(kMonthsPerYear);
  WidenAdultAgeBand(herd, herd.adult_count, entry_age, entry_age);
  herd.adult_count = static_cast<std::uint16_t>(herd.adult_count + grown);
  herd.adult_age_game_years_total += static_cast<float>(grown) * entry_age;
  const std::uint16_t males_target = TargetMales(kind, herd.adult_count);
  if (kind.sexed == 0) {
    herd.adult_male_count = 0;
    return;
  }
  const auto room_for_males = static_cast<float>(
      males_target > herd.adult_male_count ? males_target - herd.adult_male_count : 0);
  const std::uint16_t culled =
      DrawFlow(herd.cull_progress, (static_cast<float>(grown) * 0.5F) - room_for_males);
  // THE SIRES THE HERD KEEPS OFF WHAT MATURED, added rather than re-derived:
  // as many as the share leaves room for, and the cull below takes the rest
  // to meat. Half of what grows up is male by nature, and a herd keeps only
  // its share of them.
  const auto kept = static_cast<std::uint16_t>(
      std::min(static_cast<float>(grown), room_for_males < 0.0F ? 0.0F : room_for_males));
  herd.adult_male_count = static_cast<std::uint16_t>(herd.adult_male_count + kept);
  const std::uint16_t gone = TakeHeads(herd.adult_count, culled);
  // THE CULL TAKES MALES THE COUNT NEVER HELD, so nothing comes out of it
  // here. The draw above is `grown * 0.5 - room_for_males`: the young males
  // the herd had no room for, which is exactly the ones `kept` did not add.
  //
  // Subtracting `gone` from the sires was this block's first draft and the
  // measurement caught it in one run: with a team of two the share leaves
  // room for no sire at all, so `kept` was nil while `gone` was one — and the
  // stallion the chairman had just BOUGHT was culled as surplus. Twelve years
  // later the run showed three horses and no sire among them.
  herd.adult_male_count = std::min(herd.adult_male_count, herd.adult_count);
  if (gone > 0) {
    herd.adult_age_game_years_total -=
        static_cast<float>(gone) * kind.adult_from_game_months / static_cast<float>(kMonthsPerYear);
    world.ledger.current.herd_culled += gone;
    // SAID, AND BOOKED BY KIND (boss, boss-core-epoch1-5 seq 43 and 45;
    // 0.35.3): until then the cull wrote herd_culled alone and said nothing.
    AddLedgerHeads(world.ledger.current.herd_males_culled, herd.kind, gone);
    SimEvent& event = EmitEvent(world, EventKind::kHerdMalesCulled);
    event.herd = herd_id;
    event.amount = static_cast<std::int64_t>(gone);
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
               HerdId herd_id,
               bool in_birth_season,
               bool stable_built,
               WorldState& world,
               YearLedger& book) {
  if (herd.household_owned != 0) {
    // A YARD BREEDS, and only its cap stops it (boss answer Q7,
    // 2026-08-31). This used to be a flat block — a stopgap for the missing
    // cap, put in the wrong place: without a ceiling eight hens become a
    // hundred and twenty in a year, so stage 6 shut the door instead of
    // building the wall. The measured cost of that was a yard standing empty
    // by the eighth year and a third of the village's table gone with it.
    //
    // THE CAP DOES NOT STOP THE BREEDING — it stops the KEEPING, and the
    // canon is explicit about the difference: "offspring appears by itself;
    // more than the limit may not be kept; here is where the young go."
    // Gating the birth instead was tried both ways and both are wrong. Gate
    // on the whole herd and a two-goat yard never breeds while it is full,
    // so it can only ever lose: one doe dies outside the calving season, the
    // other follows, and the yard is empty for good because nobody in the
    // village has a spare. Gate on a pipeline and a hen house — a hen is
    // grown in one game month — hatches and kills its whole flock every
    // forty days.
    //
    // So the yard breeds, and PlaceSurplusHead below walks the surplus to a
    // neighbour or takes it for meat. That surplus is also the only thing
    // that ever restocks a yard that has lost its animals.
    if (!(kind.household_cap_heads > 0.0F)) {
      return;
    }
  } else if (herd.billeted_count > 0) {
    return;  // no room: they breed under a roof and only when there is space
  } else if (herd.fed_share < config.farming.calving_fed_share_floor) {
    // A HUNGRY HERD DOES NOT CALVE (boss, boss-core-epoch1-2 seq 19, B): the
    // natural brake. The billet gate above was the only one, so a herd given
    // every yard of its kind grew to three times what the hay fed, starved
    // and failed the milk position named off its January size — 41 plan
    // years on nine seeds. A day short of the ration adds no birth progress.
    return;
  }
  // A sire is required only of a kind that KEEPS sires. `males_share == 0`
  // says the table does not model males for this kind at all — the goat is
  // the case: two does at a yard and the buck is somebody else's business,
  // which is why the canon gives a yard "two goats" and never a billy. With
  // the share read as a plain "sexed" flag the yards' goats could never
  // breed, and they died out just as surely as when breeding was blocked
  // outright (manual/balance/69-reconciliation.md §7).
  if (kind.sexed != 0 && kind.males_share > 0.0F && herd.adult_male_count == 0) {
    return;
  }
  if (kind_id.value == config.horse_kind.value && !stable_built) {
    // No stable, no foals (livestock design §5). Until the yard reaches its
    // second step the team only ages — and since ploughing is horse work,
    // the stable is the condition on which the farm goes on ploughing at
    // all. The start canon asks for exactly this, and boss's answer of
    // 2026-08-31 wrote down the deadline that follows from it.
    return;
  }
  // THE SEASON IS DECIDED BY THE WALK, NOT ASKED FOR HERE (2026-09-07).
  // This used to call MonthInRange out of herd_system.h — the one arc that
  // pointed from nature back up at the household, and the compiler would
  // have allowed every other such call along with it. The answer now arrives
  // the way `stable_built` beside it already did.
  if (!in_birth_season) {
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
  // Said where it happens, with the herd it happened to. Two hundred and
  // thirty head were born over four hundred days and the journal carried
  // none of them (boss, 2026-09-05).
  SimEvent& event = EmitEvent(world, EventKind::kHerdBorn);
  event.herd = herd_id;
  event.amount = static_cast<std::int64_t>(born);
  // A kind with no newborn rung (poultry) hatches straight into juveniles.
  if (kind.newborn_game_months > 0.0F) {
    herd.newborn_count = static_cast<std::uint16_t>(herd.newborn_count + born);
  } else {
    herd.juvenile_count = static_cast<std::uint16_t>(herd.juvenile_count + born);
  }
}

float MeanAdultAgeYears(const HerdRow& herd) {
  return herd.adult_count == 0
             ? 0.0F
             : herd.adult_age_game_years_total / static_cast<float>(herd.adult_count);
}

std::uint16_t TakeOldestAdults(const LivestockDef& kind, HerdRow& herd, std::uint16_t wanted) {
  if (wanted == 0 || herd.adult_count == 0) {
    return 0;
  }
  const std::uint16_t before = herd.adult_count;
  const std::uint16_t gone = TakeHeads(herd.adult_count, wanted);
  if (gone == 0) {
    return 0;
  }
  // THE OLD ONES GO, not the average ones: off the top of the herd's own age
  // band (herd_age_band.h; 0.35.16). Until then off an ASSUMED spread around
  // the mean, which lay past the lifespan for a herd that did not.
  const float taken_age = CutOldestFromAdultAgeBand(herd, before, gone);
  herd.adult_age_game_years_total -= static_cast<float>(gone) * taken_age;
  const float adult_from_years = kind.adult_from_game_months / static_cast<float>(kMonthsPerYear);
  const float youngest_possible = static_cast<float>(herd.adult_count) * adult_from_years;
  herd.adult_age_game_years_total = herd.adult_age_game_years_total < youngest_possible
                                        ? youngest_possible
                                        : herd.adult_age_game_years_total;
  // The sires go in proportion with the rest, and A PROPORTION CAN ROUND THE
  // LAST ONE AWAY: three adults with one sire losing two keep one adult and
  // no sire. Whoever needs a sire KEPT must therefore ask MalesAfterLoss
  // BEFORE calling and refuse on its answer — which is what the district's
  // hand-over does (OrderRefusal::kLastSire). This comment used to say the
  // refusal was raised "by name rather than by arithmetic here", which read
  // as a guarantee this function gives and does not: the refusal it named was
  // testing how many heads were ASKED FOR, and the rounding happens on what
  // is LEFT.
  herd.adult_male_count = MalesAfterLoss(herd.adult_male_count, before, gone);
  return gone;
}

/// Age takes the herd from the top of its lifespan band: the hazard is zero
/// at the lower end and certain at the upper one, averaged over the ages the
/// herd holds (AverageAgeHazard). The draw is the world's sequential RNG, so
/// a replay lands on the same animals.
void RunAgeDeaths(const LivestockDef& kind, HerdRow& herd, HerdId herd_id, WorldState& world) {
  if (herd.adult_count == 0) {
    return;
  }
  const auto adults = static_cast<float>(herd.adult_count);
  herd.adult_age_game_years_total += adults / static_cast<float>(kDaysPerYear);
  AgeAdultAgeBand(herd, 1.0F / static_cast<float>(kDaysPerYear));
  if (!(kind.life_game_years_max > 0.0F)) {
    return;  // a kind whose lifespan the table does not name does not age out
  }
  // OVER THE AGES THE HERD HOLDS (herd_age_band.h; boss, core-boss-epoch1-6
  // [3] line 1; 0.35.16): its youngest to its oldest head. No head dies of
  // age before the oldest has reached the lifespan band — the mean and an
  // assumed ±3.3 years killed one or two of the start's one-to-four-year-old
  // horses in year 2 on five seeds of nine.
  const float low = herd.adult_age_min_game_years;
  const float high = herd.adult_age_max_game_years > low ? herd.adult_age_max_game_years : low;
  const float hazard_per_year = AverageAgeHazard(
      (low + high) * 0.5F, (high - low) * 0.5F, kind.life_game_years_min, kind.life_game_years_max);
  const float expected = adults * hazard_per_year / static_cast<float>(kDaysPerYear);
  auto dead = static_cast<std::uint16_t>(expected);
  if (NextRandomUnitFloat(world.rng) < expected - static_cast<float>(dead)) {
    dead = static_cast<std::uint16_t>(dead + 1);
  }
  // THE OLD ONES GO, the sires go in proportion, and the summed age follows
  // them — all three in TakeOldestAdults, which the district's hand-over
  // calls for the same reason. Old age is no respecter of sex, and this used
  // to take EVERY death out of the males and then throw the result away for
  // a re-derive.
  const std::uint16_t gone = TakeOldestAdults(kind, herd, dead);
  if (gone == 0) {
    return;
  }
  world.ledger.current.herd_deaths_age += gone;
  // Routine: animals age out, and a village that is told about it loudly
  // every day stops reading the journal. Hunger is the loud one below.
  SimEvent& event = EmitEvent(world, EventKind::kHerdDied);
  event.herd = herd_id;
  event.amount = static_cast<std::int64_t>(gone);
}

/// A herd left hungry long enough starts to lose heads. The ladder is the
/// design's: produce drops from the first hungry day, deaths only after the
/// threshold. The cold ladder of question 99 is NOT here — every phase-1
/// herd stands under a roof and unit temperatures do not exist.
void RunHungerDeaths(const ProductionConfig& config,
                     const LivestockDef& /*kind*/,
                     HerdRow& herd,
                     HerdId herd_id,
                     WorldState& world,
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
  const auto starved = static_cast<std::uint32_t>(owed_at_first - owed) + adults_gone;
  book.herd_deaths_hunger += starved;
  if (starved > 0) {
    // THE SEVERITY IS THE WHOLE DIFFERENCE between this and the line above:
    // the kind's contract says "severity says hunger from age"
    // (event_state.h), so the two paths share a kind and part on how loudly
    // they say it. An animal that starved is news the player has to act on.
    SimEvent& event = EmitEvent(world, EventKind::kHerdDied, EventSeverity::kInterrupting);
    event.herd = herd_id;
    event.amount = static_cast<std::int64_t>(starved);
  }
  if (adults_gone > 0 && herd.adult_count > 0) {
    const float mean =
        herd.adult_age_game_years_total / static_cast<float>(herd.adult_count + adults_gone);
    herd.adult_age_game_years_total -= static_cast<float>(adults_gone) * mean;
  } else if (adults_gone > 0) {
    herd.adult_age_game_years_total = 0.0F;
  }
  // Hunger takes the sires in proportion too, and THE REASON THIS LINE EXISTS
  // AT ALL is the one the re-derive here used to give: a herd starved down to
  // one or two adults and then rescued must not keep a male count from its
  // fat years, or "females" comes out non-positive and it gives no milk and
  // bears nothing, for ever. Proportion answers that as well as the re-derive
  // did — and unlike the re-derive it does not also erase what the chairman
  // bought.
  herd.adult_male_count = MalesAfterLoss(herd.adult_male_count,
                                         static_cast<std::uint16_t>(herd.adult_count + adults_gone),
                                         adults_gone);
}

/// The autumn pig slaughter (livestock design §6): everything but the sows
/// and the sire goes to meat. How many sows the herd keeps is the one number
/// the design does not name, so it is a table knob — ASSUMPTION, and the
/// balance run is what will move it.
/// The sows the autumn keeps: the share of the adults, and never fewer than
/// sow_keep_min while the herd has that many females (boss, boss-core-epoch1-5
/// seq 45, option б; 0.35.5). ONE HOME for the slaughter and for the meat it
/// is sized by (AutumnSlaughterMeatShort), which used to compute it twice.
std::uint16_t SowsKept(const ProductionConfig& config, const HerdRow& herd) {
  const std::uint16_t by_share =
      AsHeads(static_cast<float>(herd.adult_count) * config.farming.sow_keep_share);
  const std::uint16_t females =
      herd.adult_count > herd.adult_male_count ? herd.adult_count - herd.adult_male_count : 0;
  const std::uint16_t floor = AsHeads(config.farming.sow_keep_min);
  const std::uint16_t least = females < floor ? females : floor;
  return by_share > least ? by_share : least;
}

std::uint16_t AutumnSlaughterHeads(const ProductionConfig& config,
                                   const LivestockDef& kind,
                                   const HerdRow& herd) {
  const std::uint16_t sows = SowsKept(config, herd);
  const std::uint16_t keep = static_cast<std::uint16_t>(sows + TargetMales(kind, herd.adult_count));
  const std::uint16_t adults = herd.adult_count > keep ? herd.adult_count - keep : 0;
  return static_cast<std::uint16_t>(herd.juvenile_count + adults);
}

Grams AutumnSlaughterMeatShort(const ProductionConfig& config,
                               const WorldState& world,
                               const HerdRow& herd) {
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  if (herd.kind.value != config.pig_kind.value || herd.household_owned != 0 ||
      herd.autumn_slaughter_done != 0 || month != config.farming.pig_slaughter_month ||
      herd.kind.value >= config.livestock.size()) {
    return 0;
  }
  const LivestockDef& kind = config.livestock[herd.kind.value];
  const Grams meat = KilogramsToGrams(kind.meat_kg_per_head *
                                      static_cast<float>(AutumnSlaughterHeads(config, kind, herd)));
  const Grams room = ReceivableRoom(config, world, config.meat_resource);
  return meat > room ? meat - room : 0;
}

void RunAutumnSlaughter(const ProductionConfig& config,
                        const LivestockDef& kind,
                        LivestockKindId kind_id,
                        const HerdPlace& place,
                        HerdRow& herd,
                        HerdId herd_id,
                        WorldState& world,
                        const CalendarState& calendar) {
  const auto month = static_cast<std::uint8_t>(calendar.date.month);
  if (kind_id.value != config.pig_kind.value) {
    return;
  }
  if (month != config.farming.pig_slaughter_month) {
    herd.autumn_slaughter_done = 0;  // a new season's slaughter will be due
    return;
  }
  if (herd.autumn_slaughter_done != 0) {
    return;
  }
  // THE SLAUGHTER WAITS FOR ROOM (boss, host-econ-shops seq 26): until
  // 2026-09-19 it fell on the month's first day whatever the stores held, and
  // host's seed 5 lost 420 kg of 420 that day. A kolkhoz herd's meat with no
  // room waits — kSlaughterWaitsForRoom says so — and the month's last day
  // takes it whatever the room, as it always did. A larder takes all.
  const bool last_day = calendar.date.day_in_month + 1U >= kDaysPerMonth;
  if (!last_day && place.pantry == nullptr && AutumnSlaughterMeatShort(config, world, herd) > 0) {
    return;
  }
  herd.autumn_slaughter_done = 1;
  const std::uint16_t sows = SowsKept(config, herd);
  const std::uint16_t males = TargetMales(kind, herd.adult_count);
  const auto keep = static_cast<std::uint16_t>(sows + males);
  std::uint16_t gone = TakeHeads(herd.juvenile_count, herd.juvenile_count);
  if (herd.adult_count > keep) {
    const auto surplus = static_cast<std::uint16_t>(herd.adult_count - keep);
    const float mean = herd.adult_age_game_years_total / static_cast<float>(herd.adult_count);
    gone = static_cast<std::uint16_t>(gone + TakeHeads(herd.adult_count, surplus));
    herd.adult_age_game_years_total -= static_cast<float>(surplus) * mean;
    // The autumn slaughter keeps `males` sires BY NAME — the design says
    // everything but the sows and the sire goes to meat — so the survivors
    // are that number, not a proportion of what stood before.
    herd.adult_male_count = std::min(males, herd.adult_count);
  }
  world.ledger.current.herd_culled += gone;
  if (gone > 0) {
    AddLedgerHeads(world.ledger.current.herd_autumn_slaughtered, herd.kind, gone);
    SimEvent& event = EmitEvent(world, EventKind::kHerdAutumnSlaughter, EventSeverity::kNotable);
    event.herd = herd_id;
    event.amount = static_cast<std::int64_t>(gone);
  }
  Slaughter(config, kind, place, gone, world);
}
}  // namespace core
