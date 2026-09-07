/// @file
/// @brief What happens to ONE FIELD: the phase it is in, the work that phase
///        asks for, the sowing, the cut and the harvest.
/// @threading SINGLE_THREADED
/// Called from the production decisions sub-step (slot 3) alone, on the sim
/// thread, outside any parallel phase. Every function here MOVES the world,
/// so none of them may be called from a worker.
///
/// THE SEAM IS THE SUBJECT, NOT THE LINE COUNT. The other half of
/// production_system.cpp moves the HOUSEHOLD: the year's plan and its
/// delivery, the order book, the manure the settlement has to spread, the
/// walk that visits every field. This half moves ONE field and knows nothing
/// about the rest — which is why the dependency runs one way only, from the
/// household to the field, and never back.
///
/// The old file had grown to 997 lines against a hard limit of 1000, and the
/// limit names the illness while saying nothing about the cure: a cut by line
/// number would have put the sowing and the harvest in different files.
///
/// One rule is worth stating because it is invisible in the signatures: a
/// field row is handed around by REFERENCE, and the id is re-derived from the
/// row when an event needs one (FieldIdOf). Threading an id through a dozen
/// helpers to say what the row already knows is the shape that lets the two
/// drift apart.

#ifndef CORE_PRODUCTION_FIELD_WORK_H_
#define CORE_PRODUCTION_FIELD_WORK_H_

#include <cstdint>

#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief kIdle for a phase that needs no work, the phase itself for the
/// four working ones.
constexpr FieldPhase KindOfWorkingPhase(FieldPhase phase) {
  switch (phase) {
    case FieldPhase::kPlowing:
    case FieldPhase::kHarrowing:
    case FieldPhase::kSowing:
    case FieldPhase::kHarvest:
      return phase;
    case FieldPhase::kIdle:
    case FieldPhase::kGrowing:
    // Not a phase: handled beside the phases that need no work, so this
    // switch keeps no default and a new phase stays a compile error.
    case FieldPhase::kFieldPhaseCount:
      return FieldPhase::kIdle;
  }
  return FieldPhase::kIdle;
}

/// @brief Clears the growing spell a field has been through, so the next
///        one starts from nothing.
///
/// Wipe the weather a field has been through: both accumulators, both
/// run counters and the judgement made off them. Kept as one function
/// because there are four places that end a growing spell (a perennial
/// plan change, a field lost to snow, a fresh sowing, a finished harvest)
/// and five fields to clear — four copies of five lines is how one of them
/// eventually keeps a stale "kSoaking" on a field that is bare.
void ClearFieldWeather(FieldRow& field);

/// @brief The id of a field row, from the row itself.
///
/// The field loops of this file hand a REFERENCE around — that is how they
/// were written, and threading an id through a dozen helpers to say one
/// sentence in the journal would be a larger change than the sentence. The
/// rows live in a vector, so the reference names its own index.
///
/// PRECONDITION, and the only one: `field` is a row of `current.fields`, not
/// a copy of one. Every caller here is inside a loop over those rows; the
/// assert catches the day somebody passes a temporary.
FieldId FieldIdOf(const WorldState& current, const FieldRow& field);

/// @brief Moves a field into a phase AND says so.
///
/// One function because there are ten places that move a phase, and ten
/// copies of "set it, then announce it" is exactly how eighteen event kinds
/// came to have no emitter at all (boss, 2026-09-05). The announcement
/// carries the NEW phase in `amount`, as the kind's contract says.
void MoveFieldPhase(WorldState& current, FieldRow& field, FieldPhase phase);

/// @brief Moves the field into a working phase and sizes its demand:
/// area x the phase's norm. The crop is the one in the ground or, while
/// the field is still being prepared, the one this year's rotation plans.
void OpenPhase(const ProductionConfig& config,
               WorldState& current,
               FieldRow& field,
               FieldPhase phase);

/// @brief The manure bonus this field has coming, by the share of its dose
/// it received (FieldRow::manure_applied is that share in percent).
float ManureBonus(const ProductionConfig& config, const FieldRow& field);

/// @brief Opens the meadow's cut when its month comes round; does nothing
///        on every other day of the year.
///
/// The meadow's whole year: it stands, and once a season the scythes go
/// out. No sowing window, no temperature gate, no snow loss (grass winters
/// where it grew), no fertility — a meadow is land, not a crop
/// (land_state.h, LandKind; boss answer Q6).
void RunMeadow(const ProductionConfig& config,
               WorldState& current,
               FieldRow& field,
               std::uint8_t month);

/// @brief Opens the ploughing for NEXT year's slot when the autumn window
///        and the temperature both allow it.
///
/// The autumn sowing (defect D12). A winter crop is harvested the summer
/// AFTER it is sown, so the slot it belongs to is next year's — "winter rye
/// goes into the ground in the autumn of the same year, and the ring starts
/// turning in the second" (start canon §8). Sown from this year's slot it
/// arrived a year late and ate the following spring as well.
void TrySowWinter(const ProductionConfig& config,
                  WorldState& current,
                  FieldRow& field,
                  std::uint8_t month,
                  float temperature);

/// @brief Opens the ploughing for this year's slot when the spring window
///        and the temperature both allow it.
///
/// The field year opens here: the sowing window and the temperature say
/// "go", and the field enters plowing. What follows — harrowing, sowing —
/// is paced by the crew, so the seed may well go into the ground after
/// the window has closed. That is the point of the seam: the window is
/// when the work STARTS, the crew decides when it ends.
void TrySow(const ProductionConfig& config,
            WorldState& current,
            FieldRow& field,
            std::uint8_t month,
            float temperature);

/// @brief Closes a worked-through sowing phase: the seed is in the ground
///        and the field starts growing.
///
/// The seed goes into the ground when the sowing phase is worked through.
void FinishSowing(const ProductionConfig& config, WorldState& current, FieldRow& field);

/// @brief Closes a worked-through harvest phase: arable pays into the
///        field buffer, a meadow into the manger and the stores.
///
/// The reaped field pays out and leaves the harvest phase.
void FinishHarvest(const ProductionConfig& config, WorldState& current, FieldRow& field);

}  // namespace core

#endif  // CORE_PRODUCTION_FIELD_WORK_H_
