/// @file
/// @brief Two reads for the player's own orders, from the accountant's own plan (#1825; boss 17
///        Nov): what a DRAFT day order would leave the accountant short of, by kind of work
///        (society design: «у учётчика останется недобор: подводы −3»), and how many hands a
///        herd's care needs today by the accountant's own norm (#1811's herd probe: a hand
///        gives less than one care-day, so the seam's ceiling under-sends).
/// @threading SINGLE_THREADED
/// Plain data, built on the sim thread between steps by planning the morning on a copy.
///
/// AN ESTIMATE FROM TODAY: the plan runs on today's state (seams, weather, horses, rest), while
/// an order runs on the next working morning. `measured_on_day` names the day it was planned
/// on; the layer says «оценка на сегодня».

#ifndef CORE_COMMON_DAY_ORDER_PREVIEW_H_
#define CORE_COMMON_DAY_ORDER_PREVIEW_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"

namespace core {

/// @brief One kind of work in the preview: the accountant's hands on it and his jobs of it left
///        short, without the draft and with it.
struct DayOrderKindPreview {
  WorkKind work = WorkKind::kNone;
  std::uint16_t hands_without = 0;
  std::uint16_t hands_with = 0;
  std::uint16_t short_jobs_without = 0;
  std::uint16_t short_jobs_with = 0;
};

/// @brief ISession::PreviewDayOrder's answer: every kind whose numbers the draft changes.
struct DayOrderPreview {
  /// False when nothing could be planned (no draft shape, nobody to place): «not measured».
  bool measured = false;

  /// The day whose state the plan ran on (an estimate of the next working morning).
  SimDay measured_on_day = 0;

  /// The draft's people the plan took out (≤ its N; fewer when fewer were free).
  std::uint16_t taken = 0;

  /// Only the kinds that differ, in WorkKind order.
  std::vector<DayOrderKindPreview> kinds;
};

/// @brief ISession::HerdCareToday's answer: a herd's care by the accountant's own numbers.
struct HerdCareView {
  HerdId herd;

  /// False for a herd with no care job today (a family's, none left, no place): «not measured».
  bool measured = false;

  /// Game care-days left today (HerdRow::care_days_remaining).
  float seam = 0.0F;

  /// What one hand is expected to give today on this herd, care-days - the accountant's own
  /// ExpectedNormPerHand over the hands he may send (the light after the round trip, by their
  /// efficiency, over the standard day). 0 when nobody can go.
  float per_hand = 0.0F;

  /// ceil(seam / per_hand): the crew that covers it by the accountant's expectation.
  std::uint16_t hands = 0;
};

}  // namespace core

#endif  // CORE_COMMON_DAY_ORDER_PREVIEW_H_
