/// @file
/// @brief The runs of working days each job of the village stood with no hand
/// on it (task 427, item 2: kWorkGotNoHands; host's feed, host-boss-event-
/// feed-reading-2026-10-10 [10]; boss, core-boss-feed-events-2026-10-10 [2]).
/// @threading SINGLE_THREADED
/// Written by labour's morning allocation (phase 3) on the sim thread, read
/// nowhere else. SAVED (save 149): a run must survive a load, or a save on
/// the third morning would hide the fourth's word.
///
/// A JOB IS THE VILLAGE'S STANDING WORK, NOT AN ORDER'S: it has no id of its
/// own, so a run is keyed by the job's kind and its subject — the ids the
/// allocator's job carries (labor's AssignmentJob). A job that is not in the
/// morning's list any more takes its run with it; a day off neither counts
/// nor breaks a run.

#ifndef CORE_COMMON_UNMANNED_RUN_STATE_H_
#define CORE_COMMON_UNMANNED_RUN_STATE_H_

#include <cstdint>
#include <vector>

#include "core_common/ids.h"
#include "core_common/labor_state.h"

namespace core {

/// @brief One job's run of working mornings with no hand on it.
struct UnmannedRunRow {
  WorkKind kind = WorkKind::kNone;

  /// The job's subject, as the allocator names it; unnamed ids invalid.
  FieldId field;
  HerdId herd;
  UnitId unit;
  TimberStandId stand;
  ExtractionSiteId extraction_site;
  LimitDeliveryId limit_delivery;
  RoadWorkId road_work;

  /// Working mornings in a row the job stood with work left and nobody on it.
  std::uint16_t days = 0;

  /// 1 once kWorkGotNoHands was said for this run.
  std::uint8_t said = 0;
};

/// The runs standing this morning, in the order they began.
using UnmannedRuns = std::vector<UnmannedRunRow>;

/// The working mornings with no hand after which kWorkGotNoHands is said, once.
inline constexpr std::uint16_t kWorkGotNoHandsDays = 4;

}  // namespace core

#endif  // CORE_COMMON_UNMANNED_RUN_STATE_H_
