#include "herd_forecast.h"

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

/// One kolkhoz herd as the forecast carries it: fractional heads, grown and
/// born by the herd day's own numbers.
struct ProjectedHerd {
  HerdId id;
  LivestockKindId kind;
  float adults = 0.0F;
  float juveniles = 0.0F;
  float newborns = 0.0F;
  /// Heads a day A FEMALE brings while the calving band stands and the gates
  /// hold; 0 when any gate is shut (herd_life.cpp, RunBirths). The gates are
  /// today's: a herd billeted or hungry today calves in no forecast day.
  float births_per_female_band_day = 0.0F;
  /// The adult males (a sexed kind): the females are the rest, and the young
  /// grown up in the forecast calve with them (static review of 0.37.57 — a
  /// rate frozen at today's females under-counted next year's band, the
  /// unsafe side). 0 for an unsexed kind, whose adults all breed.
  float males = 0.0F;
  /// The day, as an offset from today, the herd is in the village from: 0
  /// for a herd that stands, the arrival for a lot of stock on its way.
  std::uint32_t from_day = 0;
  /// THE HERD PLAN (#1289; HerdRow::plan_heads): no birth on a forecast day
  /// the projected heads stand at or over it, as RunBirths gates the real
  /// one - else the lamp and the window would count calves the plan stops.
  /// Negative: no plan.
  float plan_heads = -1.0F;
  /// THE OLD ONES GO IN THE FORECAST TOO (#1292; econ's rule, the thread
  /// core-boss-1289-herd-plan [36]): the share of the adults old age takes a
  /// day, by the herd day's own expectation (ExpectedAgeDeathsPerDay) on the
  /// herd as it stands today, held over the horizon. Without it a herd of 42
  /// cows was fed 42 to the next cut while age took 11, and the lamp read
  /// next spring 13-34 % short where the herds were fed.
  float age_deaths_per_adult_day = 0.0F;
};

std::uint16_t RoundHeads(float heads) {
  const float rounded = std::floor(heads + 0.5F);
  return rounded <= 0.0F ? 0U : static_cast<std::uint16_t>(rounded > 65535.0F ? 65535.0F : rounded);
}

/// THE BIRTHS' GATES, as RunBirths reads them (herd_life.cpp): a hungry
/// herd, a kind that keeps sires with none, a horse with no stable — any
/// shut gate, no offspring; and the billet as a SHARE, CalvingRoofShare, the
/// same home RunBirths reads (0.37.62). The share is today's, held over the
/// whole horizon: the forecast does not guess when the chairman will raise
/// the barn. The daily rate a female inside the band as RunBirths draws it
/// (its rate is females × this).
float BirthsPerFemaleBandDay(const ProductionConfig& config,
                             const LivestockDef& kind,
                             LivestockKindId kind_id,
                             const HerdRow& herd,
                             bool stable_built) {
  if (herd.fed_share < config.farming.calving_fed_share_floor) {
    return 0.0F;
  }
  if (kind.sexed != 0 && kind.males_share > 0.0F && herd.adult_male_count == 0) {
    return 0.0F;
  }
  if (kind_id.value == config.horse_kind.value && !stable_built) {
    return 0.0F;
  }
  if (!(kind.births_per_game_year > 0.0F)) {
    return 0.0F;
  }
  const auto band_days = static_cast<float>(
      (config.farming.birth_to_month - config.farming.birth_from_month + 1U) * kDaysPerMonth);
  return band_days > 0.0F
             ? CalvingRoofShare(herd) * kind.births_per_game_year * kind.litter_heads / band_days
             : 0.0F;
}

/// The heads' day of growing up, as RunMaturation flows them: newborns into
/// juveniles over the newborn months, juveniles into adults over the rest.
void MatureOneDay(const LivestockDef& kind, ProjectedHerd& herd) {
  const auto month_days = static_cast<float>(kDaysPerMonth);
  const float newborn_days = kind.newborn_game_months * month_days;
  const float juvenile_days = (kind.adult_from_game_months - kind.newborn_game_months) * month_days;
  // RunMaturation's order: the newborns join the juveniles first, and the
  // juveniles' flow is drawn off the count WITH them (static review of 0.37.57).
  if (newborn_days > 0.0F) {
    const float grown_newborns = herd.newborns / newborn_days;
    herd.newborns -= grown_newborns;
    herd.juveniles += grown_newborns;
  }
  if (juvenile_days > 0.0F) {
    const float grown_juveniles = herd.juveniles / juvenile_days;
    herd.juveniles -= grown_juveniles;
    herd.adults += grown_juveniles;
    // AND THE YOUNG MALES THE HERD HAS NO ROOM FOR GO TO MEAT AS THEY GROW
    // (RunMaturation): half of what grows is male, the herd keeps its share
    // of sires, the rest is culled. Kept out of the forecast at first, it
    // carried a kolkhoz herd's bull calves for a year (0.37.57's pair).
    if (kind.sexed != 0 && kind.males_work != 0) {
      // A kind whose males are work stock keeps them all (RunMaturation;
      // 0.37.209): half of what grows is male, none is culled.
      herd.males += grown_juveniles * 0.5F;
    } else if (kind.sexed != 0) {
      const auto target = static_cast<float>(TargetMales(kind, RoundHeads(herd.adults)));
      const float room = target > herd.males ? target - herd.males : 0.0F;
      const float culled = std::max(0.0F, grown_juveniles * 0.5F - room);
      herd.males += std::min(grown_juveniles, room);
      herd.adults -= culled;
    }
  }
}

/// The hay the forecast expects cut this year, grams, still to come: last
/// year's (the closed book) less what this year has laid already. Asked from
/// year 2 only: year 1 has no book, and its forecast stops at the first cut
/// (ForecastHerdFeed; boss [88], option (б)).
Grams ExpectedCutStillToCome(const ProductionConfig& config, const WorldState& world) {
  const ResourceId hay = config.hay_resource;
  if (hay.value == kInvalidDefIdValue) {
    return 0;
  }
  const Grams expected = AmountOf(world.ledger.closed.harvest, hay);
  const Grams laid = AmountOf(world.ledger.current.harvest, hay);
  return expected > laid ? expected - laid : 0;
}

/// THE HARVEST'S FEED STILL TO COME (#1292; econ, core-boss-1289-herd-plan
/// [40]), kilograms by resource and the day it lands, as offsets from today
/// up to `horizon_days`: for each feed but hay, the book's harvest - the
/// LOWER of last year's and the mean of up to three closed years (the
/// chronicle's rows that carry a harvest, save 192) - less what this year has
/// harvested already, of which the herds may eat what the holds ladder leaves
/// (HerdFeedFromIncome). It lands on the last day of its crop's harvest
/// window this year; straw on the latest window of the crops that leave it.
/// No book - year 1, or a chronicle whose rows carry no harvest - no income.
/// Not counted: lots not yet ordered, any purchase still to be decided.
std::vector<std::pair<std::uint32_t, std::vector<float>>> ScheduledHarvestFeed(
    const ProductionConfig& config, const WorldState& world, std::uint32_t horizon_days) {
  std::vector<std::pair<std::uint32_t, std::vector<float>>> landings;
  /// The book's years: econ's «the three-year mean».
  constexpr std::size_t kBookYears = 3;
  std::vector<const ResourceAmounts*> book;
  for (auto year = world.ledger.chronicle.rbegin();
       year != world.ledger.chronicle.rend() && book.size() < kBookYears;
       ++year) {
    if (!year->harvest.empty()) {
      book.push_back(&year->harvest);
    }
  }
  if (book.empty()) {
    return landings;
  }
  const SimDay today = world.calendar.day;
  const SimDay year_start = today - (today % kDaysPerYear);
  // The last day of each resource's harvest window this year: its crop's for
  // a grain, the latest straw-leaving crop's for the straw.
  std::vector<SimDay> lands_on(config.feed_values.size(), 0);
  for (const CropDef& crop : config.crops) {
    const SimDay window_end =
        year_start + ((static_cast<SimDay>(crop.harvest_to_month) + 1U) * kDaysPerMonth) - 1U;
    if (crop.resource.value < lands_on.size()) {
      lands_on[crop.resource.value] = std::max(lands_on[crop.resource.value], window_end);
    }
    if (crop.straw_ratio > 0.0F && config.straw_resource.value < lands_on.size()) {
      lands_on[config.straw_resource.value] =
          std::max(lands_on[config.straw_resource.value], window_end);
    }
  }
  for (std::size_t resource = 0; resource < config.feed_values.size(); ++resource) {
    if (resource == config.hay_resource.value || !(config.feed_values[resource] > 0.0F) ||
        lands_on[resource] < today || lands_on[resource] - today >= horizon_days) {
      continue;
    }
    const auto of = [resource](const ResourceAmounts& amounts) {
      return resource < amounts.size() ? amounts[resource] : Grams{0};
    };
    Grams sum = 0;
    for (const ResourceAmounts* year : book) {
      sum += of(*year);
    }
    const Grams mean = sum / static_cast<Grams>(book.size());
    const Grams expected = std::min(of(*book.front()), mean);
    const Grams harvested = of(world.ledger.current.harvest);
    const Grams to_come = expected > harvested ? expected - harvested : 0;
    const Grams eaten =
        HerdFeedFromIncome(config, world, DefIdFromIndex<ResourceIdTag>(resource), to_come);
    if (eaten <= 0) {
      continue;
    }
    std::vector<float> goods(config.feed_values.size(), 0.0F);
    goods[resource] = static_cast<float>(eaten) / static_cast<float>(kGramsPerKilogram);
    landings.emplace_back(lands_on[resource] - today, std::move(goods));
  }
  return landings;
}

/// The hay still standing as grass on the meadows, grams: the uncut share of
/// every meadow in its cut, and a meadow the cut's months will still open
/// this year, whole.
Grams HayStandingOnMeadows(const ProductionConfig& config, const WorldState& world) {
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  Grams standing = 0;
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind == LandKind::kArable) {
      continue;
    }
    const float rate = field.kind == LandKind::kFloodplainMeadow
                           ? config.farming.meadow_floodplain_yield_kg_per_ha
                           : config.farming.meadow_yield_kg_per_ha;
    const auto whole = static_cast<float>(KilogramsToGrams(rate * field.area_ga));
    if (field.phase == FieldPhase::kHarvest) {
      const float left = field.harvest_work_days > 0.0F
                             ? field.work_days_remaining / field.harvest_work_days
                             : 0.0F;
      standing += GramsFromFloat(whole * (left < 0.0F ? 0.0F : (left > 1.0F ? 1.0F : left)));
    } else if (field.phase == FieldPhase::kGrowing &&
               month <= config.farming.meadow_cut_month_end &&
               (field.last_mown_day == kNeverMownDay ||
                field.last_mown_day / kDaysPerYear != world.calendar.day / kDaysPerYear)) {
      standing += GramsFromFloat(whole);
    }
  }
  return standing;
}

/// The cut's pace and what it promises (lever 1).
struct CutPace {
  bool known = false;        ///< Inside the cut with a meadow open: measured, or the prior.
  bool prior = false;        ///< Before the second dry working day: the norm's, not measured.
  bool grass_bound = false;  ///< The grass standing is the smaller term: the cut ends mown through.
  Grams hay_to_come = 0;     ///< min(grass standing, pace x dry working days left).
  float kg_a_day = 0.0F;     ///< The pace: the year's hay laid over the days counted.
  float dry_days_left = 0;   ///< The dry working days left, by the calendar and the climate.
};

/// @brief THE CUT'S PACE (econ's four-levers §2a, as boss ruled it on 13
///        October 2026): the hay this year's cut has laid over the dry
///        working days it has been open (ChairmanState::hay_cut_dry_work_days),
///        and the dry working days left - the days left to the cut's end
///        LESS THE DAYS OFF AMONG THEM, counted from the calendar (a quarter
///        of the cut's days are days off, and unlike rain they are known in
///        advance - a harvest worked without days off falls out of the same
///        count), each by the climate's dry share of its day
///        (ProductionConfig::rain_day_shares): the table's weather, never the
///        run's coming one.
CutPace PaceOfTheCut(const ProductionConfig& config,
                     const WorldState& world,
                     SimDay cut_from,
                     SimDay this_cut_end) {
  /// The pace is read from the cut's second dry working day.
  constexpr std::uint16_t kFirstPaceDay = 2;
  CutPace pace;
  const SimDay today = world.calendar.day;
  // ON THE CUT'S FIRST DAY THE COUNT IS NOUGHT (#1292): SettleHayCutDay
  // starts it anew only that evening, and the lamp's noon read took last
  // cut's days as this one's - 0 kg over four days, a pace of nought on a
  // full meadow, the lamp lit on 62 of 63 first cut days (core's print).
  const std::uint16_t worked = today == cut_from ? 0 : world.chairman.hay_cut_dry_work_days;
  if (today < cut_from || today > this_cut_end || config.hay_resource.value == kInvalidDefIdValue) {
    return pace;
  }
  const Grams standing = HayStandingOnMeadows(config, world);
  if (worked < kFirstPaceDay) {
    // THE PRIOR, BEFORE THERE IS A PACE TO READ (econ's four-levers §2c, arm
    // 4; boss, 13 October 2026): the norm's hay a mower-day - the grass
    // standing on the meadows in their cut over the norm-days left on them -
    // times the mowers on the cut this morning; before any is placed,
    // FarmingConfig::hay_prior_mower_share of the grown residents. Without it
    // the lamp was dark on the cut's opening days, and June's day off - which
    // falls on them - could not be advised. Only with a meadow open: before
    // the cut's first field opens there is no norm to read, and no prior.
    float norm_days_left = 0.0F;
    for (const FieldRow& field : world.fields.rows) {
      if (field.kind != LandKind::kArable && field.phase == FieldPhase::kHarvest &&
          field.work_days_remaining > 0.0F) {
        norm_days_left += field.work_days_remaining;
      }
    }
    if (!(norm_days_left > 0.0F)) {
      return pace;
    }
    float mowers = 0.0F;
    float grown = 0.0F;
    for (const ResidentRow& person : world.residents.rows) {
      grown += BiologicalAgeYears(config.farming.life_speedup, person.birth_day, today) >=
                       config.farming.adult_age_years
                   ? 1.0F
                   : 0.0F;
      if (person.work.kind != WorkKind::kHarvest) {
        continue;
      }
      const std::uint32_t row = FindRow(world.fields, person.work.field);
      mowers += row != kNoRow && world.fields.rows[row].kind != LandKind::kArable ? 1.0F : 0.0F;
    }
    if (!(mowers > 0.0F)) {
      mowers = config.farming.hay_prior_mower_share * grown;
    }
    pace.known = true;
    pace.prior = true;
    pace.kg_a_day = static_cast<float>(standing) / static_cast<float>(kGramsPerKilogram) /
                    norm_days_left * mowers;
  } else {
    pace.known = true;
    const Grams laid = AmountOf(world.ledger.current.harvest, config.hay_resource);
    pace.kg_a_day = static_cast<float>(laid) / static_cast<float>(kGramsPerKilogram) /
                    static_cast<float>(worked);
  }
  // The dry working days left are a DRAW, not a mean: beside their expected
  // count, the variance of the draw - each day dry with its own share.
  float dry_variance = 0.0F;
  for (SimDay day = today + 1U; day <= this_cut_end; ++day) {
    if (!IsDayOffIn(world, day)) {
      const float share = config.rain_day_shares[day % kDaysPerYear];
      const float wet = share < 0.0F ? 0.0F : (share > 1.0F ? 1.0F : share);
      pace.dry_days_left += 1.0F - wet;
      dry_variance += wet * (1.0F - wet);
    }
  }
  const Grams by_pace = GramsFromKilograms(pace.kg_a_day * pace.dry_days_left);
  pace.hay_to_come = by_pace < standing ? by_pace : standing;
  // THE GRASS IS THE BOUND ONLY IF A POOR DRAW OF THE WEATHER STILL MOWS IT
  // THROUGH (econ's four-levers §2h-2i; boss, 13 October 2026): the pace over
  // the expected dry working days left LESS ONE STANDARD DEVIATION of their
  // draw reaches the grass. By the mean alone a village lost 20 t to rain on
  // three of the cut's last four days with every move withdrawn; a flat
  // margin large enough for the last week (3.0) named the moves all summer,
  // one small enough for June (1.5) did not save that village. (With no
  // grass counted at all there is no bound to speak of.)
  const float poor_draw_days = pace.dry_days_left - std::sqrt(dry_variance);
  pace.grass_bound = standing > 0 && poor_draw_days > 0.0F &&
                     GramsFromKilograms(pace.kg_a_day * poor_draw_days) >= standing;
  return pace;
}

/// THE YEAR-1 CUT BY THE NORM, for the herd window before the first cut
/// (#1289; FeedHorizon::kWindowByNorm; econ, core-boss-1289 [7]: «the norm's
/// t a mower-day × the mowers the village can place × the window's dry
/// working days»): the pace's own PRIOR (PaceOfTheCut, before its second dry
/// working day) read before any meadow is open - the norm's hay a mower-day
/// is the grass standing over the meadows' norm-days (area × meadow_mow_days_
/// per_ha), the mowers FarmingConfig::hay_prior_mower_share of the grown,
/// the dry working days the window's expected count - and never more than
/// the grass standing.
Grams NormCutYearOne(const ProductionConfig& config,
                     const WorldState& world,
                     SimDay cut_from,
                     SimDay cut_end) {
  const Grams standing = HayStandingOnMeadows(config, world);
  float norm_days = 0.0F;
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind != LandKind::kArable) {
      norm_days += config.farming.meadow_mow_days_per_ha * field.area_ga;
    }
  }
  if (standing <= 0 || !(norm_days > 0.0F)) {
    return 0;
  }
  const SimDay today = world.calendar.day;
  float grown = 0.0F;
  for (const ResidentRow& person : world.residents.rows) {
    grown += BiologicalAgeYears(config.farming.life_speedup, person.birth_day, today) >=
                     config.farming.adult_age_years
                 ? 1.0F
                 : 0.0F;
  }
  const float mowers = config.farming.hay_prior_mower_share * grown;
  float dry_days = 0.0F;
  for (SimDay day = cut_from; day <= cut_end; ++day) {
    if (!IsDayOffIn(world, day)) {
      const float share = config.rain_day_shares[day % kDaysPerYear];
      dry_days += 1.0F - std::clamp(share, 0.0F, 1.0F);
    }
  }
  const float kg_a_mower_day =
      static_cast<float>(standing) / static_cast<float>(kGramsPerKilogram) / norm_days;
  return std::min(standing, GramsFromKilograms(kg_a_mower_day * mowers * dry_days));
}

/// THE MONTH THE CUT ENDS (static review of 0.37.57): the later of the
/// meadows' cut month and the last harvest month of a crop that gives hay
/// (clover and timothy, June-August) — last year's hay in the book is both,
/// and landing all of it at the meadows' month over-promised, then read 0 in
/// July with the mowing still under way.
std::uint8_t HayCutEndMonth(const ProductionConfig& config) {
  std::uint8_t month = config.farming.meadow_cut_month;
  for (const CropDef& crop : config.crops) {
    if (crop.resource.value == config.hay_resource.value && crop.harvest_to_month > month) {
      month = crop.harvest_to_month;
    }
  }
  return month;
}

/// Whether a delivery of `resource` would find room through the door today:
/// a numbered store that takes it with room left, or an outline that does.
bool StoreHasRoomFor(const ProductionConfig& config, const WorldState& world, ResourceId resource) {
  return std::ranges::any_of(world.units.rows, [&world, &config, resource](const UnitRow& unit) {
    return (NumberedStoreAccepts(world, unit, config, resource) &&
            FreeRoomGrams(unit, config) > 0) ||
           HomeOutlineAccepts(world, unit, config, resource);
  });
}

/// Heads a projected herd has for the drain's arithmetic.
float HeadsOf(const ProjectedHerd& herd) {
  return herd.adults + herd.juveniles;
}

/// A standing kolkhoz herd as the forecast starts it.
ProjectedHerd ProjectHerd(const ProductionConfig& config,
                          const WorldState& world,
                          std::uint32_t row,
                          bool stable_built) {
  const HerdRow& herd = world.herds.rows[row];
  ProjectedHerd projected;
  projected.id = world.herds.row_ids[row];
  projected.kind = herd.kind;
  projected.adults = static_cast<float>(herd.adult_count);
  projected.juveniles = static_cast<float>(herd.juvenile_count);
  projected.newborns = static_cast<float>(herd.newborn_count);
  const LivestockDef& kind = config.livestock[herd.kind.value];
  projected.births_per_female_band_day =
      BirthsPerFemaleBandDay(config, kind, herd.kind, herd, stable_built);
  projected.males = kind.sexed != 0 ? static_cast<float>(herd.adult_male_count) : 0.0F;
  projected.plan_heads =
      herd.plan_heads != kNoHerdPlan ? static_cast<float>(herd.plan_heads) : -1.0F;
  projected.age_deaths_per_adult_day =
      herd.adult_count > 0
          ? ExpectedAgeDeathsPerDay(kind, herd) / static_cast<float>(herd.adult_count)
          : 0.0F;
  return projected;
}

/// One forecast day of a herd, in the herd day's order (RunHerdDay): the
/// maturing, the old ones going, then the day's births inside the band.
void ProjectHerdDay(const ProductionConfig& config, std::uint8_t month, ProjectedHerd& herd) {
  const LivestockDef& kind = config.livestock[herd.kind.value];
  MatureOneDay(kind, herd);
  // Age takes from the adults, the sires in proportion (TakeOldestAdults).
  const float aged = herd.adults * herd.age_deaths_per_adult_day;
  herd.males -= herd.adults > 0.0F ? herd.males * (aged / herd.adults) : 0.0F;
  herd.adults -= aged;
  const bool at_plan =
      herd.plan_heads >= 0.0F && herd.adults + herd.juveniles + herd.newborns >= herd.plan_heads;
  if (herd.births_per_female_band_day > 0.0F && !at_plan &&
      MonthInRange(month, config.farming.birth_from_month, config.farming.birth_to_month)) {
    (kind.newborn_game_months > 0.0F ? herd.newborns : herd.juveniles) +=
        std::max(0.0F, herd.adults - herd.males) * herd.births_per_female_band_day;
  }
}

}  // namespace

float ForecastHerdHeads(const ProductionConfig& config,
                        const WorldState& world,
                        HerdId herd,
                        std::uint32_t days) {
  const std::uint32_t row = FindRow(world.herds, herd);
  if (row == kNoRow || world.herds.rows[row].kind.value >= config.livestock.size()) {
    return 0.0F;
  }
  ProjectedHerd projected = ProjectHerd(config, world, row, StableBuilt(world, config));
  for (std::uint32_t day = 1; day <= days; ++day) {
    ProjectHerdDay(
        config, static_cast<std::uint8_t>(DateFromDay(world.calendar.day + day).month), projected);
  }
  return projected.adults + projected.juveniles + projected.newborns;
}

std::vector<float> HerdFeedHeldKg(const ProductionConfig& config, const WorldState& world) {
  const ResourceAmounts allowance = HerdFeedAllowance(config, world);
  std::vector<float> held_kg(config.feed_values.size(), 0.0F);
  for (std::uint32_t resource = 0; resource < held_kg.size(); ++resource) {
    if (!(config.feed_values[resource] > 0.0F) || resource >= allowance.size()) {
      continue;
    }
    held_kg[resource] =
        static_cast<float>(allowance[resource]) / static_cast<float>(kGramsPerKilogram);
  }
  if (config.hay_resource.value < held_kg.size()) {
    held_kg[config.hay_resource.value] +=
        static_cast<float>(HeapAbovePlanDebt(world, config.hay_resource)) /
        static_cast<float>(kGramsPerKilogram);
  }
  return held_kg;
}

bool StoredHayShortWithin(const ProductionConfig& config,
                          const WorldState& world,
                          std::uint32_t days) {
  const std::size_t hay = config.hay_resource.value;
  if (days == 0 || config.livestock.empty() || hay >= config.feed_values.size()) {
    return false;
  }
  // The kinds that eat hay at all: a herd of any other kind wanting feed
  // says nothing of the hay.
  std::vector<std::uint8_t> eats_hay(config.livestock.size(), 0);
  for (const FeedLinkDef& link : config.feed_links) {
    if (link.resource.value == hay && link.work_only == 0 && link.kind.value < eats_hay.size()) {
      eats_hay[link.kind.value] = 1;
    }
  }
  // THE STORES ALONE: the allowance the herd feeding draws on, without the
  // heaps HerdFeedHeldKg adds — the question is whether they must come in.
  const ResourceAmounts allowance = HerdFeedAllowance(config, world);
  std::vector<float> held_kg(config.feed_values.size(), 0.0F);
  for (std::size_t resource = 0; resource < held_kg.size() && resource < allowance.size();
       ++resource) {
    if (config.feed_values[resource] > 0.0F) {
      held_kg[resource] =
          static_cast<float>(allowance[resource]) / static_cast<float>(kGramsPerKilogram);
    }
  }
  const std::vector<std::uint8_t> peoples_foods = PeoplesFoods(config, world);
  std::vector<FeedDayNeed> needs;
  std::vector<float> covered;
  for (std::uint32_t day = 0; day < days; ++day) {
    const auto month = static_cast<std::uint8_t>(DateFromDay(world.calendar.day + day).month);
    const bool team_out = TeamOutInMonth(config, world, month);
    needs.clear();
    for (const HerdRow& herd : world.herds.rows) {
      if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
        continue;  // a family herd eats from its family's pantry, as in the forecast
      }
      const bool grazing = herd.kind.value != config.horse_kind.value || team_out;
      const float units =
          FeedNeedUnits(config, config.livestock[herd.kind.value], herd, month, grazing);
      if (units > 0.0F) {
        needs.push_back(FeedDayNeed{.kind = herd.kind, .units = units});
      }
    }
    DrainFeedDay(config, peoples_foods, needs, held_kg, covered);
    // SHORT: the hay is out AND a herd that eats it is not fed in full. The
    // hay out alone is not it — the stores holding exactly the days asked
    // feed the last day whole — and a herd underfed with hay still in store
    // waits on another feed or on a link's ceiling, which no cart of hay
    // mends.
    if (held_kg[hay] >= kOutKg) {
      continue;
    }
    for (std::size_t index = 0; index < needs.size(); ++index) {
      if (eats_hay[needs[index].kind.value] != 0 &&
          covered[index] + kFedSlackUnits < needs[index].units) {
        return true;
      }
    }
  }
  return false;
}

void DrainFeedDay(const ProductionConfig& config,
                  const std::vector<std::uint8_t>& peoples_foods,
                  std::span<const FeedDayNeed> needs,
                  std::vector<float>& held_kg,
                  std::vector<float>& covered) {
  covered.assign(needs.size(), 0.0F);
  for (std::size_t index = 0; index < needs.size(); ++index) {
    const FeedDayNeed& herd = needs[index];
    float got = 0.0F;
    for (const FeedLinkDef& link : config.feed_links) {
      if (got >= herd.units) {
        break;
      }
      // The kind filter is the whole point: a feeding order belongs to the
      // kind it names, and a cow is never offered the pigs' barley.
      if (link.kind.value != herd.kind.value || link.resource.value >= held_kg.size()) {
        continue;
      }
      // Work-only feeds are the horse's WAGE, not its keep (boss, Q2): in a
      // forecast nobody is in the traces, so they carry nothing.
      if (link.work_only != 0) {
        continue;
      }
      if (link.reserve != 0 && link.resource.value < peoples_foods.size() &&
          peoples_foods[link.resource.value] != 0) {
        continue;  // the people's food: the feeding refuses it to a reserve link
      }
      const float value = config.feed_values[link.resource.value] *
                          (link.reserve != 0 ? config.farming.reserve_feed_factor : 1.0F);
      if (!(value > 0.0F)) {
        continue;
      }
      const float ceiling = herd.units * link.max_share;
      float take_units = herd.units - got;
      take_units = take_units < ceiling ? take_units : ceiling;
      const float available_units = held_kg[link.resource.value] * value;
      take_units = take_units < available_units ? take_units : available_units;
      if (!(take_units > 0.0F)) {
        continue;
      }
      held_kg[link.resource.value] -= take_units / value;
      got += take_units;
    }
    covered[index] = got;
  }
}

FeedPurchase MaximalFeedPurchase(const ProductionConfig& config,
                                 const WorldState& world,
                                 bool* blocked_by_store,
                                 std::uint32_t lots_cap) {
  FeedPurchase purchase;
  purchase.goods_kg.assign(config.feed_values.size(), 0.0F);
  if (blocked_by_store != nullptr) {
    *blocked_by_store = false;
  }

  // Every goods lot that carries a feed the herds eat, with its feed units a
  // point — the order the points are spent in.
  struct FeedLot {
    LimitLotId lot;
    float units_a_point = 0.0F;
  };

  std::vector<FeedLot> lots;
  for (std::uint32_t index = 0; index < config.limit.lots.size(); ++index) {
    const LimitLotDef& def = config.limit.lots[index];
    if (def.kind != LimitLotKind::kGoods) {
      continue;
    }
    float units = 0.0F;
    for (std::size_t resource = 0;
         resource < def.goods.size() && resource < config.feed_values.size();
         ++resource) {
      if (def.goods[resource] > 0 && config.feed_values[resource] > 0.0F) {
        units += static_cast<float>(def.goods[resource]) / static_cast<float>(kGramsPerKilogram) *
                 config.feed_values[resource];
      }
    }
    if (!(units > 0.0F)) {
      continue;
    }
    const LimitLotId lot{static_cast<std::uint16_t>(index)};
    // The door's own answer with every point of the year in hand: a lot
    // refused for the store alone is the granary's day, not the lot's.
    const OrderRefusal door =
        LimitLotRefusalToday(config, world, lot, std::numeric_limits<std::int32_t>::max());
    if (door == OrderRefusal::kNowhereToStore && blocked_by_store != nullptr) {
      *blocked_by_store = true;
    }
    if (door != OrderRefusal::kNone) {
      continue;
    }
    // A free lot (0 points) is priced as one point: it sorts first and is
    // taken once.
    const float points = def.points > 0 ? static_cast<float>(def.points) : 1.0F;
    lots.push_back(FeedLot{.lot = lot, .units_a_point = units / points});
  }
  // The most feed units a point first; equal lots in the catalogue's order.
  std::ranges::stable_sort(lots, [](const FeedLot& left, const FeedLot& right) {
    return left.units_a_point > right.units_a_point;
  });
  std::int32_t points_left = world.limit.points;
  for (const FeedLot& feed : lots) {
    const LimitLotDef& def = config.limit.lots[feed.lot.value];
    // As many as the points cover — and no more lots in all than `lots_cap`
    // (the least purchase is found by asking for fewer, AdviseOnShortFodder).
    const std::uint32_t cap_left = lots_cap - purchase.lots;
    const std::int32_t by_points = def.points > 0 ? points_left / def.points : 1;
    const std::int32_t copies = by_points > 0 && static_cast<std::uint32_t>(by_points) > cap_left
                                    ? static_cast<std::int32_t>(cap_left)
                                    : by_points;
    if (copies <= 0) {
      continue;
    }
    points_left -= copies * def.points;
    purchase.points += copies * def.points;
    purchase.lots += static_cast<std::uint32_t>(copies);
    for (std::size_t resource = 0;
         resource < def.goods.size() && resource < purchase.goods_kg.size();
         ++resource) {
      if (def.goods[resource] > 0 && config.feed_values[resource] > 0.0F) {
        purchase.goods_kg[resource] += static_cast<float>(copies) *
                                       static_cast<float>(def.goods[resource]) /
                                       static_cast<float>(kGramsPerKilogram);
        purchase.any = true;
      }
    }
  }
  if (purchase.any && blocked_by_store != nullptr) {
    *blocked_by_store = false;  // a lot IS buyable: the day's move is the lot
  }
  // THE CART'S LATEST DAY: counted as landing early, the purchase would name
  // fewer heads than the fodder is short of.
  purchase.land_day = LimitBaseDeliveryDays(config, world) + config.limit.delivery_delay_days_max;
  return purchase;
}

namespace {

/// The classes of HeadsHandedOver.
enum class HeadClass : std::uint8_t { kHorses, kCows, kOther };

HeadClass ClassOf(const ProductionConfig& config, LivestockKindId cow, LivestockKindId kind) {
  if (kind.value == config.horse_kind.value) {
    return HeadClass::kHorses;
  }
  return kind.value == cow.value ? HeadClass::kCows : HeadClass::kOther;
}

/// Takes `wanted` heads off one cohort of the herds of a class, herd by herd
/// in row order; the sires of a sexed kind go in proportion to its adults.
/// Returns the heads still wanted.
float TakeCohort(const ProductionConfig& config,
                 LivestockKindId cow,
                 HeadClass of_class,
                 bool adults,
                 float wanted,
                 std::vector<ProjectedHerd>& herds) {
  for (ProjectedHerd& herd : herds) {
    if (!(wanted > 0.0F)) {
      break;
    }
    if (ClassOf(config, cow, herd.kind) != of_class) {
      continue;
    }
    float& cohort = adults ? herd.adults : herd.juveniles;
    const float gone = std::min(cohort, wanted);
    if (adults && cohort > 0.0F) {
      // AS THE DOOR TAKES THEM (herd_life.h, MalesAfterHandOver; 0.37.209): a
      // kind whose males are work stock gives its males first, down to its
      // sire share of the adults left; the rest, and every other kind, in
      // proportion.
      float males_first = 0.0F;
      if (herd.kind.value < config.livestock.size() &&
          config.livestock[herd.kind.value].males_work != 0) {
        const float sires = config.livestock[herd.kind.value].males_share * (cohort - gone);
        males_first = std::clamp(herd.males - sires, 0.0F, gone);
      }
      const float after_males = cohort - males_first;
      herd.males =
          after_males > 0.0F ? (herd.males - males_first) * ((cohort - gone) / after_males) : 0.0F;
    }
    cohort -= gone;
    wanted -= gone;
  }
  return wanted;
}

/// The horses' share of HeadsHandedOver off the projected herds IN THE
/// DOOR'S ORDER (district_limit.cpp, OrderHandStock; horse_lamp_memory.h):
/// the young above what each herd keeps — newborns, then juveniles — and the
/// adults only while the lamp has been dark a whole year; a kept young never.
void TakeHorsesHandedOver(const ProductionConfig& config,
                          const WorldState& world,
                          std::int64_t heads,
                          std::vector<ProjectedHerd>& herds) {
  const LivestockKindId cow = MilkKind(config);
  auto wanted = static_cast<float>(heads);
  for (ProjectedHerd& herd : herds) {
    if (!(wanted > 0.0F) || ClassOf(config, cow, herd.kind) != HeadClass::kHorses) {
      continue;
    }
    const auto kept = static_cast<float>(HorseYoungKept(
        world.horse_lamp, static_cast<std::int64_t>(herd.adults), world.calendar.day));
    float above = std::max(0.0F, herd.newborns + herd.juveniles - kept);
    const float newborns = std::min({herd.newborns, above, wanted});
    herd.newborns -= newborns;
    above -= newborns;
    wanted -= newborns;
    const float juveniles = std::min({herd.juveniles, above, wanted});
    herd.juveniles -= juveniles;
    wanted -= juveniles;
  }
  if (!HorseLampLitWithinYear(world.horse_lamp, world.calendar.day)) {
    TakeCohort(config, cow, HeadClass::kHorses, true, wanted, herds);
  }
}

/// HeadsHandedOver off the projected herds, in the door's order: the horses
/// by TakeHorsesHandedOver; every other class its adults, then its juveniles.
void TakeHandedOver(const ProductionConfig& config,
                    const WorldState& world,
                    const HeadsHandedOver& handed,
                    std::vector<ProjectedHerd>& herds) {
  const LivestockKindId cow = MilkKind(config);
  const auto take = [&config, &herds, cow](HeadClass of_class, std::int64_t heads) {
    const auto wanted = static_cast<float>(heads);
    TakeCohort(config,
               cow,
               of_class,
               false,
               TakeCohort(config, cow, of_class, true, wanted, herds),
               herds);
  };
  TakeHorsesHandedOver(config, world, handed.horses, herds);
  take(HeadClass::kCows, handed.cows);
  take(HeadClass::kOther, handed.other);
}

}  // namespace

HerdFeedForecast ForecastHerdFeed(const ProductionConfig& config,
                                  const WorldState& world,
                                  bool one_more_horse,
                                  FeedHorizon horizon,
                                  const FeedPurchase* purchase,
                                  const HeadsHandedOver* handed) {
  HerdFeedForecast forecast;
  if (config.livestock.empty() || config.feed_values.empty()) {
    return forecast;
  }
  const SimDay today = world.calendar.day;
  // THE HORIZON: the first scythes of the NEXT year (boss [68] p. 1: to the
  // new cut, not to the pasture — the horses have none; [92]). From any day
  // of this year that is the same day: next year's.
  const SimDay year_start = today - (today % kDaysPerYear);
  const SimDay cut_end_offset =
      ((static_cast<SimDay>(HayCutEndMonth(config)) + 1U) * kDaysPerMonth) - 1U;
  const SimDay this_cut_end = year_start + cut_end_offset;
  const SimDay cut_from =
      year_start + (static_cast<SimDay>(config.farming.meadow_cut_month) * kDaysPerMonth);
  // YEAR 1 LOOKS ONLY TO THE FIRST CUT (boss [87], [88], option (б)): with no
  // book of a year before, a forecast past it stood on the meadows' ceiling ×
  // a share (0.76, econ's P10) and lit the yellow all year 1 in 27 villages of
  // 27, none of which starved (core's lamp probe on 0.37.57). Before the
  // first mowing: to its first day — does what lies reach the scythes; in the
  // mowing's window: nothing to forecast yet; after it: to next year's cut on
  // the hay actually in, as every later year.
  const bool first_year = world.calendar.date.year <= 1;
  // TO NEXT YEAR'S FIRST SCYTHES, not the end of its mowing: the new cut
  // feeds the herds from its first day, and a horizon to the window's end,
  // with that cut not in it, counted three months of need against nothing —
  // 173 of 365 short days of year 2's spring yellow lay inside year 3's
  // mowing (the check-run of 0.37.57, core's lamp probe).
  //
  // TO THE NEAREST FIRST SCYTHES, IN EVERY YEAR (boss, econ-boss-hay-term-
  // 2026-10-01 [7]; 0.37.96). Until then only year 1 stopped at its own cut:
  // from year 2 the horizon ran from 1 January to the first scythes of the
  // year AFTER, a year and a half and two calvings on, and the lamp of 1
  // January spoke of the January to come — «short for 111-153 heads» with
  // 82-93 in the yards — while the canon's bot handed twenty cows over the
  // same day and the stores never ran dry (hay at May's end 27, 89-99,
  // 165-191, 166-203 t over years 1-4). Before this year's first scythes the
  // question is «does what lies reach them»; from the mowing on, next
  // year's. Past the nearest scythes the lamp is silent.
  // THE HORSE BOUGHT TO STAY keeps the long look (FeedHorizon::
  // kNextYearsScythes): this year's cut in the income and the winter after.
  // THE CUT READ BY ITS PACE (lever 1; contract core-boss-lever1-hay-pace-
  // contract-2026-10-13; econ's four-levers §2a): inside this year's cut,
  // from its second dry working day, the hay still to come is what the
  // mowers' own pace lays in the dry working days left - not the grass that
  // stands and not last year's book. Year 1 included: until 0.37.250 the
  // lamp said nothing through the first mowing and lit in September, after
  // the last day it could be answered.
  const CutPace pace = PaceOfTheCut(config, world, cut_from, this_cut_end);
  // TO THE DAY THE NEW HAY CAN BE IN A STORE (FarmingConfig::
  // hay_new_cut_in_store_days): from this year's first scythes on, the need
  // runs that many days into next year's cut - on its first day the herds
  // still eat the old hay.
  SimDay horizon_end = cut_from + kDaysPerYear + config.farming.hay_new_cut_in_store_days;
  // THE HORSE BOUGHT TO STAY LOOKS PAST YEAR 1's FIRST CUT TOO (#1113; boss,
  // tasks.db, cure (a)): year 1 stopped every reader at the first scythes, so
  // the kTooFewHorses «hay first» test asked only whether the start's rick
  // carried today's herds and one more horse to ~day 22, and the novice
  // bought ~120 horses on it in year 1 (MR.2's trace, wip/mr 3afc1f89). The
  // long question now keeps its year, with the cut it has no book for priced
  // from the grass standing on the meadows (HayStandingOnMeadows): a CEILING,
  // so it errs toward yes. The yellow lamp's nearest horizon keeps option (б).
  const bool first_year_long_look = first_year && today <= this_cut_end && !pace.known &&
                                    horizon == FeedHorizon::kNextYearsScythes;
  // THE WINDOW'S YEAR 1 BEFORE ITS CUT (#1289; kWindowByNorm): to next year's
  // scythes with this year's cut by the norm. Every other case of the
  // window's horizon reads as the lamp's.
  const bool window_norm =
      horizon == FeedHorizon::kWindowByNorm && first_year && today < cut_from && !pace.known;
  if (first_year && today <= this_cut_end && !pace.known && !first_year_long_look && !window_norm) {
    horizon_end = today < cut_from ? cut_from : today;
  } else if ((horizon == FeedHorizon::kNearestScythes ||
              (horizon == FeedHorizon::kWindowByNorm && !window_norm)) &&
             today < cut_from) {
    // This year's scythes, and the same days into their cut: one horizon from
    // either side of the year's turn. (Year 1 before its first cut is the
    // branch above and stops on the first scythes: it has no book to promise
    // a cut by, and the start's rick is sized to that day.)
    horizon_end = cut_from + config.farming.hay_new_cut_in_store_days;
  }
  const auto horizon_days = static_cast<std::uint32_t>(horizon_end - today);
  forecast.horizon_days = static_cast<std::uint16_t>(horizon_days);
  // THE CUT STILL AHEAD THIS YEAR LANDS DAY BY DAY over the mowing's window,
  // from the first day of the meadows' cut month to the end of the cut, as
  // the mowing lays it (LayMownShare). Landed whole on the cut's last day it
  // lit the yellow in every January of 0.37.57's first pair: the start's hay
  // is sized to the first scythes (day 22). Year 1 expects none (above), save
  // the horse's long look, which takes the grass standing (#1113).
  // By the pace where it is known; else, from year 2, last year's book as
  // before (the days before the cut and its first dry working day).
  Grams expected_cut = 0;
  if (pace.known) {
    expected_cut = pace.hay_to_come;
  } else if (window_norm) {
    expected_cut = NormCutYearOne(config, world, cut_from, this_cut_end);
  } else if (first_year_long_look) {
    expected_cut = HayStandingOnMeadows(config, world);
  } else if (!first_year && today <= this_cut_end) {
    expected_cut = ExpectedCutStillToCome(config, world);
  }
  const std::uint32_t cut_first_day = today <= cut_from ? cut_from - today : 0U;
  const std::uint32_t cut_last_day = today <= this_cut_end ? this_cut_end - today : 0U;
  const float cut_kg_a_day =
      expected_cut > 0 ? static_cast<float>(expected_cut) / static_cast<float>(kGramsPerKilogram) /
                             static_cast<float>(cut_last_day - cut_first_day + 1U)
                       : 0.0F;

  const bool stable_built = StableBuilt(world, config);
  std::vector<ProjectedHerd> herds;
  bool horse_added = !one_more_horse;
  for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
    const HerdRow& herd = world.herds.rows[row];
    if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
      continue;  // a family herd eats from its family's pantry, not the farm's
    }
    ProjectedHerd projected = ProjectHerd(config, world, row, stable_built);
    if (!horse_added && herd.kind.value == config.horse_kind.value) {
      projected.adults += 1.0F;
      horse_added = true;
    }
    herds.push_back(projected);
  }
  if (!horse_added && config.horse_kind.value < config.livestock.size()) {
    ProjectedHerd team;
    team.kind = config.horse_kind;
    team.adults = 1.0F;
    herds.push_back(team);
  }
  // THE HEADS ASKED ABOUT AS HANDED OVER (HeadsHandedOver; 0.37.137): off the
  // herds that stand, before the stock on the road is added — a head bought
  // and not yet come is not the chairman's to hand back.
  if (handed != nullptr) {
    TakeHandedOver(config, world, *handed, herds);
  }
  // THE MOVES ALREADY MADE (econ §4а, boss [70]): stock bought on the limit
  // stands in from its day; its heads eat from the farm's stores.
  for (const LivestockArrivalRow& arrival : world.livestock_arrivals.rows) {
    if (arrival.kind.value >= config.livestock.size()) {
      continue;
    }
    ProjectedHerd coming;
    coming.kind = arrival.kind;
    const auto heads = static_cast<float>(arrival.head_count);
    // Young stock joins newborn_count and climbs the ladder (limit_state.h).
    (arrival.stage == LivestockArrivalStage::kAdultStart ? coming.adults : coming.newborns) = heads;
    coming.from_day = arrival.arrive_day > today ? arrival.arrive_day - today : 0U;
    herds.push_back(coming);
  }
  if (herds.empty()) {
    return forecast;
  }
  float standing = 0.0F;
  for (const ProjectedHerd& herd : herds) {
    standing += HeadsOf(herd);
  }
  const auto standing_heads = static_cast<std::int64_t>(std::ceil(standing));

  std::vector<float> held_kg = HerdFeedHeldKg(config, world);
  const std::vector<std::uint8_t> peoples_foods = PeoplesFoods(config, world);
  // The feed lots on the road land on their day, where a store would take
  // them (a lot with nowhere to go is refused at the door — kNowhereToStore).
  std::vector<std::pair<std::uint32_t, std::vector<float>>> landings;
  for (const LimitDeliveryRow& cart : world.limit_deliveries.rows) {
    if (cart.outbound != 0) {
      continue;  // the plan's own delivery leaves the village (stage В5)
    }
    std::vector<float> goods(held_kg.size(), 0.0F);
    bool any = false;
    for (std::size_t resource = 0; resource < cart.goods.size() && resource < goods.size();
         ++resource) {
      const ResourceId id = DefIdFromIndex<ResourceIdTag>(resource);
      if (cart.goods[resource] <= 0 || !(config.feed_values[resource] > 0.0F) ||
          !StoreHasRoomFor(config, world, id)) {
        continue;
      }
      goods[resource] =
          static_cast<float>(cart.goods[resource]) / static_cast<float>(kGramsPerKilogram);
      any = true;
    }
    if (any) {
      landings.emplace_back(cart.arrive_day > today ? cart.arrive_day - today : 0U,
                            std::move(goods));
    }
  }
  // AND THE PURCHASE ASKED ABOUT (FeedPurchase; 0.37.122), by the carts' own
  // rule: only what a store has room for lands.
  if (purchase != nullptr && purchase->any) {
    std::vector<float> goods(held_kg.size(), 0.0F);
    bool any = false;
    for (std::size_t resource = 0; resource < purchase->goods_kg.size() && resource < goods.size();
         ++resource) {
      if (purchase->goods_kg[resource] > 0.0F &&
          StoreHasRoomFor(config, world, DefIdFromIndex<ResourceIdTag>(resource))) {
        goods[resource] = purchase->goods_kg[resource];
        any = true;
      }
    }
    if (any) {
      landings.emplace_back(purchase->land_day, std::move(goods));
    }
  }
  // AND THE HARVEST STILL TO COME, WHAT THE HERDS MAY EAT OF IT (#1292; econ,
  // core-boss-1289-herd-plan [40]): the straw and the grain the stores will
  // take before the horizon ends, by the book, landing on the last day of
  // each crop's window. Without it the forecast fed the herds from today's
  // grain and straw alone, and the year's barley, oats and straw - about 24k
  // fodder units in a fed lit village-year - came in unseen.
  for (const auto& [day, goods] : ScheduledHarvestFeed(config, world, horizon_days)) {
    landings.emplace_back(day, goods);
  }

  // The night pasture's conditions by month, once: they walk the residents.
  std::vector<std::uint8_t> team_out_in(kMonthsPerYear, 0);
  for (std::uint8_t month = 0; month < kMonthsPerYear; ++month) {
    team_out_in[month] = TeamOutInMonth(config, world, month) ? 1U : 0U;
  }
  constexpr std::uint32_t kNeverOut = std::numeric_limits<std::uint32_t>::max();
  float short_units = 0.0F;  // every uncovered fodder unit of the days forecast
  std::vector<std::uint32_t> out_day(held_kg.size(), kNeverOut);
  std::vector<FeedDayNeed> needs;
  std::vector<std::size_t> needs_of;  // index into `herds` per need
  std::vector<float> covered;
  std::vector<float> held_before;
  for (std::uint32_t day = 0; day < horizon_days; ++day) {
    const auto month = static_cast<std::uint8_t>(DateFromDay(today + day).month);
    if (cut_kg_a_day > 0.0F && day >= cut_first_day && day <= cut_last_day &&
        config.hay_resource.value < held_kg.size()) {
      held_kg[config.hay_resource.value] += cut_kg_a_day;
    }
    for (const auto& [landing_day, goods] : landings) {
      if (landing_day == day) {
        for (std::size_t resource = 0; resource < goods.size(); ++resource) {
          held_kg[resource] += goods[resource];
        }
      }
    }
    const bool team_out = month < team_out_in.size() && team_out_in[month] != 0;
    needs.clear();
    needs_of.clear();
    for (std::size_t index = 0; index < herds.size(); ++index) {
      ProjectedHerd& herd = herds[index];
      if (day < herd.from_day) {
        continue;
      }
      const LivestockDef& kind = config.livestock[herd.kind.value];
      // Today (day 0) is fed from what stands; the herds grow from tomorrow,
      // as ForecastHerdHeads counts them.
      if (day > 0) {
        ProjectHerdDay(config, month, herd);
      }
      HerdRow row;
      row.adult_count = RoundHeads(herd.adults);
      row.juvenile_count = RoundHeads(herd.juveniles);
      const bool grazing = herd.kind.value != config.horse_kind.value || team_out;
      const float units = FeedNeedUnits(config, kind, row, month, grazing);
      if (units > 0.0F) {
        needs.push_back(FeedDayNeed{.kind = herd.kind, .units = units});
        needs_of.push_back(index);
      }
    }
    // A FEED RUNS OUT when the day's drain takes it from held to nothing: a
    // feed the herds never had did not run out (static review of 0.37.57 —
    // every absent feed read «out on day 0», and the cows short of hay on day
    // 60 were said short of silage they never held).
    held_before = held_kg;
    DrainFeedDay(config, peoples_foods, needs, held_kg, covered);
    for (std::size_t resource = 0; resource < held_kg.size(); ++resource) {
      if (out_day[resource] == kNeverOut && held_before[resource] >= kOutKg &&
          held_kg[resource] < kOutKg) {
        out_day[resource] = day;
      }
    }
    float unfed_heads = 0.0F;
    for (std::size_t index = 0; index < needs.size(); ++index) {
      forecast.need_units += needs[index].units;
      forecast.covered_units +=
          covered[index] < needs[index].units ? covered[index] : needs[index].units;
      if (covered[index] + kFedSlackUnits >= needs[index].units) {
        continue;
      }
      short_units += needs[index].units - covered[index];
      const ProjectedHerd& herd = herds[needs_of[index]];
      unfed_heads += (needs[index].units - covered[index]) / needs[index].units * HeadsOf(herd);
      if (forecast.short_ahead) {
        continue;
      }
      // THE FIRST SHORT DAY: its herd, and among its kind's links the feed
      // that ran out earliest (boss [86] p. 3: the truth of what runs out).
      forecast.short_ahead = true;
      forecast.days_ahead = static_cast<std::uint16_t>(day);
      forecast.herd = herd.id;
      std::uint32_t earliest = kNeverOut;
      for (const FeedLinkDef& link : config.feed_links) {
        if (link.kind.value != herd.kind.value || link.work_only != 0 ||
            link.resource.value >= out_day.size()) {
          continue;
        }
        if (forecast.first_short.value == kInvalidDefIdValue ||
            out_day[link.resource.value] < earliest) {
          earliest = out_day[link.resource.value];
          forecast.first_short = link.resource;
        }
      }
    }
    const auto heads = static_cast<std::int64_t>(std::ceil(unfed_heads));
    forecast.heads_short = heads > forecast.heads_short ? heads : forecast.heads_short;
  }
  // NEVER MORE HEADS THAN STAND TODAY (0.37.96; boss, econ-boss-hay-term
  // [7] (б)): the calves to come are in the need above, and a chairman told
  // «short for 127 heads» with 92 in the yards is told a number he cannot
  // act on. The stock on the road counts: its heads are bought already.
  forecast.heads_short = std::min(forecast.heads_short, standing_heads);
  // The shortage in the hay's own measure (HerdFeedForecast::short_hay_grams).
  const float hay_value = config.hay_resource.value < config.feed_values.size()
                              ? config.feed_values[config.hay_resource.value]
                              : 0.0F;
  forecast.short_hay_grams = hay_value > 0.0F ? GramsFromKilograms(short_units / hay_value) : 0;
  forecast.by_pace = pace.known;
  forecast.grass_bound = pace.known && pace.grass_bound;
  forecast.cut_end_day = this_cut_end;
  forecast.pace_kg_a_day = pace.kg_a_day;
  forecast.dry_work_days_left = pace.dry_days_left;
  return forecast;
}

std::uint8_t HayCutLastMonth(const ProductionConfig& config) {
  return HayCutEndMonth(config);
}

bool CutCanBeHurried(const ProductionConfig& config, const WorldState& world) {
  // ONLY IN THE MONTHS OF THE MOWING (0.37.137): the grass nobody mowed
  // stands in its cut's phase to the year's end, and the avral's door takes
  // an order on it in January.
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  if (month < config.farming.meadow_cut_month || month > HayCutEndMonth(config)) {
    return false;
  }
  return std::ranges::any_of(world.fields.rows, [](const FieldRow& field) {
    if (field.kind == LandKind::kArable || field.phase != FieldPhase::kHarvest ||
        !(field.work_days_remaining > 0.0F)) {
      return false;
    }
    // The avral on THIS cut (FieldRow::rush_phase), below its last step: the
    // door of the move (kDeclareRush) takes one step more.
    const std::int64_t step = field.rush_phase == field.phase ? field.rush_step : 0;
    return step < kMaxRushStep;
  });
}

}  // namespace core
