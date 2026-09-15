/// @file
/// @brief Implementation of fund_ladder.h.

#include "core_common/fund_ladder.h"

#include <cstdint>

#include "core_common/land_state.h"
#include "core_common/world_state.h"

namespace core {

ResourceAmounts HeldAboveFodder(const WorldState& world,
                                std::span<const SeedNorm> seed_norms_by_crop,
                                std::size_t resource_count,
                                bool reserve_seed_fund) {
  ResourceAmounts held(resource_count, 0);
  if (reserve_seed_fund) {
    for (const FieldRow& field : world.fields.rows) {
      // UNTIL THE SOWING TAKES IT, not until the ploughing starts: a field is
      // done needing seed once the crop is in the ground (69-reconciliation.md
      // §13.16 — the fund used to guard a phase of the field rather than a
      // quantity of grain).
      const bool already_sown =
          field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest;
      // AND NOT ONCE THE REAPING HAS TAKEN IT (2026-09-15, boss parcel 421): a
      // reaped field is idle again and still names this year's crop until the
      // year's turn, so the fund held seed for a sowing already sown and
      // reaped — 52.5 t of potatoes from the October digging to New Year.
      const bool reaped_this_year =
          field.reaped_day != kNeverReapedDay &&
          field.reaped_day / kDaysPerYear == world.calendar.day / kDaysPerYear;
      if (already_sown || reaped_this_year ||
          field.rotation_year0.value >= seed_norms_by_crop.size()) {
        continue;
      }
      const SeedNorm& seed = seed_norms_by_crop[field.rotation_year0.value];
      if (seed.resource.value >= held.size() || seed.sowing_norm_kg_per_ha <= 0.0F) {
        continue;
      }
      held[seed.resource.value] += GramsFromKilograms(seed.sowing_norm_kg_per_ha * field.area_ga);
    }
  }
  // THE PLAN RESERVE IS FILLED BY THE HARVEST, NOT BY THE CALENDAR.
  const ResourceAmounts& reaped = world.ledger.current.harvest;
  for (std::uint32_t index = 0; index < world.plan.due.size() && index < held.size(); ++index) {
    const Grams owed = world.plan.due[index];
    const Grams gathered = index < reaped.size() ? reaped[index] : 0;
    held[index] += owed < gathered ? owed : gathered;
  }
  for (const ResourceAmounts& opened : world.unsealed.by_fund) {
    for (std::uint32_t index = 0; index < opened.size() && index < held.size(); ++index) {
      held[index] = held[index] > opened[index] ? held[index] - opened[index] : 0;
    }
  }
  return held;
}

}  // namespace core
