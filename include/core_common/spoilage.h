/// @file
/// @brief How stored goods go bad: the one rule, for every place goods lie.
/// @threading SINGLE_THREADED
/// The file holds no state of its own, but SpoiledToday is the only PURE
/// function here: **SpoilAmounts WRITES THROUGH BOTH ITS REFERENCES** — it
/// empties the pile and appends to the loss column — and the loss column its
/// two callers pass is the SAME one, `ledger.current.spoiled`, which it may
/// resize. Called from a parallel phase that would be a data race and a
/// concurrent reallocation at once.
///
/// So the label says SINGLE_THREADED and means it: both callers rot their
/// own rows inside the sequential decisions slot (phase 3), production at
/// the day's last tick and residents at the next day's first. A label of
/// PARALLEL_READONLY here would have been a licence to do the wrong thing,
/// and it was the one this file carried until the A4 delivery cycle read it
/// against the code.
///
/// Shared by core_production (the units' stores) and core_residents (the
/// families' larders) for the reason haul.h is shared: milk in a granary and
/// milk in a cellar rot at the same rate, and a rule with two homes grows
/// two answers.
///
/// Model: transport design §10 (shelf life and spoilage); task A4. The
/// shelf life of each resource is a column of resources.csv — `spoil_days`,
/// in GAME days, empty for what does not go bad at all.
///
/// WHEN, and it matters more than the numbers (boss, 2026-09-03): rotting
/// happens at the END of the day, AFTER the village has eaten. Eaten food
/// cannot rot. The other way round and the settlement starves beside a full
/// store — and both halves of that look correct while you hunt for it.

#ifndef CORE_COMMON_SPOILAGE_H_
#define CORE_COMMON_SPOILAGE_H_

#include <cstdint>
#include <vector>

#include "core_common/quantities.h"

namespace core {

/// @brief What a day takes from a pile that keeps `spoil_days` game days.
///
/// Exponential and not linear, and the difference is a design decision
/// rather than arithmetic taste: a linear loss would need to know HOW OLD
/// each gram is, which is a field on every stored amount and a save format
/// that carries the age of every potato. A share of what is there needs no
/// memory at all — the pile that has stood longest is simply the smallest.
///
/// @param held Grams in the pile.
/// @param spoil_days Shelf life in game days; 0 or less means it does not
///        go bad, and nothing is taken.
/// @param keeping_factor Multiplier on the shelf life: a cellar, an ice
///        house, a frost, a jar. STUB at 1.0 everywhere — none of them
///        exists in the vertical slice yet, and the parameter is here so
///        that the day one does, there is a place to put it rather than a
///        formula to find.
/// @return Grams lost today, never more than `held`.
Grams SpoiledToday(Grams held, float spoil_days, float keeping_factor);

/// @brief Rots a whole store or larder in place and adds what was lost to
/// `lost`, position by position, so the caller can book it.
/// @param amounts The pile, dense by ResourceId; shortened vectors are fine.
/// @param spoil_days Per resource, by ResourceId; entries past its end keep.
/// @param keeping_factor See above; STUB at 1.0.
void SpoilAmounts(ResourceAmounts& amounts,
                  const std::vector<float>& spoil_days,
                  float keeping_factor,
                  ResourceAmounts& lost);

}  // namespace core

#endif  // CORE_COMMON_SPOILAGE_H_
