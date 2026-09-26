/// @file
/// @brief The road network as the layer draws it (Roads(), delivery 7a;
///        road_draft.h RoadView), read off the road table as it stands.
/// @threading PARALLEL_READONLY
/// Outside every phase of the step: read between steps by the session's door,
/// on the completed world. A pure function of the table it is given; the
/// views it returns are values the caller owns.

#ifndef CORE_COMMON_ROAD_VIEW_H_
#define CORE_COMMON_ROAD_VIEW_H_

#include <vector>

#include "core_common/road_draft.h"
#include "core_common/road_state.h"

namespace core {

/// @brief One view per road of `roads`, in the table's row order: its kind,
///        surface, origin and removability, the axis with each point's
///        running length along it and its mark, and the stretches' wear.
/// @return No work on any view: road work comes with delivery 7e, and until
///         then `works` is empty on every road (it is never guessed).
std::vector<RoadView> RoadViews(const RoadTable& roads);

}  // namespace core

#endif  // CORE_COMMON_ROAD_VIEW_H_
