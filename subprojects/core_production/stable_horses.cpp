// The one-time horse transfer (stable_horses.h). Kept apart from the herd
// day because it is not part of it: RunHerdDay is what happens every day,
// this is what happens once in a campaign. The contract, and the reasons
// behind the preconditions, are in the header.

#include "stable_horses.h"

#include <cstdint>
#include <vector>

#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/herd_age_band.h"
#include "core_common/herd_state.h"
#include "core_common/ids.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "herd_life.h"

namespace core {
namespace {

/// The fewest heads a stabled team needs for one of them to be its stallion.
constexpr std::uint16_t kLeastBreedingPair = 2;

}  // namespace

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
    MergeAdultAgeBand(team, team.adult_count, herd);
    team.adult_count = static_cast<std::uint16_t>(team.adult_count + herd.adult_count);
    team.juvenile_count = static_cast<std::uint16_t>(team.juvenile_count + herd.juvenile_count);
    team.newborn_count = static_cast<std::uint16_t>(team.newborn_count + herd.newborn_count);
    team.adult_age_game_years_total += herd.adult_age_game_years_total;
    emptied.push_back(current.herds.row_ids[row]);
  }
  for (const HerdId id : emptied) {
    RemoveRow(current.herds, id);
  }
  // THE SIRE COUNT IS COMPOSED HERE, and since 2026-09-16 it has to be. This
  // used to be left to the herd day, which re-derived the count for every row
  // every morning; that re-derive is gone, because it also erased the sex of
  // a head the chairman had BOUGHT (boss, parcel 20).
  //
  // Summing is still wrong, and for the reason this block always gave: the
  // start's team arrives as sixteen lone "herds" of one head, each carrying
  // nought sires because one animal cannot be the herd's share of them. The
  // sum is nought, and a team of sixteen mares with no stallion never foals —
  // measured, and it killed the canonical team off inside ten years.
  //
  // So the team is COMPOSED, which is what TargetMales is for: a herd being
  // founded takes its share of sires. That is the whole difference between
  // this call and the one that was deleted — founding a herd, not correcting
  // one every day.
  //
  // AND A TEAM OF TWO OR MORE ALWAYS HAS ITS STALLION (boss, boss-core-
  // epoch1-5 seq 22, option б; livestock design, «Конюшня делает две
  // вещи»: «в сведённом табуне жеребец выводится»). The share alone rounds
  // to nought below eight heads (0.07 x 7 + 0.5 < 1), so a team stabled late
  // came in as mares only and never foaled: seed 1939 with a year of
  // inaction was stabled at four heads and was down to two in its sixth
  // year. That step by head count was nowhere in the design and nothing
  // showed it to the player. One head stays one head: a pair is the least
  // that can breed.
  if (gathered != kNoRow) {
    HerdRow& team = current.herds.rows[gathered];
    if (team.kind.value < config.livestock.size()) {
      const LivestockDef& kind = config.livestock[team.kind.value];
      team.adult_male_count = TargetMales(kind, team.adult_count);
      if (kind.sexed != 0 && team.adult_count >= kLeastBreedingPair && team.adult_male_count == 0) {
        team.adult_male_count = 1;
      }
    }
  }
  current.chairman.horses_stabled = 1;
  if (moved_heads == 0) {
    return;  // nothing actually came in: a loaded save, and no news in it
  }
  SimEvent& event = EmitEvent(current, EventKind::kHorsesStabled, EventSeverity::kNotable);
  event.unit = yard;
  event.amount = static_cast<std::int64_t>(moved_heads);
}

}  // namespace core
