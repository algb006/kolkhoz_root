#include "core_world/era_readiness.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core_common/calendar.h"
#include "core_log/log.h"

namespace core {

namespace {

// -- THE WEIGHTS ARE THE DESIGN'S STRUCTURE (epochs design §6) --------------
//
// Not balance, and that is why they are here rather than in a table: moving
// one is a change to what the transition MEANS, which arrives as a design
// decision and a commit, not as a balancer's afternoon. The two absentees of
// Era I are the reason the divisors are 80 and 90 and not 100 — finance has
// no money to measure and the chairman's standing has no role-playing lines
// to be earned in, and a nought in either cell would lie by its full weight.
constexpr float kWeightPlan = 25.0F;
constexpr float kWeightWinterStocks = 20.0F;
constexpr float kWeightMechanisation = 20.0F;
constexpr float kWeightFunds = 15.0F;
constexpr float kWeightSatisfaction = 35.0F;
constexpr float kWeightEffort = 25.0F;
constexpr float kWeightSocialObjects = 20.0F;
constexpr float kWeightDemography = 10.0F;

// -- AND THESE ARE BALANCE, AND STUB (boss, parcel 132; polish backlog P3) --
//
// Tuned by runs. They stand here and not in a table only because no readiness
// table exists yet; the day one does, these move into it and this block goes.
constexpr float kEconomicThreshold = 55.0F;        ///< STUB
constexpr float kSocialThreshold = 50.0F;          ///< STUB
constexpr float kBirthsPerThousandTarget = 30.0F;  ///< STUB
constexpr float kChildShareTarget = 0.30F;         ///< STUB
/// «доля детей до 16 лет» — the design's own boundary for the age structure,
/// and NOT the same number as adulthood for work: this is a demographic
/// bracket, not a threshold anybody is employed at.
constexpr float kChildUntilYears = 16.0F;
constexpr std::uint8_t kSocialObjectsRequired = 4;  ///< STUB — «4 из 6»
constexpr float kOfficeWearAtMostPercent = 1.0F;    ///< units rules §11, not a stub
constexpr std::uint8_t kEraOneUnitLevel = 2;        ///< units rules §11, not a stub

/// The satisfaction metric's two frozen parts in Era I, in points of its
/// hundred: «общее дело» at 20 and «нужды» at 25, drawn once at genesis and
/// never written again (family_state.h, both marked STUB there).
///
/// CARRIED AND PRINTED ALWAYS, never only when it is large (boss, parcel
/// 132). A mark that appears at a threshold teaches its reader that its
/// absence means "all honest", and on the day it is forgotten the absence is
/// read as an answer.
constexpr float kSatisfactionStubPointsEraOne = 45.0F;

/// The answer when the era's upper variety norm cannot be read at all — a
/// threshold no table can reach, so an unknown gate stays SHUT. Nought would
/// have opened it for everybody, which is the dangerous direction.
constexpr float kVarietyThresholdUnknown = 1.0e9F;

/// @brief A score clamped into the 0..100 a component is weighed on. The
/// ceiling is the design's own: overshooting one component may not buy
/// another.
ReadinessComponent Scored(float value) {
  return ReadinessComponent{
      .score = std::clamp(value, 0.0F, 100.0F), .available = 1, .measured = 1};
}

/// @brief The era HAS this component and the closed year could not be scored
/// on it. It stays in the divisor and scores nought — the settlement has
/// nothing to show, which is what readiness asks about.
constexpr ReadinessComponent kUnmeasured = {.score = 0.0F, .available = 1, .measured = 0};

/// @brief THE ERA DOES NOT HAVE IT — finance and the chairman's standing in
/// Era I. This one and only this one leaves the divisor, because a nought
/// here would lie by the component's full weight about a thing the era cannot
/// possess. Unused in Era I's own code path: the two absentees are absent by
/// never being listed in Index() below, which is the same statement made
/// where the reader can see it.
[[maybe_unused]] constexpr ReadinessComponent kNotInThisEra = {
    .score = 0.0F, .available = 0, .measured = 0};

/// @brief Weighted mean over the components the ERA HAS, normalised to 100.
///
/// THE DIVISOR IS `available` AND NOT `measured`, which is the whole repair
/// of 2026-09-17. A year that could not be scored on a component still pays
/// its weight — it simply pays nought — because readiness asks what the
/// settlement can SHOW, and a year with nothing to show is not a year with
/// less to be judged by. Dropping the unmeasured from the divisor made the
/// emptiest year score best, and eight villages of nine passed a three-year
/// gate for thirty-three years running.
float Index(std::initializer_list<std::pair<ReadinessComponent, float>> weighted) {
  float sum = 0.0F;
  float weights = 0.0F;
  for (const auto& [component, weight] : weighted) {
    if (component.available == 0) {
      continue;
    }
    sum += component.score * weight;
    weights += weight;
  }
  return weights > 0.0F ? sum / weights : 0.0F;
}

/// @brief The share, as a percentage capped at 100, or absent when there was
/// nothing to divide by.
///
/// A DIVISOR OF NOUGHT IS NOT A SCORE OF NOUGHT. A year in which nobody was
/// able-bodied, or no building stood, is a year in which the question could
/// not be asked — and a nought there would be the score of a settlement that
/// did everything wrong.
ReadinessComponent SharePercent(float part, float whole) {
  return whole > 0.0F ? Scored(part / whole * 100.0F) : kUnmeasured;
}

}  // namespace

float ReadFoodVarietyThreshold(const ITableSet& tables, Epoch era) {
  // THE NORM IS A RANGE AND THE TABLE HELD ONE END OF IT (boss, parcel 138).
  // Metrics §8 gives Era I «3–4 категории», II «5–6», III «7 и больше»;
  // food.csv carried only the LOWER end — below which the table is poor —
  // while the transition block asks for the UPPER: «верх нормы своей эпохи,
  // I → II — 4». Reading `categories_norm_epoch_N` here was the right number
  // for the wrong question, and there was nowhere else to take one from.
  //
  // Era III has no upper end BY DESIGN («7 и больше»), which is a named edge
  // and not a missing row.
  const ITable* food = tables.FindTable("food");
  if (food == nullptr) {
    return kVarietyThresholdUnknown;
  }
  // THE HUMAN NUMBER, BY ITS NAME. This read `+ 1` for a day and asked an
  // Era I world for `categories_top_epoch_2` — while the warning printed that
  // very key nine times a run and I read it as "villages that reached Epoch
  // II". The enum already counts from one; `EpochHumanNumber` exists so the
  // next reader writes no arithmetic at all (world_state.h).
  const std::string key = "categories_top_epoch_" + std::to_string(EpochHumanNumber(era));
  const std::uint32_t row = food->FindRowByKey(key);
  const std::uint32_t column = food->FindColumn("value");
  if (row == kNoTableRow || column == kNoTableColumn) {
    // A MISSING THRESHOLD MAY NOT OPEN A GATE. Returning nought would make
    // the block trivially met — every table is above nothing — so the refusal
    // is loud and the block stays shut until the key exists. The near miss is
    // the reason for the noise: the lower end is one row away and reads
    // plausibly.
    LogWarning("food: no `" + key +
               "` — the era's variety norm has an upper end and the table holds only the lower; "
               "the transition's variety block stays shut until it does");
    return kVarietyThresholdUnknown;
  }
  const std::optional<double> value = food->CellReal(row, column);
  return value.has_value() ? static_cast<float>(*value) : kVarietyThresholdUnknown;
}

float ReadLifeSpeedup(const ITableSet& tables) {
  const ITable* life = tables.FindTable("life");
  if (life == nullptr) {
    return 1.0F;
  }
  const std::uint32_t row = life->FindRowByKey("life_speedup");
  const std::uint32_t column = life->FindColumn("value");
  if (row == kNoTableRow || column == kNoTableColumn) {
    return 1.0F;
  }
  const std::optional<double> value = life->CellReal(row, column);
  // The same row core_residents reads, never a copy of its number: a second
  // home for the biology factor would age the readiness score against the
  // rest of the world without a single test going red.
  return value.has_value() && *value > 0.0 ? static_cast<float>(*value) : 1.0F;
}

ReadinessCatalog ReadReadinessCatalog(const ITableSet& tables, Epoch era) {
  ReadinessCatalog catalog;
  const ITable* types = tables.FindTable("unit_types");
  if (types == nullptr) {
    return catalog;
  }
  const std::uint32_t class_column = types->FindColumn("class");
  const std::uint32_t era_column = types->FindColumn("era");
  const std::uint32_t parent_column = types->FindColumn("parent");
  const std::uint32_t one_family_column = types->FindColumn("one_family");
  // Same off-by-one as the threshold above and the same cure: unit_types.csv's
  // `era` column is the human number. With the `+ 1` this list held Era II's
  // social objects in an Era I world, so "0 of 6 ever marked" was counted over
  // the wrong six entirely.
  const auto wanted_era = static_cast<std::int64_t>(EpochHumanNumber(era));
  for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
    const UnitTypeId id = DefIdFromRow<UnitTypeIdTag>(row);
    // A FAMILY'S OWN HOUSE IS NOT THE FARM'S BUILDING, and the design says so
    // in as many words: «жилые дома семей не входят» into the state of the
    // funds. Everything else the village raises stands on the kolkhoz books.
    const std::optional<std::int64_t> one_family = one_family_column == kNoTableColumn
                                                       ? std::nullopt
                                                       : types->CellInteger(row, one_family_column);
    if (!one_family.has_value() || *one_family == 0) {
      catalog.kolkhoz_types.push_back(id);
    }
    if (class_column == kNoTableColumn) {
      continue;
    }
    const std::string_view kind = types->CellText(row, class_column);
    // THE LIST COMES OUT OF THE TABLE, not out of six names written here: the
    // design's own instruction is «берётся он из класса social базы дизайна по
    // колонке эпохи», so a seventh object added to the table is a seventh the
    // component counts, without anybody remembering to come back here.
    //
    // FREE-STANDING ONLY. A module hung on somebody else's plot — the
    // boarding wing on the school, the sports ground beside it — is a RUNG of
    // its parent and not an object of the norm; counting it would let one
    // school answer the list twice.
    const bool free_standing =
        parent_column == kNoTableColumn || types->CellText(row, parent_column).empty();
    const std::optional<std::int64_t> row_era =
        era_column == kNoTableColumn ? std::nullopt : types->CellInteger(row, era_column);
    if (kind == "social" && free_standing && row_era.has_value() && *row_era == wanted_era) {
      catalog.social_objects.push_back(id);
    }
  }
  catalog.office = DefIdFromRow<UnitTypeIdTag>(types->FindRowByKey("farm_office"));
  catalog.repair_base = DefIdFromRow<UnitTypeIdTag>(types->FindRowByKey("workshops"));
  return catalog;
}

void ScoreReadiness(const ReadinessCatalog& catalog,
                    float food_variety_categories,
                    float life_speedup,
                    WorldState& current) {
  const YearLedger& book = current.ledger.closed;
  ReadinessState& out = current.readiness;
  out.year = book.year;

  // -- THE PLAN, over up to three years ------------------------------------
  //
  // The ring takes a year only if the district SPOKE of it: a year with no
  // figure is not a year of this average, and folding it in as a nought would
  // score a settlement for a plan it was never given.
  if (book.plan_percent_known != 0) {
    out.plan_percent_years[0] = out.plan_percent_years[1];
    out.plan_percent_years[1] = out.plan_percent_years[2];
    out.plan_percent_years[2] = book.plan_percent;
    out.plan_years_filled = out.plan_years_filled < 3
                                ? static_cast<std::uint8_t>(out.plan_years_filled + 1U)
                                : static_cast<std::uint8_t>(3);
  }
  if (out.plan_years_filled > 0) {
    float sum = 0.0F;
    for (std::uint8_t back = 0; back < out.plan_years_filled; ++back) {
      sum += out.plan_percent_years[2 - back];
    }
    out.economy.plan = Scored(sum / static_cast<float>(out.plan_years_filled));
  } else {
    out.economy.plan = kUnmeasured;
  }

  // -- THE WINTERING, off the three counts booked on 1 December ------------
  //
  // The division happens here and only here, over numbers each of which can
  // be checked against the world. Absent when no December was lived: a
  // campaign that ended in the autumn has no wintering to judge, which is a
  // different fact from a wintering that failed.
  bool wintering_closed = false;
  if (book.winter_cover_taken != 0 && book.winter_days_dec1 > 0) {
    const auto needed = static_cast<float>(book.winter_days_dec1);
    const float food = std::min(book.food_days_dec1 / needed, 1.0F);
    const float feed = std::min(book.feed_days_dec1 / needed, 1.0F);
    out.economy.winter_stocks = Scored((food + feed) * 0.5F * 100.0F);
    // CLOSED MEANS BOTH REACHED THE GRASS, not that their mean did: a barn
    // full of hay does not feed the village, and the design's word for the
    // block is «кормовой И продовольственный баланс сошёлся».
    wintering_closed = book.food_days_dec1 >= needed && book.feed_days_dec1 >= needed;
  } else {
    out.economy.winter_stocks = kUnmeasured;
  }
  out.wintering_run =
      wintering_closed ? static_cast<std::uint8_t>(out.wintering_run < 255 ? out.wintering_run + 1U
                                                                           : out.wintering_run)
                       : static_cast<std::uint8_t>(0);

  // -- MECHANISATION, both halves of the ratio already in one book ---------
  out.economy.mechanisation =
      SharePercent(book.horse_backed_assignment_days, book.total_assignment_days);

  // -- THE FUNDS: a hundred less the mean wear of the farm's buildings -----
  float wear_sum = 0.0F;
  std::uint32_t standing = 0;
  for (const UnitRow& unit : current.units.rows) {
    if (unit.level == 0 || unit.dead != 0) {
      continue;  // a marked site is not a building and a ruin is not on the books
    }
    const bool kolkhoz = std::ranges::any_of(
        catalog.kolkhoz_types, [&unit](UnitTypeId type) { return type.value == unit.type.value; });
    if (!kolkhoz) {
      continue;
    }
    wear_sum += unit.wear / kWearScale * 100.0F;
    ++standing;
  }
  out.economy.funds =
      standing > 0 ? Scored(100.0F - (wear_sum / static_cast<float>(standing))) : kUnmeasured;

  // -- SATISFACTION, the mean over families AND days of the year -----------
  // A MEAN AND NOT A SHARE. Satisfaction is already a metric on 0..100, so
  // the mean of it IS the score — putting it through SharePercent multiplied
  // a 55 by a hundred and the clamp made it 100, in every village of every
  // year. Found by printing the components at both ends of the campaign
  // (boss, parcel 142): a component reading exactly 100 in year 1 AND year 33
  // is not a village that is content, it is a scale that is not being read.
  out.society.satisfaction =
      book.satisfaction_samples > 0
          ? Scored(book.satisfaction_sum / static_cast<float>(book.satisfaction_samples))
          : kUnmeasured;
  out.satisfaction_stub_points =
      out.society.satisfaction.available != 0 ? kSatisfactionStubPointsEraOne : 0.0F;

  // -- THE EFFORT GIVEN TO THE KOLKHOZ -------------------------------------
  out.society.kolkhoz_effort = SharePercent(book.total_assignment_days, book.able_bodied_days);

  // -- THE SOCIAL OBJECTS OF THE ERA'S LIST --------------------------------
  std::uint32_t built = 0;
  for (const UnitTypeId type : catalog.social_objects) {
    const bool stands = std::ranges::any_of(current.units.rows, [type](const UnitRow& unit) {
      return unit.type.value == type.value && unit.level >= 1 && unit.dead == 0;
    });
    built += stands ? 1U : 0U;
  }
  out.society.social_objects =
      SharePercent(static_cast<float>(built), static_cast<float>(catalog.social_objects.size()));

  // -- DEMOGRAPHY: the birth rate and the share of children ----------------
  const auto population = static_cast<float>(current.residents.rows.size());
  if (population > 0.0F) {
    const float per_thousand = static_cast<float>(book.births) / population * 1000.0F;
    // CHILDHOOD IN BIOLOGICAL YEARS, like every other age rule in this tree:
    // the state stores only birth_day, and calendar years against a
    // biological threshold is the mistake that once read a village of adults
    // as a village of children.
    std::uint32_t children = 0;
    for (const ResidentRow& person : current.residents.rows) {
      const float age = BiologicalAgeYears(life_speedup, person.birth_day, current.calendar.day);
      children += age < kChildUntilYears ? 1U : 0U;
    }
    const float child_share = static_cast<float>(children) / population;
    const float births_score = per_thousand / kBirthsPerThousandTarget * 100.0F;
    const float age_score = child_share / kChildShareTarget * 100.0F;
    // EACH HALF CAPPED BEFORE THEY ARE AVERAGED, not after: a village of
    // nothing but children would otherwise buy its way past a birth rate of
    // nought, which is the compensation the design forbids one storey up.
    out.society.demography =
        Scored((std::min(births_score, 100.0F) + std::min(age_score, 100.0F)) * 0.5F);
  } else {
    out.society.demography = kUnmeasured;
  }

  // -- THE TWO INDICES AND THE RUN -----------------------------------------
  out.economic_index = Index({{out.economy.plan, kWeightPlan},
                              {out.economy.winter_stocks, kWeightWinterStocks},
                              {out.economy.mechanisation, kWeightMechanisation},
                              {out.economy.funds, kWeightFunds}});
  out.social_index = Index({{out.society.satisfaction, kWeightSatisfaction},
                            {out.society.kolkhoz_effort, kWeightEffort},
                            {out.society.social_objects, kWeightSocialObjects},
                            {out.society.demography, kWeightDemography}});
  const bool both_above =
      out.economic_index >= kEconomicThreshold && out.social_index >= kSocialThreshold;
  out.both_above_run =
      both_above ? static_cast<std::uint8_t>(out.both_above_run < 255 ? out.both_above_run + 1U
                                                                      : out.both_above_run)
                 : static_cast<std::uint8_t>(0);

  // -- THE BLOCKS ----------------------------------------------------------
  //
  // Facts and not scores, each standing apart from the weights: while one is
  // unmet the transition does not open however high the indices stand, which
  // is what makes them blocks rather than a ninth component.
  out.blocks.social_objects = built >= kSocialObjectsRequired ? 1U : 0U;
  out.blocks.wintering_two_years = out.wintering_run >= 2 ? 1U : 0U;
  // VARIETY IN EVERY SEASON INCLUDING WINTER, and it is the village's mean
  // rather than any one family's: the design's word is «колхозное среднее не
  // ниже порога во все четыре сезона». The worst season of the year decides —
  // a summer of six categories does not answer for a winter of two — and all
  // four must have been LIVED: a year short of a season has not failed the
  // block, it has not been asked, which is why the count is tested and not
  // only the number.
  out.blocks.food_variety = (book.variety_seasons_seen >= kSeasonsPerYear &&
                             book.worst_season_variety >= food_variety_categories)
                                ? 1U
                                : 0U;
  // OWN TRACTION OR A REPAIR BASE. The traction half is read off the year
  // rather than off the stable: a horse that pulled nothing is not the farm
  // pulling its own work.
  const bool repair_base_stands =
      catalog.repair_base.value != kInvalidDefIdValue &&
      std::ranges::any_of(current.units.rows, [&catalog](const UnitRow& unit) {
        return unit.type.value == catalog.repair_base.value && unit.level >= 1 && unit.dead == 0;
      });
  out.blocks.own_traction =
      (book.horse_backed_assignment_days > 0.0F || repair_base_stands) ? 1U : 0U;
  // EVERY UNIT OF THE ERA AT ITS LEVEL. Sites do not count — a marked plot is
  // not a unit that has failed to be upgraded, it is a unit that does not
  // exist yet.
  const bool all_at_level = std::ranges::all_of(current.units.rows, [](const UnitRow& unit) {
    return unit.level == 0 || unit.dead != 0 || unit.level >= kEraOneUnitLevel;
  });
  out.blocks.units_at_level = all_at_level ? 1U : 0U;
  // THE OFFICE, STANDING AND JUST REPAIRED. One per cent and not nought,
  // because nought is unreachable: wear runs continuously, so a threshold of
  // nought would be a block that can never be met — the same defect as a rule
  // that can never fire (units rules §11).
  out.blocks.office_repaired =
      catalog.office.value != kInvalidDefIdValue &&
              std::ranges::any_of(current.units.rows,
                                  [&catalog](const UnitRow& unit) {
                                    return unit.type.value == catalog.office.value &&
                                           unit.level >= 1 && unit.dead == 0 &&
                                           unit.wear / kWearScale * 100.0F <=
                                               kOfficeWearAtMostPercent;
                                  })
          ? 1U
          : 0U;
}

}  // namespace core
