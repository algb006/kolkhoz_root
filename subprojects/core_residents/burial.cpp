/// @file
/// @brief The graves and the game's stop for one (burial.h).

#include "burial.h"

#include <array>
#include <limits>

#include "core_catalog/table_value.h"
#include "core_common/emit_event.h"
#include "core_common/plot.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 1> kBurialWorldParamKeys = {"cemetery_m2_per_grave"};

/// A cemetery that takes a grave: built (a marked plot is string and pegs) and
/// of the cemetery type. ITS ROOM IS NOT ASKED (#1367; social.md «Ёмкость не
/// ограничена», the human's word of 1 November 2026, #1564): a full one's rows
/// grow denser.
bool TakesAGrave(const BurialConfig& config, const UnitRow& unit) {
  return config.cemetery_type.value != kInvalidDefIdValue &&
         unit.type.value == config.cemetery_type.value && unit.level > 0;
}

/// THE OLDEST BUILT CEMETERY, by its id (an id is never reused): where the
/// waiting graves of a world that had none go once one stands. kNoRow when
/// none is built.
std::uint32_t OldestCemetery(const BurialConfig& config, const WorldState& world) {
  std::uint32_t best = kNoRow;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (!TakesAGrave(config, world.units.rows[row])) {
      continue;
    }
    if (best == kNoRow || world.units.row_ids[row].value < world.units.row_ids[best].value) {
      best = row;
    }
  }
  return best;
}

/// THE NEAREST CEMETERY BY ROAD OF THE DEAD'S OWN SETTLEMENT (#1367; social.md
/// «Где хоронят»: «На ближайшем по дороге кладбище своего селения»; replaces
/// 0.37.317's «oldest with room»): the walk from his yard (RoadKm, on foot) to
/// each built cemetery whose settlement is his yard's. None of his settlement:
/// the nearest of any. A dead with no yard: the oldest. Ties by id. kNoRow when
/// none is built.
std::uint32_t NearestCemetery(const BurialConfig& config,
                              const WorldState& world,
                              ResidentId dead) {
  const std::uint32_t person = FindRow(world.residents, dead);
  Vec2 home;
  if (person == kNoRow || !HomePositionOf(world, world.residents.rows[person].family, home)) {
    return OldestCemetery(config, world);
  }
  SettlementId settlement;
  const std::uint32_t family = FindRow(world.families, world.residents.rows[person].family);
  if (family != kNoRow) {
    const std::uint32_t house = FindRow(world.units, world.families.rows[family].house);
    if (house != kNoRow) {
      settlement = world.units.rows[house].settlement;
    }
  }
  std::uint32_t best = kNoRow;
  float best_km = 0.0F;
  bool best_own = false;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (!TakesAGrave(config, unit)) {
      continue;
    }
    const bool own =
        settlement.value != kInvalidEntityIdValue && unit.settlement.value == settlement.value;
    const float km = RoadKm(world, TravelMode::kWalk, home, unit.position);
    const bool better = best == kNoRow || (own && !best_own) ||
                        (own == best_own &&
                         (km < best_km || (km == best_km && world.units.row_ids[row].value <
                                                                world.units.row_ids[best].value)));
    if (better) {
      best = row;
      best_km = km;
      best_own = own;
    }
  }
  return best;
}

}  // namespace

std::span<const std::string_view> BurialWorldParamKeys() {
  return kBurialWorldParamKeys;
}

bool ParseBurialConfig(const ITableSet& tables, BurialConfig& config, std::string& error) {
  if (const ITable* unit_types = tables.FindTable("unit_types")) {
    config.cemetery_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey(kCemeteryTypeKey));
  }
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  const std::array<ScalarKnob, kBurialWorldParamKeys.size()> knobs = {{
      {.key = kBurialWorldParamKeys[0],
       .value = &config.m2_per_grave,
       .range = {.low = kCemeteryM2PerGraveMin, .high = kCemeteryM2PerGraveMax}},
  }};
  return ReadKnobs(*world, "world_params", knobs, error);
}

std::uint16_t GraveCapacityOf(const BurialConfig& config,
                              std::span<const float> plot_radius_m,
                              const UnitRow& unit) {
  if (config.cemetery_type.value == kInvalidDefIdValue ||
      unit.type.value != config.cemetery_type.value || unit.level == 0 ||
      unit.type.value >= plot_radius_m.size()) {
    return 0;
  }
  // Its own plot where it has one (the old cemetery's 14 m; plot.h).
  return CemeteryCapacityGraves(PlotRadiusOf(unit, plot_radius_m), config.m2_per_grave);
}

void LayGraveOf(const BurialConfig& config,
                std::span<const float> plot_radius_m,
                WorldState& current,
                ResidentId dead) {
  (void)plot_radius_m;  // the room is not asked since #1367; the fill reads it
  const std::uint32_t best = NearestCemetery(config, current, dead);
  if (best != kNoRow) {
    UnitRow& cemetery = current.units.rows[best];
    if (cemetery.graves < std::numeric_limits<std::uint16_t>::max()) {
      ++cemetery.graves;
    }
    return;
  }
  // NO CEMETERY BUILT AT ALL - a world with none, since the start's old one is
  // never taken down (it holds graves: construction_system.cpp, Demolish). The
  // grave waits and is laid when one stands. NO STOP (#1367, #1564: «Остановки
  // игры нет»): the event is notable, not interrupting, and BurialHeld is
  // always false.
  if (current.burials_waiting < std::numeric_limits<std::uint16_t>::max()) {
    ++current.burials_waiting;
  }
  SimEvent& held = EmitEvent(current, EventKind::kBurialHeld, EventSeverity::kNotable);
  held.resident = dead;
  held.amount = current.burials_waiting;
}

void LayWaitingGraves(const BurialConfig& config,
                      std::span<const float> plot_radius_m,
                      WorldState& current) {
  // All in the oldest built cemetery, the first that stands (#1367: no room
  // asked; the waiting are the dead of a world with no cemetery at all).
  (void)plot_radius_m;
  if (current.burials_waiting == 0) {
    return;
  }
  const std::uint32_t row = OldestCemetery(config, current);
  if (row == kNoRow) {
    return;
  }
  UnitRow& unit = current.units.rows[row];
  const std::uint16_t laid = current.burials_waiting;
  const std::uint32_t total = static_cast<std::uint32_t>(unit.graves) + laid;
  unit.graves = total > std::numeric_limits<std::uint16_t>::max()
                    ? std::numeric_limits<std::uint16_t>::max()
                    : static_cast<std::uint16_t>(total);
  current.burials_waiting = 0;
  SimEvent& event = EmitEvent(current, EventKind::kBurialsLaid, EventSeverity::kNotable);
  event.unit = current.units.row_ids[row];
  event.amount = laid;
}

}  // namespace core
