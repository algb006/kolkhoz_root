/// @file
/// @brief The graves and the game's stop for one (burial.h).

#include "burial.h"

#include <array>
#include <limits>

#include "core_catalog/table_value.h"
#include "core_common/emit_event.h"
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
  return CemeteryCapacityGraves(plot_radius_m[unit.type.value], config.m2_per_grave);
}

void LayGraveOf(const BurialConfig& config,
                std::span<const float> plot_radius_m,
                WorldState& current,
                ResidentId dead,
                const Vec2* home) {
  std::uint32_t best = kNoRow;
  float best_d2 = std::numeric_limits<float>::max();
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const UnitRow& unit = current.units.rows[row];
    if (!TakesAGrave(config, plot_radius_m, unit)) {
      continue;
    }
    if (home == nullptr) {
      best = row;  // no house to be near: the first by row
      break;
    }
    const float dx = unit.position.x - home->x;
    const float dy = unit.position.y - home->y;
    const float d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      best = row;
    }
  }
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
  for (std::uint32_t row = 0; row < current.units.rows.size() && current.burials_waiting > 0;
       ++row) {
    UnitRow& unit = current.units.rows[row];
    const std::uint16_t capacity = GraveCapacityOf(config, plot_radius_m, unit);
    if (unit.graves >= capacity) {
      continue;
    }
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
