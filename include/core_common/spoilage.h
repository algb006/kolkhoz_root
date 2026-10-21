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

#include <array>
#include <cstdint>
#include <vector>

#include "core_common/ids.h"
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

/// @brief A field heap's shelf in game days: `spoil_days` x the heap's
///        keeping factor, or infinity when the resource keeps for ever (0).
///
/// ONE HOME for two doors (#1099): logistics orders its heaps by it (#1056
/// C4.2, ShelfDaysOf) and labour weighs a rotting heap's shelf against a
/// standing crop's snow (MarkTheUrgentLoads). Each reads the same two cells
/// (resources.csv spoil_days, world_params field_heap_keeping_factor).
float FieldHeapShelfDays(float spoil_days, float keeping_factor);

/// @brief The season whose share of loose hay weathers in month `month`
///        (calendar month index 0..11): 0 summer, 1 autumn, 2 winter, 3
///        spring - the index of farming.csv's hay_weathering_summer, _autumn,
///        _winter, _spring. One home for the heap's weathering (field_haul.cpp,
///        SpoilFieldHeaps) and the readers of a heap's loss below.
std::uint32_t HayWeatheringSeason(std::uint32_t month);

/// @brief A loose hay heap's shelf in game days by its nightly loss: the
///        month's share over the month's days, as 1 / that daily share;
///        infinity when the season weathers none. Comparable with
///        FieldHeapShelfDays, whose daily share is 1 / shelf.
float HayHeapShelfDays(float season_share);

/// @brief Loose hay's loss on its FIELD (#1200): resources.csv `hay` and
///        farming.csv's hay_weathering_* (the month's share). Loose hay has no
///        spoil_days - it keeps for ever in a store - but on its field it
///        weathers by the season (#1015). Until #1200 four readers read the
///        empty cell as «keeps for ever» on the field too: the plan's order
///        (hay last of its level), the heap's spoil urgency (never), labour's
///        «keeps past the snow» (always) and the carting-peak estimate (no
///        cap). Each reader keeps its own spoil_days and keeping factor and
///        passes them in; this holds only what hay adds.
struct HayWeathering {
  ResourceId hay;                    ///< resources.csv `hay`; invalid with no such row
  std::array<float, 4> by_season{};  ///< farming.csv, the month's share (HayWeatheringSeason)
};

/// @brief A field heap's shelf in game days in month `month`: hay's by its
///        weathering (HayHeapShelfDays), any other resource's by `spoil_days`
///        x `keeping_factor` (FieldHeapShelfDays); infinity for one that keeps.
float FieldHeapShelfDaysOf(const std::vector<float>& spoil_days,
                           float keeping_factor,
                           const HayWeathering& hay,
                           ResourceId resource,
                           std::uint32_t month);

/// @brief What a field heap of `held` grams of `resource` loses in a day of
///        month `month`: hay's weathering share (rounded as SpoilFieldHeaps
///        rounded it), any other resource's SpoiledToday at its spoil_days and
///        the keeping factor; 0 for one that keeps.
Grams FieldHeapLossToday(const std::vector<float>& spoil_days,
                         float keeping_factor,
                         const HayWeathering& hay,
                         ResourceId resource,
                         std::uint32_t month,
                         Grams held);

/// @brief The ROT MARGIN: grams to hold beyond `held` so that `held` is
/// still there after `days` of SpoiledToday's daily share —
/// held / (1 − 1/spoil_days)^days − held.
///
/// ONE HOME for the seed fund's margin (core_residents, family_exchange) and
/// the room booked for missing seed (core_production, seed_room): the
/// booking held the bare norm, and seed 1939 lay at exactly its rung on the
/// snow and 2-3 t below it on 1 March, every gram of the difference rot.
/// @param spoil_days Effective shelf life (spoil_days × keeping factor);
///        1 or less keeps nothing apart and returns 0.
/// @return Grams, 0 or more.
Grams RotMarginGrams(Grams held, float spoil_days, std::uint32_t days);

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
