// «Край» and «Без соли» (core_residents/salt_edge.h; #1630 (C)).

#include "salt_edge.h"

#include <cstdint>
#include <vector>

#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "demography.h"

namespace core {
namespace {

/// The counter-hash stream of the step-4 death draw: its own, so a salt death
/// moves no other draw in the world.
constexpr std::uint64_t kSaltEdgeStream = 0x53414C5445444745ULL;  // "SALTEDGE"

}  // namespace

void RunSaltEdgeDay(const LifeConfig& config, WorldState& current) {
  const SaltIssueConfig& salt = config.salt;
  if (!salt.on) {
    return;
  }
  const SimDay day = current.calendar.day;
  const Tick now = current.calendar.tick;
  // THE FAMILIES THAT COUNT: at least one member in the village today.
  std::vector<std::uint8_t> present(current.families.rows.size(), 0);
  for (const ResidentRow& person : current.residents.rows) {
    const std::uint32_t row = FindRow(current.families, person.family);
    if (row != kNoRow && !AwayInDistrict(person, now)) {
      present[row] = 1;
    }
  }
  std::uint32_t counted = 0;
  std::uint32_t on_edge = 0;
  for (std::size_t row = 0; row < present.size(); ++row) {
    if (present[row] != 0) {
      ++counted;
      on_edge += current.families.rows[row].salt_step >= 4U ? 1U : 0U;
    }
  }
  // THE RUN AND THE CONDITION, judged before the day's departures.
  const bool half = counted > 0 && static_cast<float>(on_edge) >=
                                       salt.edge_village_share * static_cast<float>(counted);
  current.salt.half_on_edge_days =
      half ? static_cast<std::uint16_t>(current.salt.half_on_edge_days + 1U) : std::uint16_t{0};
  if (current.salt.ending_raised == 0 &&
      static_cast<float>(current.salt.half_on_edge_days) >= salt.edge_series_days) {
    current.salt.ending_raised = 1;
    SimEvent& due = EmitEvent(current, EventKind::kSaltEndingDue, EventSeverity::kInterrupting);
    due.amount = current.salt.half_on_edge_days;
  }
  // THE DEATHS OF «КРАЙ»: the step-4 families' children and elders.
  std::vector<ResidentId> dead;
  for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
    const ResidentRow& person = current.residents.rows[row];
    const std::uint32_t family = FindRow(current.families, person.family);
    if (family == kNoRow || current.families.rows[family].salt_step < 4U) {
      continue;
    }
    const float age = BiologicalAgeYears(config.life_speedup, person.birth_day, day);
    if (age >= salt.edge_child_years && age < salt.edge_elder_years) {
      continue;
    }
    const ResidentId id = current.residents.row_ids[row];
    if (CounterHashUnitFloat(
            current.world_seed, static_cast<std::uint64_t>(day), id.value, kSaltEdgeStream) <
        salt.edge_death_share) {
      dead.push_back(id);
    }
  }
  BuryTheDead(config, current, dead, DeathCause::kSalt);
  // THE LEAVING: a family salt_edge_leave_days into step 4 goes whole.
  std::vector<FamilyId> leaving;
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    const FamilyRow& family = current.families.rows[row];
    if (family.salt_step >= 4U &&
        family.salt_hunger_days >= salt.step4_days + salt.edge_leave_days) {
      leaving.push_back(current.families.row_ids[row]);
    }
  }
  for (const FamilyId family : leaving) {
    std::vector<ResidentId> members;
    for (std::uint32_t row = 0; row < current.residents.rows.size(); ++row) {
      if (current.residents.rows[row].family.value == family.value) {
        members.push_back(current.residents.row_ids[row]);
      }
    }
    {
      SimEvent& left =
          EmitEvent(current, EventKind::kFamilyLeftForSalt, EventSeverity::kInterrupting);
      left.family = family;
      left.amount = static_cast<std::int64_t>(members.size());
    }
    for (const ResidentId member : members) {
      {
        SimEvent& gone = EmitEvent(current, EventKind::kResidentLeft, EventSeverity::kNotable);
        gone.resident = member;
        gone.family = family;
      }
      RemoveResident(current, member);
    }
    const auto count = static_cast<std::uint32_t>(members.size());
    current.ledger.current.departures += count;
    current.ledger.current.departures_salt += count;
    DropFamilyIfEmpty(current, family);
  }
}

}  // namespace core
