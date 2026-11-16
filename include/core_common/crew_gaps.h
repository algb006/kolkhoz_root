/// @file
/// @brief The morning's jobs left short of hands (#1789; layers design §7 «Нехватка рук и
///        простой» - the badge over a building or a field): which job, how many the placement
///        put on it, and the reason it stopped filling.
/// @threading SINGLE_THREADED
/// Written by core_labor's morning placement (phase 3) from its own diagnosis
/// (assignment.h, PlacementDiagnosis::shortfall); read between steps (ISession::CrewGapsToday).
///
/// WHY A REASON AND NOT «WANTED»: the placement does not hold a wanted number - it fills a job
/// until the crew's expected output covers the day's demand, its brigade is full, or nobody is
/// left - so the honest pair is the hands it placed and the rule that stopped it. «Простой» is
/// the layer's from WorkbookLine::idle with its reasons (boss, core-boss-1789-layers-desk [2]).

#ifndef CORE_COMMON_CREW_GAPS_H_
#define CORE_COMMON_CREW_GAPS_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/labor_state.h"

namespace core {

/// @brief One job this morning's placement left short (WorldState::crew_gaps; saved).
struct CrewGapLine {
  /// The work and its place: exactly one target valid, as on AssignmentJob.
  WorkKind work = WorkKind::kNone;
  FieldId field;
  HerdId herd;
  UnitId unit;
  TimberStandId stand;
  ExtractionSiteId extraction_site;
  RoadWorkId road_work;

  /// The hands the morning placed on it (0: nobody).
  std::uint16_t placed = 0;

  /// Why it stopped filling: no horse, its brigade full, nobody free and fit, or the road.
  JobShortfall shortfall = JobShortfall::kNoHands;
};

}  // namespace core

#endif  // CORE_COMMON_CREW_GAPS_H_
