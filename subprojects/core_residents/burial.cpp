/// @file
/// @brief The graves and the game's stop for one (burial.h).

#include "burial.h"

#include <array>
#include <limits>

#include "core_catalog/table_value.h"
#include "core_common/emit_event.h"
#include "core_common/plot.h"
#include "core_common/state_table_ops.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 1> kBurialWorldParamKeys = {"cemetery_m2_per_grave"};

/// A cemetery that may take a grave: built (a marked plot is string and
/// pegs), of the cemetery type, with room.
bool TakesAGrave(const BurialConfig& config,
                 std::span<const float> plot_radius_m,
                 const UnitRow& unit) {
  return unit.graves < GraveCapacityOf(config, plot_radius_m, unit);
}

/// THE OLDEST CEMETERY WITH ROOM, by its id (an id is never reused, so the
/// smallest is the earliest marked; the row order is not an age, a removal
/// moves rows). kNoRow when none has room. boss [33], (α): «новое
/// размечается, когда старое кончится» - the old ground is used to the end.
std::uint32_t OldestWithRoom(const BurialConfig& config,
                             std::span<const float> plot_radius_m,
                             const WorldState& world) {
  std::uint32_t best = kNoRow;
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    if (!TakesAGrave(config, plot_radius_m, world.units.rows[row])) {
      continue;
    }
    if (best == kNoRow || world.units.row_ids[row].value < world.units.row_ids[best].value) {
      best = row;
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
  const std::uint32_t best = OldestWithRoom(config, plot_radius_m, current);
  if (best != kNoRow) {
    ++current.units.rows[best].graves;
    return;
  }
  // FULL EVERYWHERE: the game stops (social.md). The grave waits; it is not
  // lost, and it is laid the first day a cemetery has room.
  if (current.burials_waiting < std::numeric_limits<std::uint16_t>::max()) {
    ++current.burials_waiting;
  }
  SimEvent& held = EmitEvent(current, EventKind::kBurialHeld, EventSeverity::kInterrupting);
  held.resident = dead;
  held.amount = current.burials_waiting;
}

void LayWaitingGraves(const BurialConfig& config,
                      std::span<const float> plot_radius_m,
                      WorldState& current) {
  // The oldest with room first, as a single grave is laid (OldestWithRoom).
  while (current.burials_waiting > 0) {
    const std::uint32_t row = OldestWithRoom(config, plot_radius_m, current);
    if (row == kNoRow) {
      return;
    }
    UnitRow& unit = current.units.rows[row];
    const std::uint16_t capacity = GraveCapacityOf(config, plot_radius_m, unit);
    const auto room = static_cast<std::uint16_t>(capacity - unit.graves);
    const std::uint16_t laid = room < current.burials_waiting ? room : current.burials_waiting;
    unit.graves = static_cast<std::uint16_t>(unit.graves + laid);
    current.burials_waiting = static_cast<std::uint16_t>(current.burials_waiting - laid);
    SimEvent& event = EmitEvent(current, EventKind::kBurialsLaid, EventSeverity::kNotable);
    event.unit = current.units.row_ids[row];
    event.amount = laid;
  }
}

}  // namespace core
