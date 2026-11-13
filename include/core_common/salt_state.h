/// @file
/// @brief The village's salt watch (#1630 (C); save 209): the run that is «Без
/// соли»'s condition and its latch, the district's letter and the loan. Plain
/// data in WorldState::salt.
/// @threading SINGLE_THREADED
/// Written by core_residents (the run, the latch; salt_issue.cpp) and by
/// core_production (the letter, the loan; salt_loan.cpp), each its own fields,
/// in the decisions slot.
///
/// THE DESIGN (epochs §8 «Без соли»; health §4; econ's salt-ending-c §2-§5):
/// the ending's CONDITION is at least salt_edge_village_share of the families
/// on step 4 «край» salt_edge_series_days running, raised once as
/// kSaltEndingDue; the district offers a loan by letter at a year's first
/// yellow lamp while the points do not buy the lot; the loan is one salt lot
/// paid off next year's base fund by itself, once a year.

#ifndef CORE_COMMON_SALT_STATE_H_
#define CORE_COMMON_SALT_STATE_H_

#include <cstdint>

namespace core {

struct SaltState {
  /// Days running that at least the share of the families stood on step 4;
  /// 0 the day the share falls under it. Judged before the day's departures.
  std::uint16_t half_on_edge_days = 0;

  /// 0/1: kSaltEndingDue has been raised (once a campaign).
  std::uint8_t ending_raised = 0;

  /// The game year (1-based) of the district's last loan letter; 0 none.
  std::uint16_t letter_year = 0;

  /// The game year (1-based) of the last salt loan taken; 0 none - the rule
  /// «once a year».
  std::uint16_t loan_year = 0;

  /// Points owed to the district for salt loans, taken off the next year's
  /// grant at the turn (TurnLimitYear), then 0.
  std::int32_t loan_points_owed = 0;
};

}  // namespace core

#endif  // CORE_COMMON_SALT_STATE_H_
