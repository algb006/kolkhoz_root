/// @file
/// @brief Who gets a roof: the free house first, and the STUB that raises
/// one when there is none.
/// @threading SINGLE_THREADED
/// Runs in the sequential decisions phase, from the sim thread; it appends
/// unit rows, which only a sequential phase may do.
///
/// WHY HOUSING IS ITS OWN FILE AND NOT PART OF DEMOGRAPHY. It is the half
/// of the structural work that answers to a different design document
/// (life-cycle §12 and the unit rules, rather than the demography curves),
/// and it is the half that reaches OUT of this module — into the units
/// table and the plot rule that core_construction applies to the same
/// table. A wedding that cannot find a house is a demography question; a
/// house that cannot find a place is not.
///
/// It also earned the separation the hard way: putting a house where a
/// house already stands went unnoticed for the whole of phase 1, because
/// the rule it broke was guarded at a door this code does not come through
/// (core_common/plot.h).

#ifndef CORE_RESIDENTS_HOUSING_H_
#define CORE_RESIDENTS_HOUSING_H_

#include "core_common/family_state.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "life_config.h"

namespace core {

/// @brief The house a new household moves into: a FREE one if the village
/// has it, a STUB one raised on the spot otherwise.
/// @param groom_family,bride_family Whose parents' yard to build beside;
///        either may be invalid, and a couple with neither settles amid the
///        village. The house is never placed ON the wanted spot without
///        asking the plot rule — that spot is by definition one somebody
///        already lives on.
/// @return The unit the household is to live in. Never invalid: a wedding
///         may not fall through for want of a house (life-cycle §12).
/// @note Appends a unit row, so no reference into `current.units` survives
///       this call.
UnitId SettleHouse(const LifeConfig& config,
                   WorldState& current,
                   FamilyId groom_family,
                   FamilyId bride_family);

/// @brief Gives a roof to every family whose house is gone — the start's
/// old houses fall at the top of the wear scale, and the construction
/// sub-step cannot rehouse anyone itself.
/// @note Runs FIRST in the day's structural work: a day of homelessness is
///       the design's answer for a fallen house, a second one would be a
///       family with nowhere for its day to start.
void Rehouse(const LifeConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_RESIDENTS_HOUSING_H_
