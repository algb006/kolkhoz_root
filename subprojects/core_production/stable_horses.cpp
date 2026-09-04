// The one-time horse transfer (stable_horses.h). Kept apart from the herd
// day because it is not part of it: RunHerdDay is what happens every day,
// this is what happens once in a campaign. The contract, and the reasons
// behind the preconditions, are in the header.

#include "stable_horses.h"

#include <cstdint>
#include <vector>

#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"

namespace core {

void StableHorses(const ProductionConfig& config, WorldState& current) {
  if (current.chairman.horses_stabled != 0 || config.groom_post.value == kInvalidDefIdValue ||
      config.horse_kind.value == kInvalidDefIdValue) {
    return;
  }
  UnitId yard;
  for (const ResidentRow& resident : current.residents.rows) {
    if (resident.post.profession.value != config.groom_post.value) {
      continue;
    }
    const std::uint32_t unit_row = FindRow(current.units, resident.post.unit);
    if (unit_row == kNoRow || current.units.rows[unit_row].level == 0) {
      continue;  // his yard is gone, or is still pegs and string
    }
    yard = resident.post.unit;
    break;
  }
  if (yard.value == kInvalidEntityIdValue) {
    return;
  }
  std::uint32_t gathered = kNoRow;
  std::uint32_t moved_heads = 0;
  std::vector<HerdId> emptied;
  for (std::uint32_t row = 0; row < current.herds.rows.size(); ++row) {
    HerdRow& herd = current.herds.rows[row];
    if (herd.kind.value != config.horse_kind.value || herd.household_owned != 0) {
      continue;  // a family's OWN horse is not the kolkhoz's to gather
    }
    if (herd.household.value != kInvalidEntityIdValue) {
      moved_heads += static_cast<std::uint32_t>(herd.adult_count) +
                     static_cast<std::uint32_t>(herd.juvenile_count) +
                     static_cast<std::uint32_t>(herd.newborn_count);
    }
    if (gathered == kNoRow) {
      gathered = row;
      herd.unit = yard;
      herd.household = FamilyId{};
      continue;
    }
    HerdRow& team = current.herds.rows[gathered];
    team.adult_count = static_cast<std::uint16_t>(team.adult_count + herd.adult_count);
    team.juvenile_count = static_cast<std::uint16_t>(team.juvenile_count + herd.juvenile_count);
    team.newborn_count = static_cast<std::uint16_t>(team.newborn_count + herd.newborn_count);
    team.adult_age_game_years_total += herd.adult_age_game_years_total;
    emptied.push_back(current.herds.row_ids[row]);
  }
  for (const HerdId id : emptied) {
    RemoveRow(current.herds, id);
  }
  // The sire count is NOT set here: it is a herd-system invariant, re-derived
  // for every row by the walk that follows this call. Summing sixteen lone
  // "herds" of one stallion each would leave a team of sixteen stallions and
  // no mares.
  current.chairman.horses_stabled = 1;
  if (moved_heads == 0) {
    return;  // nothing actually came in: a loaded save, and no news in it
  }
  SimEvent& event = EmitEvent(current, EventKind::kHorsesStabled, EventSeverity::kNotable);
  event.unit = yard;
  event.amount = static_cast<std::int64_t>(moved_heads);
}

}  // namespace core
