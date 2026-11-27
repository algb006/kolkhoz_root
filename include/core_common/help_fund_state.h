/// @file
/// @brief #2199 «ФОНД ПОМОЩИ» (econ's help-fund-low-earners-2026-11-26 §1 and his yeses of 27
///        Nov; Alex #2457): the kolkhoz's help fund - a share of the year's grain and potato
///        harvest set aside at the turn, from which the clerk tops every family that ate below
///        the line last month up to it, automatically, until the fund is empty.
/// @threading SINGLE_THREADED
/// Written by core_residents (help_fund.h) in the decisions sub-step (phase 3) on the sim
/// thread: the turn's top-up of the fund, the month's issue from it, the share's order. SAVED
/// (save 236).
///
/// WHAT IT IS, by econ's rules (all STUB): not a loan, not trudodni - a book line «из фонда
/// помощи» (YearLedger::help_issued); topped to the line (0.9), not to 1.0, so the reason to
/// earn stays. The fund is SEALED like the other funds: the issue, the ration and the night
/// theft stay above it (IssueReserve). At each turn it is TOPPED UP to its share of the closed
/// year's harvest, not added to - the leftover counts toward the new share (econ). Year 1 has no
/// fund (nothing closed yet): its hunger is the issue raise's. Past year 15 the 2 % runs dry in
/// 86 % of village-years (HELPFUND2199) - an Epoch I door; Epoch II's hunger is a return
/// condition (a larger share or the ration's fix), not this build.
#ifndef CORE_COMMON_HELP_FUND_STATE_H_
#define CORE_COMMON_HELP_FUND_STATE_H_

#include <array>
#include <cstdint>

#include "core_common/quantities.h"

namespace core {

/// @brief The fund's numbers (econ's STUB).
struct HelpFundRules {
  /// The share's default and ceiling, per mille of the year's grain and potato harvest.
  std::uint16_t default_permille = 20;
  std::uint16_t max_permille = 30;
  /// The line a family is topped up to: its month's eaten over its month's need.
  float line = 0.9F;
  /// The hour of the year's first day the fund is topped up: after the ledger's turn (hour 0's
  /// end), so the closed book is the year that ended.
  std::uint32_t turn_hour = 1;
};

inline constexpr HelpFundRules kHelpFundRules{};

/// @brief The help fund. SAVED (save 236).
struct HelpFundState {
  /// The chairman's share, per mille (kSetHelpFundShare), 0..max_permille.
  std::uint16_t share_permille = kHelpFundRules.default_permille;
  /// What the fund holds, grams by ResourceId (grain and potato only): drawn by the month's
  /// issue, topped up at the turn and when the share is raised.
  ResourceAmounts held;
  /// 0/1: kHelpFundEmpty has been said since the last top-up.
  std::uint8_t empty_said = 0;
  /// PROBE #2199b (not for release, not saved): the free stock above the lower rungs of the
  /// fund's foods at the month's issue, grams.
  Grams probe_free_g = 0;
  /// PROBE #2199b: over the families short last month at the issue - their count, the pantry
  /// term's kcal by kind (0 fodder/hay, 1 winter store, 2 potato, 3 vegetables, 4 grain,
  /// 5 other), the line's kcal, the kcal given, and how many got nothing.
  std::uint32_t probe_short = 0;
  std::uint32_t probe_none = 0;
  std::array<double, 6> probe_term_kcal{};
  double probe_line_kcal = 0.0;
  double probe_given_kcal = 0.0;
};

}  // namespace core

#endif  // CORE_COMMON_HELP_FUND_STATE_H_
