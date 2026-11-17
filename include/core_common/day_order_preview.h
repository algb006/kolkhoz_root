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

/// @brief One resident in the accountant's ranking for a job (ClerkPick, ISession::ClerkPicksFor).
struct ClerkPick {
  ResidentId resident;
  /// Norm-days he would deliver on the job today, by his walk, light and efficiency.
  float daily_norm = 0.0F;
};

/// @brief ISession::ClerkPicksFor's answer (#1811's herd trace: the walk decides a herder's
///        day): the residents the accountant would rank first for a job this morning, best first,
///        as many as `n` asked, each with his expected output - a read for the bots. NOT the
///        player's «Расставить самому» names: those come from the chairman's own placement level
///        (society design «Наряд игрока»; boss 17 Nov).
struct ClerkPicks {
  /// False when the target names no job open this morning, or nobody can reach it.
  bool measured = false;
  SimDay measured_on_day = 0;
  /// The job's work left this morning (its seam), game man-days.
  float seam = 0.0F;
  std::vector<ClerkPick> picks;
};

/// @brief One job of the accountant's morning (ClerkAgendaView): what, where, his rank, its seam,
///        the hands he plans on it, and whether its window has closed.
struct ClerkAgendaLine {
  WorkKind work = WorkKind::kNone;
  /// The target, as the job names it: one of these is valid for the kind (the others invalid).
  FieldId field;
  HerdId herd;
  UnitId unit;
  TimberStandId stand;
  ExtractionSiteId extraction_site;
  /// The accountant's order: 0 is the job he fills first (assignment.h, PlanningOrder - the
  /// placement's own tiers: open windows by days, then the overdue, then the windowless).
  std::uint16_t rank = 0;
  /// The job's work left this morning, game man-days.
  float seam = 0.0F;
  /// The hands his plan puts on it this morning.
  std::uint16_t hands = 0;
  /// The job's window closed (core_common/deadline.h kOverdue); false for a windowless job.
  bool overdue = false;
};

/// @brief ISession::ClerkAgenda's answer (#1811; boss 17 Nov): the accountant's whole morning
///        on a copy of today - every job he would plan, in his order, with his hands - so a bot
///        can follow the accountant's list by day orders. A READ FOR THE BOTS ONLY: society
///        design gives the player no hands number. Named «View» so it does not collide with the
///        session's ClerkAgenda() in the session's own scope.
struct ClerkAgendaView {
  /// False when the morning has no jobs or no free hands.
  bool measured = false;
  SimDay measured_on_day = 0;
  /// In rank order.
  std::vector<ClerkAgendaLine> lines;
};

}  // namespace core

#endif  // CORE_COMMON_DAY_ORDER_PREVIEW_H_
