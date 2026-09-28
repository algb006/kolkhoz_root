#include "appearance_memory.h"

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/family_state.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"

namespace core {
namespace {

/// A year's memory: seeded with today's value the first time it is asked,
/// then a year's step a day towards it (an exponential mean of about a year:
/// «за последний год, медленно, и в обе стороны»).
/// Held on 0..100 whatever it is fed: the save refuses a memory off the scale,
/// and a family's satisfaction is clamped only at nought — tables whose
/// weights summed past 100 would have written a save that no longer loads
/// (static review of 0.37.17).
Metric OnTheScale(Metric value) {
  return value < 0.0F ? 0.0F : (value > 100.0F ? 100.0F : value);
}

void RememberTheYear(Metric today, bool new_day, Metric& memory) {
  if (memory < 0.0F) {
    memory = OnTheScale(today);
    return;
  }
  if (new_day) {
    memory = OnTheScale(memory + ((today - memory) / static_cast<float>(kDaysPerYear)));
  }
}

}  // namespace

void RememberWellbeing(const LifeConfig& life,
                       WorldState& current,
                       std::uint32_t family_item,
                       bool new_day) {
  FamilyRow& family = current.families.rows[family_item];
  RememberTheYear(family.satisfaction, new_day, family.satisfaction_year);
  const FamilyId id = current.families.row_ids[family_item];
  const SimDay today = current.calendar.day;
  for (ResidentRow& resident : current.residents.rows) {
    if (resident.family.value != id.value) {
      continue;
    }
    RememberTheYear(resident.satiety, new_day, resident.satiety_year);
    // THE CHILDHOOD, the mean of its days, frozen at growing up. Seeded with
    // today's satiety as if every day before had been today — for the start's
    // children and adults alike, the STUB of the ruined village (@file).
    // STUB: on the start's first day that satiety is genesis's 70, the middle
    // of the scale and no emaciated face (boss-core-epoch1-queue-2026-09-29
    // [22]); the seed for the start's adults is `look`'s number to set.
    if (resident.satiety_childhood < 0.0F) {
      resident.satiety_childhood = OnTheScale(resident.satiety);
      continue;
    }
    if (!new_day ||
        BiologicalAgeYears(life.life_speedup, resident.birth_day, today) >= life.adult_age_years) {
      continue;
    }
    // The day of birth is a sample too: the mean after `lived` days since it
    // holds lived + 1 of them (static review of 0.37.17 — divided by `lived`,
    // a newborn's seeded day was thrown away on its first).
    const std::int64_t lived =
        static_cast<std::int64_t>(today) - static_cast<std::int64_t>(resident.birth_day);
    const float samples = lived > 0 ? static_cast<float>(lived + 1) : 1.0F;
    resident.satiety_childhood = OnTheScale(
        resident.satiety_childhood + ((resident.satiety - resident.satiety_childhood) / samples));
  }
}

}  // namespace core
