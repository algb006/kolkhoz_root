// The kinds' houses (livestock_homes.h).

#include "livestock_homes.h"

#include <cstdint>

#include "core_common/herd_state.h"
#include "core_common/unit_state.h"
#include "herd_life.h"

namespace core {
namespace {

/// Kolkhoz heads standing at the unit in row `unit_row`, of any kind.
float HeadsAt(const WorldState& world, std::uint32_t unit_row) {
  const UnitId unit = world.units.row_ids[unit_row];
  float heads = 0.0F;
  for (const HerdRow& herd : world.herds.rows) {
    if (herd.household_owned == 0 && herd.unit.value == unit.value) {
      heads += static_cast<float>(TotalHeads(herd));
    }
  }
  return heads;
}

/// Free places at the unit in row `unit_row` when it is a standing unit of
/// `type`; 0 otherwise.
float FreeAt(const ProductionConfig& config,
             const WorldState& world,
             std::uint32_t unit_row,
             UnitTypeId type) {
  const UnitRow& unit = world.units.rows[unit_row];
  if (unit.type.value != type.value || unit.level == 0 ||
      unit.type.value >= config.unit_types.size()) {
    return 0.0F;
  }
  const float places = config.unit_types[unit.type.value].LivestockCapacityHeadAt(unit.level);
  const float free = places - HeadsAt(world, unit_row);
  return free > 0.0F ? free : 0.0F;
}

}  // namespace

float HomePlacesFree(const ProductionConfig& config,
                     const WorldState& world,
                     LivestockKindId kind) {
  if (kind.value >= config.livestock.size() ||
      config.livestock[kind.value].home_unit.value == kInvalidDefIdValue) {
    return -1.0F;
  }
  const UnitTypeId home = config.livestock[kind.value].home_unit;
  float free = 0.0F;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    free += FreeAt(config, world, row, home);
  }
  return free;
}

void HouseHomelessHerds(const ProductionConfig& config, WorldState& world) {
  for (HerdRow& herd : world.herds.rows) {
    if (herd.household_owned != 0 || herd.household.value != kInvalidEntityIdValue ||
        herd.unit.value != kInvalidEntityIdValue || herd.kind.value >= config.livestock.size() ||
        herd.kind.value == config.horse_kind.value) {
      continue;
    }
    const UnitTypeId home = config.livestock[herd.kind.value].home_unit;
    if (home.value == kInvalidDefIdValue) {
      continue;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (FreeAt(config, world, row, home) >= 1.0F) {
        herd.unit = world.units.row_ids[row];
        break;
      }
    }
  }
}

}  // namespace core
