/// @file
/// @brief The accountant's whole morning as a bot reads it (#1811; boss 17 Nov): every job he
///        would plan, in his order, with its seam and the hands his plan puts on it. Its own header
///        since 0.37.392 (host's ask: a proxy built against several cores tests for the door by
///        __has_include of a header that first appears with it).
/// @threading SINGLE_THREADED
/// Plain data, built on the sim thread between steps by planning the morning on a copy.

#ifndef CORE_COMMON_CLERK_AGENDA_H_
#define CORE_COMMON_CLERK_AGENDA_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"

namespace core {

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

/// @brief ISession::ClerkAgenda's answer: the accountant's whole morning on a copy of today, so
///        a bot can follow his list by day orders. A READ FOR THE BOTS ONLY: society design gives
///        the player no hands number. Named «View» so it does not collide with the session's
///        ClerkAgenda() in the session's own scope.
struct ClerkAgendaView {
  /// False when the morning has no jobs or no free hands.
  bool measured = false;
  SimDay measured_on_day = 0;
  /// In rank order.
  std::vector<ClerkAgendaLine> lines;
};

}  // namespace core

#endif  // CORE_COMMON_CLERK_AGENDA_H_
