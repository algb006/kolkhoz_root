// The free house a household moves into (housing.h).

#include "housing.h"

#include <cstdint>

#include "core_common/quantities.h"
#include "core_common/unit_state.h"

namespace core {

namespace {

bool IsFreeHouse(const LifeConfig& config, const UnitRow& unit) {
  return unit.household.value == kInvalidEntityIdValue && unit.level != 0 &&
         unit.type.value < config.definitions.units.is_housing.size() &&
         config.definitions.units.is_housing[unit.type.value] != 0;
}

bool OnTheBrink(const LifeConfig& config, const UnitRow& unit) {
  return config.old_house_type.value != kInvalidDefIdValue &&
         unit.type.value == config.old_house_type.value &&
         unit.wear >= config.old_house_near_collapse_wear * kWearScale;
}

}  // namespace

UnitId FreeHouse(const LifeConfig& config, const WorldState& current) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    if (IsFreeHouse(config, current.units.rows[row])) {
      return current.units.row_ids[row];
    }
  }
  return UnitId{};
}

UnitId FreeHouseNotOnTheBrink(const LifeConfig& config, const WorldState& current) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const UnitRow& unit = current.units.rows[row];
    if (IsFreeHouse(config, unit) && !OnTheBrink(config, unit)) {
      return current.units.row_ids[row];
    }
  }
  return UnitId{};
}

}  // namespace core
