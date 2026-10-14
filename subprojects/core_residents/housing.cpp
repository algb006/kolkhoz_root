// The free house a household moves into (housing.h).

#include "housing.h"

#include <cstdint>
#include <limits>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/family_state.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/turned_away_state.h"
#include "core_common/unit_state.h"

namespace core {
namespace {

/// A house no household lives in, of the housing class, with no residents'
/// capacity (a family's house, not a dormitory) — and on a rung somebody may
/// live on: the priest's house on its first rung is the chairman's office
/// (unit_levels.csv no_residents; boss, the logistics thread [22]). Until
/// 0.37.178 the first wedding of year 1 moved into it.
bool IsFreeHouse(const LifeConfig& config, const UnitRow& unit) {
  return unit.household.value == kInvalidEntityIdValue && unit.level != 0 &&
         unit.type.value < config.definitions.units.is_housing.size() &&
         config.definitions.units.is_housing[unit.type.value] != 0 &&
         !config.definitions.units.HousesNobody(unit.type, unit.level) &&
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

namespace {

/// People living in each unit (by unit row), by the families whose house it is.
std::vector<std::uint32_t> LivingByUnit(const WorldState& current) {
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
  return living;
}

}  // namespace

std::vector<bool> CrowdedBarracks(const LifeConfig& config, const WorldState& current) {
  const std::vector<std::uint32_t> living = LivingByUnit(current);
  std::vector<bool> crowded(current.units.rows.size(), false);
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const float capacity = ResidentsCapacity(config, current.units.rows[row]);
    crowded[row] = capacity > 0.0F && static_cast<float>(living[row]) > capacity;
  }
  return crowded;
}

std::uint32_t FamiliesInCrowdedBarracks(const LifeConfig& config, const WorldState& current) {
  const std::vector<bool> crowded = CrowdedBarracks(config, current);
  std::uint32_t families = 0;
  for (const FamilyRow& family : current.families.rows) {
    const std::uint32_t unit_row = FindRow(current.units, family.house);
    families += family.in_barrack != 0 && unit_row != kNoRow && crowded[unit_row] ? 1U : 0U;
  }
  return families;
}

UnitId BarrackPlace(const LifeConfig& config, const WorldState& current, std::uint32_t people) {
  const std::vector<std::uint32_t> living = LivingByUnit(current);
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const float capacity = ResidentsCapacity(config, current.units.rows[row]);
    if (capacity > 0.0F && static_cast<float>(living[row] + people) <= capacity) {
      return current.units.row_ids[row];
    }
  }
  return UnitId{};
}

bool HouseOnTheBrink(const LifeConfig& config, const UnitRow& unit) {
  return OnTheBrink(config, unit);
}

bool HousingShortAlarm(const LifeConfig& config, const WorldState& world, Alarm& alarm) {
  const SimDay today = world.calendar.day;
  // (a) THE COUPLES WAITING FOR A HOUSE: the oldest's wait, and how many.
  const auto couples = static_cast<std::uint32_t>(world.wedding_waits.rows.size());
  std::uint32_t oldest_row = kNoRow;
  for (std::uint32_t row = 0; row < couples; ++row) {
    if (oldest_row == kNoRow ||
        world.wedding_waits.rows[row].since_day < world.wedding_waits.rows[oldest_row].since_day) {
      oldest_row = row;
    }
  }
  // «ТЕСНЫЙ БАРАК» (demography design; boss 11 October 2026, core's
  // barrack probe: of 468 residents over the places 462 were born there): a
  // family in a barrack over its places asks for a house as a waiting couple
  // does - (a) is lit at kHousingCrowdedBarrackFamilies of them.
  const std::uint32_t crowded = FamiliesInCrowdedBarracks(config, world);
  const bool couples_wait =
      couples >= kHousingShortCouples || crowded >= kHousingCrowdedBarrackFamilies ||
      (oldest_row != kNoRow &&
       today - world.wedding_waits.rows[oldest_row].since_day >= kHousingShortCoupleDays);
  // (b) A FAMILY'S HOUSE ON THE BRINK, with nowhere to move: no free house
  // and no house site open in the village.
  std::uint32_t on_the_brink = 0;
  FamilyId first_on_the_brink;
  bool house_site_open = false;
  for (const UnitRow& unit : world.units.rows) {
    const bool housing = unit.type.value < config.definitions.units.is_housing.size() &&
                         config.definitions.units.is_housing[unit.type.value] != 0;
    if (!housing) {
      continue;
    }
    house_site_open = house_site_open || (unit.level == 0 && unit.dead == 0 &&
                                          unit.construction.phase != ConstructionPhase::kNone);
    if (unit.household.value != kInvalidEntityIdValue && OnTheBrink(config, unit)) {
      first_on_the_brink = on_the_brink == 0 ? unit.household : first_on_the_brink;
      ++on_the_brink;
    }
  }
  const bool brink_with_nowhere =
      on_the_brink > 0 && !house_site_open &&
      FreeHouseNotOnTheBrink(config, world).value == kInvalidEntityIdValue;
  // (c) THE MIGRANTS TURNED AWAY in the year (the design's «newcomers wait»)
  // — AND NO ROOF FOR A NEWCOMER TODAY. The year's count alone kept the lamp
  // lit up to a year after a barrack was built: the canon's pair (task 433,
  // econ housing-pair-reading-2026-10-11) showed it lit in 1934 y2 and 1935,
  // 1936 y3 with places free and nobody waiting (boss, 11 October 2026).
  const std::uint32_t turned_away = TurnedAwayOfYear(world.arrivals_turned_away, today);
  bool shared = false;
  const bool newcomers_wait =
      turned_away >= kHousingShortTurnedAway &&
      HomeForNewcomers(config, world, 1, shared).value == kInvalidEntityIdValue;
  if (!couples_wait && !brink_with_nowhere && !newcomers_wait) {
    return false;
  }
  alarm = Alarm{};
  alarm.kind = AlarmKind::kHousingShort;
  if (couples_wait && oldest_row != kNoRow) {
    const std::uint32_t bride =
        FindRow(world.residents, world.wedding_waits.rows[oldest_row].bride);
    alarm.family = bride != kNoRow ? world.residents.rows[bride].family : FamilyId{};
  } else if (brink_with_nowhere) {
    alarm.family = first_on_the_brink;
  }
  alarm.amount = static_cast<std::int64_t>(couples) + crowded + on_the_brink;
  alarm.amount_more = turned_away;
  alarm.lamp = 1;
  alarm.days_to_loss = DaysToLossOf(std::numeric_limits<std::int64_t>::max());
  return true;
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
