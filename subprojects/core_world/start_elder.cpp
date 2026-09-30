#include "start_elder.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_log/log.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 2> kStartElderKeys = {"elder_age_min_years",
                                                             "elder_age_max_years"};

/// A family's oldest man within the band, and his age; invalid when it has
/// none.
struct OldestMan {
  ResidentId resident;
  float age_years = 0.0F;
};

OldestMan OldestManInBand(const StartElder& band,
                          float life_speedup,
                          const WorldState& world,
                          FamilyId family) {
  OldestMan oldest;
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    const ResidentRow& person = world.residents.rows[row];
    if (person.family.value != family.value || person.sex != Sex::kMale) {
      continue;
    }
    const float age = BiologicalAgeYears(life_speedup, person.birth_day, world.calendar.day);
    if (age < band.age_min_years || age > band.age_max_years) {
      continue;
    }
    if (oldest.resident.value == kInvalidEntityIdValue || age > oldest.age_years) {
      oldest = OldestMan{.resident = world.residents.row_ids[row], .age_years = age};
    }
  }
  return oldest;
}

/// The two families change houses: each house's `household` and each
/// family's `house`.
void SwapHouses(WorldState& world, std::uint32_t first_row, std::uint32_t second_row) {
  FamilyRow& first = world.families.rows[first_row];
  FamilyRow& second = world.families.rows[second_row];
  const std::uint32_t first_unit = FindRow(world.units, first.house);
  const std::uint32_t second_unit = FindRow(world.units, second.house);
  if (first_unit != kNoRow) {
    world.units.rows[first_unit].household = world.families.row_ids[second_row];
  }
  if (second_unit != kNoRow) {
    world.units.rows[second_unit].household = world.families.row_ids[first_row];
  }
  const UnitId first_house = first.house;
  first.house = second.house;
  second.house = first_house;
}

}  // namespace

std::span<const std::string_view> StartElderWorldParamKeys() {
  return kStartElderKeys;
}

StartElder ReadStartElder(const ITableSet& tables) {
  const StartElder defaults;
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return defaults;
  }
  StartElder band = defaults;
  const Range years{.low = 16.0F, .high = 100.0F};
  const std::array<ScalarKnob, kStartElderKeys.size()> knobs = {
      ScalarKnob{.key = kStartElderKeys[0], .value = &band.age_min_years, .range = years},
      ScalarKnob{.key = kStartElderKeys[1], .value = &band.age_max_years, .range = years}};
  std::string trouble;
  if (!ReadKnobs(*world, "world_params", knobs, trouble)) {
    LogError("genesis: " + trouble + " — the documented elder's age 45..60 is used");
    return defaults;
  }
  if (band.age_min_years > band.age_max_years) {
    LogError("genesis: the elder's age band is reversed — the documented 45..60 is used");
    return defaults;
  }
  return band;
}

void SeatStartElder(const StartElder& band,
                    float life_speedup,
                    UnitId elder_yard,
                    WorldState& world) {
  world.named.elder = ResidentId{};
  const std::uint32_t yard_row = FindRow(world.units, elder_yard);
  const FamilyId yard_family =
      yard_row != kNoRow ? world.units.rows[yard_row].household : FamilyId{};
  const std::uint32_t yard_family_row = FindRow(world.families, yard_family);
  if (yard_family_row == kNoRow) {
    LogWarning("genesis: the elder's yard has no family — the village has no elder");
    return;
  }
  const OldestMan own = OldestManInBand(band, life_speedup, world, yard_family);
  if (own.resident.value != kInvalidEntityIdValue) {
    world.named.elder = own.resident;
    return;
  }
  // THE YARD'S FAMILY HAS NO MAN OF THE AGE: the family with the oldest one
  // moves in, and the yard's family takes its house — the start's yards are
  // all old houses, so neither family is the poorer for it.
  OldestMan best;
  std::uint32_t best_row = kNoRow;
  for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
    // Only a family under a roof: a swap with a houseless one would leave the
    // yard's family out of doors (more households than yards).
    if (FindRow(world.units, world.families.rows[row].house) == kNoRow) {
      continue;
    }
    const OldestMan candidate =
        OldestManInBand(band, life_speedup, world, world.families.row_ids[row]);
    if (candidate.resident.value != kInvalidEntityIdValue &&
        (best_row == kNoRow || candidate.age_years > best.age_years)) {
      best = candidate;
      best_row = row;
    }
  }
  if (best_row == kNoRow) {
    LogWarning("genesis: no man of the village is of the elder's age — the village has no elder");
    return;
  }
  SwapHouses(world, yard_family_row, best_row);
  world.named.elder = best.resident;
}

ElderView ElderViewOf(const WorldState& world) {
  const std::uint32_t row = FindRow(world.residents, world.named.elder);
  if (row == kNoRow) {
    return ElderView{};
  }
  const std::uint32_t family_row = FindRow(world.families, world.residents.rows[row].family);
  return ElderView{
      .resident = world.named.elder,
      .house = family_row != kNoRow ? world.families.rows[family_row].house : UnitId{}};
}

}  // namespace core
