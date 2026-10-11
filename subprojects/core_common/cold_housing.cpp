// The works that will house a herd facing the cold (core_common/cold_housing.h).

#include "core_common/cold_housing.h"

#include <cstddef>
#include <cstdint>

#include "core_common/herd_state.h"
#include "core_common/unit_state.h"

namespace core {
namespace {

bool WarmRung(const ColdHousing& cold, UnitTypeId type, std::uint32_t level) {
  if (type.value >= cold.warm_by_type_level.size()) {
    return false;
  }
  const std::vector<std::uint8_t>& rungs = cold.warm_by_type_level[type.value];
  return level < rungs.size() && rungs[level] != 0;
}

}  // namespace

bool WorksHouseAColdHerd(const ColdHousing& cold, const WorldState& world, std::uint32_t unit_row) {
  if (unit_row >= world.units.rows.size()) {
    return false;
  }
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  if (month < kColdHousingFirstMonth && month > kColdHousingLastMonth) {
    return false;
  }
  const UnitRow& works = world.units.rows[unit_row];
  if (works.dead != 0 || works.construction.phase == ConstructionPhase::kNone ||
      works.construction.phase == ConstructionPhase::kDemolishing) {
    return false;
  }
  // A kind homed here that freezes in a cold place, with a kolkhoz herd of it
  // standing.
  bool cold_herd = false;
  for (const HerdRow& herd : world.herds.rows) {
    const std::size_t kind = herd.kind.value;
    if (herd.household_owned != 0 || kind >= cold.home_by_kind.size() ||
        kind >= cold.freezes_by_kind.size() || cold.freezes_by_kind[kind] == 0 ||
        cold.home_by_kind[kind].value != works.type.value) {
      continue;
    }
    cold_herd = cold_herd || herd.adult_count + herd.juvenile_count + herd.newborn_count > 0;
  }
  if (!cold_herd) {
    return false;
  }
  // And no unit of that home warm yet — by its rung or by a straw coat.
  for (const UnitRow& unit : world.units.rows) {
    if (unit.dead == 0 && unit.type.value == works.type.value &&
        (WarmRung(cold, unit.type, unit.level) || unit.insulated != 0)) {
      return false;
    }
  }
  return true;
}

}  // namespace core
