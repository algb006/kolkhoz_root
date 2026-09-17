/// @file
/// @brief Readiness for the era transition: the two indices, their components
/// and the run of years they have held (epochs design §6; boss, parcels 128
/// and 132).
/// @threading PARALLEL_READONLY
/// Plain data. Written once a year, on the FIRST day of the new year and out
/// of the year that just closed, in the sequential slot that turns the books
/// (core_world/era_readiness.h). Read by anyone between steps.
///
/// THE INDEX IS DIVIDED BY WHAT THE ERA CAN HAVE, not by a hundred. A
/// component the era does not have is NULL and not nought, because nought
/// would lie by exactly its weight: Era I has no money and no chairman's
/// standing, so its economy divides by 80 and its society by 90. The
/// threshold does not move with the divisor — 55 means "fifty-five per cent
/// of what is possible", and that is why the index of one era is NOT
/// comparable with another's. Two instruments under one name.
///
/// A COMPONENT IS CAPPED AT 100 BEFORE IT IS WEIGHED, so that overshooting
/// one does not buy another.
///
/// THE HISTORY IS THE POINT, not the value. "Both indices above their
/// thresholds for three years running" cannot be answered by a number that
/// is overwritten every January, and neither can "the wintering closed two
/// years in a row" — a quantity that needs a HISTORY rather than a value.
/// Both are kept as runs rather than as arrays of years: the design asks how
/// many in a row, nothing asks which ones.

#ifndef CORE_COMMON_READINESS_STATE_H_
#define CORE_COMMON_READINESS_STATE_H_

#include <array>
#include <cstdint>

namespace core {

/// @brief One scored component of an index, 0..100, or the statement that
/// this era does not have it.
///
/// TWO FIELDS AND NOT A SENTINEL, and the reason is written all over this
/// tree: a nought that means "not measured" is indistinguishable from a
/// nought that means "measured and bad", and the design demands the player
/// be told which — «финансы в этой эпохе не измеряются», а не молча выпал.
struct ReadinessComponent {
  /// The score, 0..100. Nought when `measured` is 0 — and that nought is the
  /// honest answer rather than a missing value, see below.
  float score = 0.0F;

  /// 1 when THIS ERA HAS the component at all. Only this leaves the divisor.
  std::uint8_t available = 0;

  /// 1 when the closed year could actually be scored on it.
  ///
  /// TWO BYTES AND NOT ONE, and the day one byte was enough is the day it was
  /// measured: with a single `available` carrying both meanings, eight
  /// villages of nine held both indices above their thresholds for all
  /// thirty-three years where three are needed. The index divided by less and
  /// less the emptier the year was, so THE EMPTIEST YEAR SCORED BEST.
  ///
  /// The difference is one word, and it was in boss's rule from the start:
  /// a component the ERA does not have is NULL, and a component this YEAR
  /// could not measure is not. A settlement whose plan the district has not
  /// judged is not a settlement the plan does not apply to — it is one with
  /// nothing to show, and the index is READINESS: «мы не знаем» may not score
  /// better than «мы знаем, и хорошо».
  std::uint8_t measured = 0;
};

/// @brief The four components of the economic index in Era I. Finance (20)
/// is absent by design — money is an Era II thing — so these four divide by
/// 80.
struct EconomicReadiness {
  ReadinessComponent plan;           ///< Weight 25. Mean delivery % of up to three years.
  ReadinessComponent winter_stocks;  ///< Weight 20. Food and fodder cover, 1 December.
  ReadinessComponent mechanisation;  ///< Weight 20. Horse-backed share of assignment days.
  ReadinessComponent funds;          ///< Weight 15. 100 - mean wear of kolkhoz buildings.
};

/// @brief The four components of the social index in Era I. The chairman's
/// standing (10) is absent by design — its sources are the role-playing
/// lines of Era II — so these four divide by 90.
struct SocialReadiness {
  ReadinessComponent satisfaction;    ///< Weight 35. Mean family satisfaction over the year.
  ReadinessComponent kolkhoz_effort;  ///< Weight 25. Kolkhoz days over kolkhoz plus household.
  ReadinessComponent social_objects;  ///< Weight 20. Built of the era's list.
  ReadinessComponent demography;      ///< Weight 10. Birth rate and the share of children.
};

/// @brief The blocks that stand apart from the weights: while one is unmet
/// the transition does not open however high the indices stand.
///
/// EACH IS A FACT AND NOT A SCORE, so each is a byte and not a float. A
/// blocker averaged into an index would be exactly the compensation the
/// design forbids.
struct TransitionBlocks {
  /// The village ate at least the era's number of food categories in every
  /// one of the four seasons of the year that closed, winter included.
  std::uint8_t food_variety = 0;

  /// Four of the era's six social objects stand.
  std::uint8_t social_objects = 0;

  /// The farm pulls its own work: horses of its own, or a repair base.
  std::uint8_t own_traction = 0;

  /// The wintering closed — food and fodder both reached the spring grass —
  /// in each of the last two years. `wintering_run` below is what it is
  /// read off.
  std::uint8_t wintering_two_years = 0;

  /// Every unit of the era stands at the level the transition asks of it.
  std::uint8_t units_at_level = 0;

  /// The office stands and its wear is at most 1 per cent — "only just
  /// repaired", the design's own words, since nought is unreachable and
  /// wear runs continuously (units rules §11). The host's fact for the same
  /// thing is `office_wear_at_most_1pct`, so the number is the same on both
  /// sides of the seam.
  std::uint8_t office_repaired = 0;
};

/// @brief Readiness as of the last turn of the year.
struct ReadinessState {
  /// The year these numbers were taken for — the year that CLOSED, as
  /// YearLedger::year carries it. 0 before the first turn, which is the
  /// state's own way of saying "never yet computed".
  std::uint16_t year = 0;

  EconomicReadiness economy;

  SocialReadiness society;

  /// The two indices, 0..100, over the components the era has.
  float economic_index = 0.0F;

  float social_index = 0.0F;

  /// Consecutive years, ending with `year`, in which BOTH indices stood at
  /// or above their thresholds. The design's "three years running".
  std::uint8_t both_above_run = 0;

  /// Consecutive years, ending with `year`, whose wintering closed.
  std::uint8_t wintering_run = 0;

  /// The last three years' delivery percentages, newest LAST, and how many
  /// of the three are filled — the plan component is their mean, and the
  /// design says so: «средний процент сдачи за последние три года (за первые
  /// годы — за прожитые)».
  ///
  /// A RING AND NOT A RUNNING MEAN, because "the last three" is not what a
  /// running mean answers: an exponential average never forgets the first
  /// year and never weights the third like the first. And the COUNT beside
  /// it, because a young campaign has fewer than three and a mean over the
  /// unfilled slots would read every new settlement as having failed two
  /// plans it was never given.
  ///
  /// Years the district never spoke of do not enter at all — they are not a
  /// nought, they are not a year of this average (ledger_state.h,
  /// `plan_percent_known`).
  std::array<float, 3> plan_percent_years = {};

  std::uint8_t plan_years_filled = 0;

  TransitionBlocks blocks;

  /// HOW MUCH OF THE SATISFACTION COMPONENT IS A STUB, in points of its
  /// hundred, and it is carried in the state rather than recomputed by every
  /// reader because the design shows the player exact figures with a
  /// breakdown, and this number belongs beside the figure it qualifies.
  ///
  /// Two of satisfaction's four parts — «общее дело» and «нужды» — are drawn
  /// once at genesis and never written again, and both say STUB in their own
  /// header (family_state.h). In Era I they carry 20 and 25 of the metric's
  /// hundred, so 45 points of every family's satisfaction are the same
  /// number in every campaign, and satisfaction can only ever move between
  /// about 25 and 80.
  ///
  /// PRINTED AS A NUMBER AND ALWAYS, never only when it is large (boss,
  /// parcel 132): a mark that appears at a threshold teaches its reader that
  /// its absence means "all honest", and on the day it is forgotten the
  /// absence is read as an answer.
  float satisfaction_stub_points = 0.0F;
};

}  // namespace core

#endif  // CORE_COMMON_READINESS_STATE_H_
