/// @file
/// @brief The issue norms in force and what stands behind them: the office's
///        answer to «сколько чего за трудодень и почему», and the village's
///        need until a position's next harvest (labor-payment §7, «Норма по
///        умолчанию — доля остатка»; register 302; 0.37.28).
/// @threading SINGLE_THREADED
/// Plain data, computed from the completed world between steps; nothing here
/// is state of the world. WorldState::issue_norms holds only what the
/// chairman decided (and a marker for "the default rule"); these views carry
/// the norm that is ISSUED, so no consumer has to repeat the rule.
#ifndef CORE_COMMON_ISSUE_NORM_VIEW_H_
#define CORE_COMMON_ISSUE_NORM_VIEW_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief Why a position of the bundle is issued at the norm it is.
/// Appended, never renumbered.
enum class IssueNormBasis : std::uint8_t {
  /// The default rule for a position a field's harvest gives: the free
  /// remainder over the funds, divided among the trudodni the village is
  /// forecast to earn until that harvest (and those earned but not issued).
  kShareOfRemainder = 0,

  /// The default rule for a position no harvest gives (milk, daily): the
  /// table's grams, food.csv `issue_kg_per_trudoden`.
  kTableGrams,

  /// The chairman's norm (kSetIssueNorm), until kResetIssueNorm.
  kChairman,

  /// NOT A BASIS: the count, for a consumer's mirror.
  kIssueNormBasisCount,
};

/// @brief One position of the bundle as the next distribution issues it.
struct IssueNormLine {
  /// The position (a food resource of food.csv's bundle).
  ResourceId resource;

  /// The norm in force, grams per trudoden. Nought: the position is not
  /// issued — struck out by the chairman, nought in the table, or nothing
  /// free to share.
  Grams grams_per_trudoden = 0;

  IssueNormBasis basis = IssueNormBasis::kShareOfRemainder;

  /// kShareOfRemainder only — the two halves of the division, so the norm
  /// can be checked and a zero told apart from an empty store:
  /// what is free to share (the stock over the funds, times the table's
  /// issue share), grams. A position with a SUBSTITUTE in its category
  /// (vegetables and sauerkraut) counts the substitute's free stock in its
  /// own — summing the lines counts it twice; the substitute's line shows
  /// its own stock, and its norm is the one it covers the shorter ones at,
  /// not its stock over its trudodni. The norm is capped at
  /// kMaxIssueNormGrams...
  Grams free_grams = 0;

  /// ...and the trudodni it is shared among: earned and not yet issued,
  /// plus the forecast to the harvest. Nought on other bases.
  float trudodni = 0.0F;

  /// Whole game days to the position's next harvest the fields will give
  /// (IProductionSystem::DaysToHarvestOf: a crop standing in a field, or
  /// the one after its next sowing) — during a standing crop's window, next
  /// year's; -1 for a position no harvest gives.
  /// Answered on every basis, the chairman's included.
  std::int32_t days_to_harvest = -1;

  /// True when the forecast had no last year to read (the first year) and
  /// stood on the fallback — a norm to read with that in mind.
  bool forecast_fallback = false;
};

/// @brief The village's need until a position's next harvest against what
/// the position holds free (econ's acceptance of register 302: hunger
/// counted only in years when the stock exceeded the need). In kilocalories,
/// the unit hunger is counted in: the core keeps no per-position eating norm
/// — a family's need is a calorie need its positions cover by category.
struct HarvestNeed {
  ResourceId resource;

  /// As IssueNormLine::days_to_harvest; -1 for a position no harvest gives.
  std::int32_t days_to_harvest = -1;

  /// The settlement's daily need, kilocalories: every resident at home, by
  /// age, as the meal counts it (no heavy-work surcharge — a forecast does
  /// not know who will work).
  float village_need_kcal_per_day = 0.0F;

  /// The position's free remainder over the funds, grams and kilocalories.
  Grams free_grams = 0;
  float free_kcal = 0.0F;
};

}  // namespace core

#endif  // CORE_COMMON_ISSUE_NORM_VIEW_H_
