// The free house a household moves into (housing.h).

#include "housing.h"

#include <cstdint>

#include "core_common/unit_state.h"

namespace core {

UnitId FreeHouse(const LifeConfig& config, const WorldState& current) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const UnitRow& unit = current.units.rows[row];
    if (unit.household.value != kInvalidEntityIdValue || unit.level == 0 ||
        unit.type.value >= config.definitions.units.is_housing.size() ||
        config.definitions.units.is_housing[unit.type.value] == 0) {
      continue;
    }
    return current.units.row_ids[row];
  }
  return UnitId{};
}

}  // namespace core
