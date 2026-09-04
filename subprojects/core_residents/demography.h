/// @file
/// @brief One day of the settlement's STRUCTURAL work: who dies, who
/// leaves, who is born, who marries, who arrives — and who gets a roof.
/// @threading SINGLE_THREADED
/// Runs in the sequential decisions phase, from the sim thread. It changes
/// the SHAPE of the world — rows appear and disappear — which is why it can
/// live nowhere else: a parallel phase may fill a row, never add one.
///
/// WHY IT IS ITS OWN FILE. residents_system.cpp reached 979 lines of a
/// thousand (task A6). The seam is the one the module already has: the
/// per-day needs of people who exist, worked in parallel by family, against
/// the structural work that decides which people exist at all, worked
/// sequentially. They share the configuration and nothing else.
///
/// THE SPLIT ADDED NO SECOND HOME — it removed one. `BiologicalAgeYears`
/// and `EpochIndex` were already written twice inside this module, in
/// residents_system.cpp and in family_meal.cpp, and a third copy here was
/// exactly what boss forbade (2026-09-04): a fact needed by both halves
/// MOVES to one of them and is offered as a projection. Both now live in
/// life_config.h, beside the configuration they are derived from.

#ifndef CORE_RESIDENTS_DEMOGRAPHY_H_
#define CORE_RESIDENTS_DEMOGRAPHY_H_

#include "core_common/world_state.h"
#include "life_config.h"

namespace core {

/// @brief The whole day's structural work, in the order the design fixes.
///
/// The order is not an implementation detail. Rehousing comes FIRST because
/// whoever lost a roof overnight — the construction sub-step lets the
/// start's old houses fall at the top of the wear scale — must have one
/// before anything else asks where his day starts from. A day of
/// homelessness is the design's own answer for a fallen house; a second one
/// would be a family with nowhere to begin.
///
/// @param config The life configuration; not written.
/// @param current The world being built for today; rows are added and
///        removed, so no reference into its tables survives this call.
void RunDemographyDay(const LifeConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_DEMOGRAPHY_H_
