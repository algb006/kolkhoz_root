// The post rules (posts.h): eligibility, where a post exists, and what
// refuses an appointment.

#include "posts.h"

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "labor_day.h"
#include "work_orders.h"

namespace core {
namespace {

/// @brief Whether `resident` holds exactly this post, at this unit.
bool HoldsPostAt(const ResidentRow& resident, ProfessionId profession, UnitId unit) {
  return resident.post.profession.value == profession.value &&
         resident.post.unit.value == unit.value;
}

/// @brief How many residents hold `profession` at `unit`, not counting the
/// man being appointed: re-appointing somebody to the post he already holds
/// is a no-op and must not be refused for the place he himself fills.
std::uint32_t HoldersAt(const WorldState& world,
                        ProfessionId profession,
                        UnitId unit,
                        ResidentId except) {
  std::uint32_t held = 0;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    if (world.residents.row_ids[row].value == except.value) {
      continue;
    }
    if (HoldsPostAt(world.residents.rows[row], profession, unit)) {
      ++held;
    }
  }
  return held;
}

/// @brief Whether anybody but `except` holds `profession` anywhere in the
/// village — the test behind `single_post`.
bool HeldAnywhere(const WorldState& world, ProfessionId profession, ResidentId except) {
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    if (world.residents.row_ids[row].value == except.value) {
      continue;
    }
    if (world.residents.rows[row].post.profession.value == profession.value) {
      return true;
    }
  }
  return false;
}

}  // namespace

const StaffSlot* FindStaffSlot(const LaborConfig& config,
                               UnitTypeId type,
                               std::uint8_t level,
                               ProfessionId profession) {
  if (level == 0) {
    return nullptr;  // a marked site is pegs and string: nobody's place yet
  }
  for (const StaffSlot& slot : config.staff) {
    if (slot.unit_type.value != type.value || slot.profession.value != profession.value) {
      continue;
    }
    // An empty level column means every step of the ladder; a filled one
    // means that step and no other (manual/74-posts.md §6).
    if (slot.level == 0 || slot.level == level) {
      return &slot;
    }
  }
  return nullptr;
}

bool IsEligible(const LaborConfig& config,
                const ResidentRow& resident,
                ProfessionId profession,
                float age) {
  if (profession.value >= config.professions.size()) {
    return false;
  }
  const ProfessionDef& post = config.professions[profession.value];
  const float min_age = post.min_age_years > 0.0F ? post.min_age_years : config.adult_age_years;
  if (age < min_age) {
    return false;
  }
  if (post.max_age_years > 0.0F && age > post.max_age_years) {
    return false;
  }
  if (post.sex_rule == PostSexRule::kFemale && resident.sex != Sex::kFemale) {
    return false;
  }
  if (post.sex_rule == PostSexRule::kMale && resident.sex != Sex::kMale) {
    return false;
  }
  return static_cast<std::uint8_t>(resident.education_stage) >=
         static_cast<std::uint8_t>(post.min_education);
}

OrderRefusal CheckAppointment(const LaborConfig& config,
                              const WorldState& world,
                              const OrderRow& order) {
  const std::uint32_t resident_row = FindRow(world.residents, order.resident);
  if (resident_row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  const std::uint32_t unit_row = FindRow(world.units, order.unit);
  if (unit_row == kNoRow) {
    // "There is no such unit" and "that unit takes no groom" are two
    // different sentences for the presentation to say, and it cannot build
    // them from one code (boss, 2026-09-03). A named thing that is gone
    // answers kNoSuchSubject here exactly as it does in construction: the
    // same case gets the same answer whichever book it lies in.
    return OrderRefusal::kNoSuchSubject;
  }
  const UnitRow& unit = world.units.rows[unit_row];
  if (FindStaffSlot(config, unit.type, unit.level, order.profession) == nullptr) {
    return OrderRefusal::kRuleForbids;  // a site, or a unit with no such place
  }
  const ResidentRow& resident = world.residents.rows[resident_row];
  const float age = BiologicalAgeYears(config, resident.birth_day, world.calendar.day);
  if (!IsEligible(config, resident, order.profession, age)) {
    return OrderRefusal::kNotEligible;
  }
  const ProfessionDef& post = config.professions[order.profession.value];
  if (post.single_post != 0 && HeldAnywhere(world, order.profession, order.resident)) {
    return OrderRefusal::kNoVacancy;  // one to a village, and it is taken
  }
  const StaffSlot* slot = FindStaffSlot(config, unit.type, unit.level, order.profession);
  if (slot->slots != 0 &&
      HoldersAt(world, order.profession, order.unit, order.resident) >= slot->slots) {
    return OrderRefusal::kNoVacancy;
  }
  // AND THE SAME RULE THE OTHER WAY ROUND (task A8 delivery cycle).
  // CheckAssignWork refuses a work order for a man who holds a post — "two
  // answers to what this man does is one too many" — but the reverse was
  // not checked, so kAppoint after kAssignWork left BOTH alive and the
  // standing order quietly overwrote the post placement every morning.
  // That is exactly the outcome 76-work-orders.md §2 forbids, arrived at
  // from the other side. Whichever of the two arrives second is refused;
  // the chairman releases him first, and that is one order, not a guess.
  if (StandingWorkRow(world, order.resident, kNoRow) != kNoRow &&
      !ReleaseIsInTheBook(world, order.resident)) {
    // ... unless the chairman is releasing him in the same batch. "Release
    // him, then appoint him" is one gesture in the office and two rows in
    // one book, and the two work verbs are read AFTER the two post verbs in
    // the tick — so without this the appointment would be refused against
    // an order settled kDone a few statements later, and the man would end
    // the step with neither post nor work. The workflow this refusal
    // prescribes must not be the workflow it breaks.
    return OrderRefusal::kConflictsWithActive;
  }
  return OrderRefusal::kNone;
}

OrderRefusal CheckDismissal(const WorldState& world, const OrderRow& order) {
  const std::uint32_t resident_row = FindRow(world.residents, order.resident);
  if (resident_row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  if (world.residents.rows[resident_row].post.profession.value == kInvalidDefIdValue) {
    return OrderRefusal::kRuleForbids;  // there is nothing to remove him from
  }
  return OrderRefusal::kNone;
}

bool HasWaitingPostOrder(const WorldState& world, ResidentId resident, std::uint32_t self) {
  for (std::uint32_t row = 0; row < world.orders.rows.size(); ++row) {
    if (row == self) {
      continue;
    }
    const OrderRow& other = world.orders.rows[row];
    if (other.status != OrderStatus::kAccepted) {
      continue;
    }
    if (other.kind != OrderKind::kAppoint && other.kind != OrderKind::kDismiss) {
      continue;
    }
    if (other.resident.value == resident.value) {
      return true;
    }
  }
  return false;
}

}  // namespace core
