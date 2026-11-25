/// @file
/// @brief THE CHAIRMAN'S AUTHORITY IN EPOCH I (#2164; metrics §6 «Авторитет в Эпохе I»; econ's
///        authority-epoch1-2161 §1-§5a, e7b13a7; Alex 25 Nov «Авторитет это важная механика и
///        надо делать сразу», #2430, «Черновик делай», #2441). CONTRACT ONLY (core §12а): the
///        types, the rules' numbers and the doors; no body yet.
///
/// THE SCALE. ChairmanState::authority, 0..100, the whole village's trust. It starts by the
/// avatar alone (avatars.csv `start_authority`, no random), moves ONLY by loud deeds - the
/// chairman's decisions the whole village sees (AuthorityDeed) - and never fades.
/// While it is below `catch_up_below` (30), a deed's PLUS counts ×`catch_up_factor` (1.5);
/// a minus counts as it is.
///
/// WHAT IT DOES IN EPOCH I, each with a decision behind it:
///   * THE UNDERSTANDING: a season's (the core's 3 months, 12 days) budget of understood days -
///     0 below 50, 1 at 50-74, 2 from 75 - spent by a cancelled day off or a rush day that has
///     a reason the people accept (UnderstoodReasonToday). An understood day costs no
///     satisfaction and no authority (the rest still does not recover). Without a reason, or
///     past the budget: the full cost and AuthorityDeed::kDayOffWithoutReason. No carry-over.
///   * THEFT: the night thief's kilograms × TheftAuthorityFactor (1 − (A − 50) × 0.015,
///     clamped 0.25..1.75) - A = 50 changes nothing (boss: the CENTRED form).
///   * COMPLAINT UPWARD: below `complaint_below` (30), once a season, a complaint to the
///     district - raikom −2 and an extraordinary visit next day (DistrictVisitCause::kComplaint).
///   * THE SUM e = clamp(p + (A − 50), 0, 100), p = ResidentRow::attitude_to_chairman: the
///     residents' tone and the headman read it (EffectiveAttitudeOf), never output.
///
/// STUBS, NAMED: a holiday declared a working day (−4 / −2, the halving at high A) - the core
/// has no working-holiday door; the Epoch I→II assembly - the build has none (refusal +1 pp a
/// point of A below 50 when it returns); Дисциплина's theft factor (1 − 0.2 × step/top) - the
/// hidden scales are #2167; promises and the chairman's house - no door in Epoch I.
/// @threading SINGLE_THREADED
/// Written in phase 3 (decisions) and 6 (events); read by phase 2 and 4 readers as plain data.

#ifndef CORE_COMMON_AUTHORITY_H_
#define CORE_COMMON_AUTHORITY_H_

#include <array>
#include <cstdint>

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

struct WorldState;

/// @brief A loud deed: a chairman's decision the whole village sees. Values are appended
///        before the count and never renumbered (seam word `authority_deed`).
enum class AuthorityDeed : std::uint8_t {
  kHolidayWorked = 0,     ///< STUB: a holiday declared working (no door in the core).
  kHolidayHalfWorked,     ///< STUB: the same till noon.
  kDayOffWithoutReason,   ///< A day off cancelled (or a rush day) with no reason or no budget.
  kDefendedFoodPosition,  ///< In a hunger season, a plan bargain that LOWERED a food position.
  kOverPlanAtRationLine,  ///< Delivered over the plan while families stand at the ration line.
  kRaisedIssueAtLine,     ///< Raised the issue norm or opened a reserve in such a month.
  kUnfairIssueMonth,      ///< A month of families under the line, food in store, nothing done.
  kAuditRemark,           ///< The district's audit remarked on the stores (#1893).
  kPlanMetFairly,         ///< The plan year met with no rush and no cancelled day off (econ [8]).
  kFirstSocialBuilding,   ///< The first of a social kind stands (bath, school, culture, nursery).
  kAuthorityDeedCount,
};

inline constexpr std::size_t kAuthorityDeedCountValue =
    static_cast<std::size_t>(AuthorityDeed::kAuthorityDeedCount);

/// @brief Why an understood day is understood (UnderstoodReasonToday). Seam word
///        `understood_reason`; appended before the count.
enum class UnderstoodReason : std::uint8_t {
  kNone = 0,      ///< No reason the people accept today.
  kWeather,       ///< A harvest or hay lamp is burning (the weather presses).
  kPlanDeadline,  ///< The plan's deadline within 4 game days, the plan short.
  kEmergency,     ///< A fire, a flood or a herd loss today or yesterday.
  kSowingWindow,  ///< The sowing window closing: a field ploughed, harrowed or sown (econ [7]).
  kUnderstoodReasonCount,
};

/// @brief The rules' numbers, all STUB (world_params `authority_*`; metrics §6's tables).
struct AuthorityRules {
  /// Each deed's move, points of A (index: AuthorityDeed).
  std::array<float, kAuthorityDeedCountValue> deed_delta = {
      -4.0F, -2.0F, -2.0F, 4.0F, -4.0F, 2.0F, -2.0F, -3.0F, 2.0F, 2.0F};
  float catch_up_below = 30.0F;
  float catch_up_factor = 1.5F;
  /// The understanding budget's edges: 0 below the first, 1 to the second, 2 from it.
  float understanding_one_from = 50.0F;
  float understanding_two_from = 75.0F;
  /// The theft factor: 1 − (A − 50) × slope, clamped to [low, high].
  float theft_slope = 0.015F;
  float theft_factor_low = 0.25F;
  float theft_factor_high = 1.75F;
  /// The complaint upward: below this A, once a season, the raikom moves by this.
  float complaint_below = 30.0F;
  float complaint_reputation = -2.0F;
  /// The plan's deadline reason: within this many days of the turn with the plan short.
  std::uint32_t deadline_days = 4;
  /// The issue gate (econ [6]): a family is short below this share of its month's need, and
  /// the row fires from this share of families short.
  float short_eaten_share = 0.9F;
  float short_share = 0.10F;
  /// A shortfall's kilocalories in grain-equivalent grams (food.csv's grain, 3.3 kcal a gram).
  float grain_kcal_per_gram = 3.3F;
};

/// @brief The rules every reader uses: econ's STUB numbers, one home. world_params for them come
///        when a balance pair asks to move one (only `authority_live` is a key in 232).
inline const AuthorityRules kAuthorityRules{};

/// @brief Moves A by `deed` (with the catch-up below `catch_up_below`), clamps to 0..100, and
///        emits EventKind::kAuthorityMoved (amount: the deed in bits 0-7, the move in tenths
///        of a point, signed, in bits 8-31). STUB rows (the holiday) are refused by shape.
void MoveAuthority(WorldState& world, const AuthorityRules& rules, AuthorityDeed deed);

/// @brief A season's budget of understood days at authority `authority` (0, 1 or 2).
std::uint8_t UnderstandingBudget(const AuthorityRules& rules, float authority);

/// @brief The understood days still left this season (the budget less those spent this
///        season, ChairmanState::understood_season / understood_used).
std::uint8_t UnderstoodDaysLeft(const WorldState& world, const AuthorityRules& rules);

/// @brief Whether today has a reason the people accept, and which (first found, in the
///        enum's order). Read off live state only: the lamps, the plan, the day's events.
UnderstoodReason UnderstoodReasonToday(const WorldState& world, const AuthorityRules& rules);

/// @brief Spends one understood day if today has a reason and the season a budget; returns
///        whether it did. Called where a cancelled day off or a rush day books its cost.
bool SpendUnderstoodDay(WorldState& world, const AuthorityRules& rules);

/// @brief The night thief's kilograms are multiplied by this: 1 − (A − 50) × slope, clamped.
///        Exactly 1 at A = 50.
float TheftAuthorityFactor(const AuthorityRules& rules, float authority);

/// @brief A resident's effective attitude: clamp(p + (A − 50), 0, 100).
float EffectiveAttitude(float personal, float authority);

/// @brief Whether any family stands at the ration line today (FamilyRow::hunger_alarm_lit):
///        the core's «hunger season» for the plan and issue deeds (econ §5a 4; the yellow
///        forecast lamp is the session's and not read inside a step).
bool FamiliesAtRationLine(const WorldState& world);

/// @brief The issue's hungry share (econ [6]): the families that ate less than
///        `short_eaten_share` of their need over the month so far (FamilyRow::month_*), their
///        share of all families with a need, and their shortfall to the full need, kcal.
struct IssueShortfall {
  std::uint32_t short_families = 0;
  std::uint32_t families = 0;
  float share = 0.0F;
  float shortfall_kcal = 0.0F;
};

IssueShortfall IssueShortfallOf(const WorldState& world, const AuthorityRules& rules);

/// @brief The issue gate's first half: at least `short_share` of the families short.
bool FamiliesShort(const IssueShortfall& shortfall, const AuthorityRules& rules);

/// @brief The chairman raised a food issue norm or opened a food reserve. The month is marked
///        (issue_raised_month: it is not judged unfair); with the families short (the gate's
///        first half; the free stock is not read here - spending food is not a pump), once a
///        season, AuthorityDeed::kRaisedIssueAtLine.
void SayIssueRaised(WorldState& world, const AuthorityRules& rules);

/// @brief The month's judgement, on its last day: the families short, the free stock over the
///        funds (`free_grain_eq_grams`, the caller's FreeStock read) at least their shortfall in
///        grain equivalent, no raise this month - and once a season - AuthorityDeed::
///        kUnfairIssueMonth. A no-op on any other day.
void JudgeIssueMonth(WorldState& world, const AuthorityRules& rules, Grams free_grain_eq_grams);

/// @brief A hard ask of the year: a cancelled day off or a rush declared. A plan year with
///        none is met «fairly» (SayPlanYearMet).
void CountHardAsk(WorldState& world);

/// @brief The plan year judged met (district_plan.cpp JudgePlan): with no hard ask this year,
///        AuthorityDeed::kPlanMetFairly; the year's count is reset either way (econ [8]).
void SayPlanYearMet(WorldState& world, const AuthorityRules& rules, bool met);

/// @brief A unit finished building whose type is one of the first-social kinds (`kind` its
///        index, 0..3): the first of a kind is AuthorityDeed::kFirstSocialBuilding, once.
void SaySocialBuilt(WorldState& world, const AuthorityRules& rules, std::uint32_t kind);

/// @brief The season's mark for the complaint (daily): on a new season, whether A fell over
///        the one just ended is remembered (authority_fell) and the new season's start A
///        stored. Returns true on the day a season turns.
bool MarkAuthoritySeason(WorldState& world);

/// @brief Whether today's cancelled day off is understood (SpendUnderstoodDay spent today).
bool UnderstoodToday(const WorldState& world);

/// @brief The talk season of `day`, plus one (the once-a-season index: the core's 3-month
///        season, counted across years; the formula of core_residents' TalkSeasonOf).
std::uint32_t AuthoritySeasonOf(std::uint32_t day);

// The once-a-season complaint upward (RunAuthorityComplaint) needs the district's doors - the
// reputation's loss book and the extraordinary visit - so it lives in core_production
// (district_visit.h), not here.

}  // namespace core

#endif  // CORE_COMMON_AUTHORITY_H_
