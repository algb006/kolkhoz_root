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
  kUnderstoodReasonCount,
};

/// @brief The rules' numbers, all STUB (world_params `authority_*`; metrics §6's tables).
struct AuthorityRules {
  /// Each deed's move, points of A (index: AuthorityDeed).
  std::array<float, kAuthorityDeedCountValue> deed_delta = {
      -4.0F, -2.0F, -2.0F, 4.0F, -4.0F, 2.0F, -2.0F, -3.0F};
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
};

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

/// @brief The once-a-season complaint upward (phase 6): below `complaint_below` and not yet
///        this season - raikom by `complaint_reputation` (booked as a loss), an extraordinary
///        visit next day (kComplaint) and EventKind::kComplaintToDistrict.
void RunAuthorityComplaint(WorldState& world, const AuthorityRules& rules);

}  // namespace core

#endif  // CORE_COMMON_AUTHORITY_H_
