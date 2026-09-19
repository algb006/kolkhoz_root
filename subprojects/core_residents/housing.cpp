// The free house a household moves into (housing.h).

#include "housing.h"

#include <cstdint>
#include <vector>

#include "core_common/family_state.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"

namespace core {
namespace {

bool IsFreeHouse(const LifeConfig& config, const UnitRow& unit) {
  return unit.household.value == kInvalidEntityIdValue && unit.level != 0 &&
         unit.type.value < config.definitions.units.is_housing.size() &&
         config.definitions.units.is_housing[unit.type.value] != 0 &&
         ResidentsCapacity(config, unit) <= 0.0F;
}

bool OnTheBrink(const LifeConfig& config, const UnitRow& unit) {
  return config.old_house_type.value != kInvalidDefIdValue &&
         unit.type.value == config.old_house_type.value &&
         unit.wear >= config.old_house_near_collapse_wear * kWearScale;
}

}  // namespace

float ResidentsCapacity(const LifeConfig& config, const UnitRow& unit) {
  if (unit.level == 0 || unit.dead != 0 || unit.type.value >= config.residents_capacity.size()) {
    return 0.0F;
  }
  const std::vector<float>& ladder = config.residents_capacity[unit.type.value];
  const std::size_t index = static_cast<std::size_t>(unit.level) - 1U;
  return index < ladder.size() ? ladder[index] : 0.0F;
}

UnitId FreeHouse(const LifeConfig& config, const WorldState& current, bool cold) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const UnitRow& unit = current.units.rows[row];
    // A house held for a specialist is nobody's — but a freezing family's
    // (boss seq 191: «кроме бездомных зимой: замерзающая семья важнее»).
    if (IsFreeHouse(config, unit) && (cold || unit.reserved_for_specialist == 0)) {
      return current.units.row_ids[row];
    }
  }
  return UnitId{};
}

UnitId FreeHouseNotOnTheBrink(const LifeConfig& config, const WorldState& current) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const UnitRow& unit = current.units.rows[row];
    if (IsFreeHouse(config, unit) && !OnTheBrink(config, unit) &&
        unit.reserved_for_specialist == 0) {
      return current.units.row_ids[row];
    }
  }
  return UnitId{};
}

UnitId BarrackPlace(const LifeConfig& config, const WorldState& current, std::uint32_t people) {
  // People living in each unit, by the families whose house it is.
  std::vector<std::uint32_t> living(current.units.rows.size(), 0);
  for (const ResidentRow& resident : current.residents.rows) {
    const std::uint32_t family_row = FindRow(current.families, resident.family);
    if (family_row == kNoRow) {
      continue;
    }
    const std::uint32_t unit_row = FindRow(current.units, current.families.rows[family_row].house);
    if (unit_row != kNoRow) {
      ++living[unit_row];
    }
  }
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const float capacity = ResidentsCapacity(config, current.units.rows[row]);
    if (capacity > 0.0F && static_cast<float>(living[row] + people) <= capacity) {
      return current.units.row_ids[row];
    }
  }
  return UnitId{};
}

UnitId HomeForNewcomers(const LifeConfig& config,
                        const WorldState& current,
                        std::uint32_t people,
                        bool& shared) {
  const UnitId house = FreeHouseNotOnTheBrink(config, current);
  if (house.value != kInvalidEntityIdValue) {
    shared = false;
    return house;
  }
  const UnitId barrack = BarrackPlace(config, current, people);
  shared = barrack.value != kInvalidEntityIdValue;
  return barrack;
}

}  // namespace core
