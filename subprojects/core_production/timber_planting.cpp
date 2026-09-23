// Planting a forest by zone (timber_planting.h).

#include "timber_planting.h"

#include <cmath>
#include <cstdint>
#include <numbers>

#include "core_catalog/timber_catalog.h"
#include "core_common/emit_event.h"
#include "core_common/geometry.h"
#include "core_common/plot.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"

namespace core {
namespace {

/// A planting's stand row made from no table row: the felling's DefOf finds
/// nothing there, and PlantedStandDef answers instead.
constexpr std::uint32_t kNoTableRow32 = 0xFFFFFFFFU;

/// Straight-line metres between two points of the map.
float DistanceMeters(Vec2 from, Vec2 to) {
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  return std::sqrt((dx * dx) + (dy * dy));
}

/// The radius of a round contour of `hectares`, metres. A zone, a field and
/// a stand are all asked as circles of their area: the core keeps no outline
/// for any of them.
float RadiusOfHectares(float hectares) {
  return hectares > 0.0F ? std::sqrt(hectares * 10000.0F / std::numbers::pi_v<float>) : 0.0F;
}

/// The species' planting row, or nullptr for one that is not in the roster
/// or does not plant (plant_years_to_logs blank: boss seq 17).
const PlantableSpecies* PlantingOf(const ProductionConfig& config, TreeSpeciesId species) {
  if (species.value >= config.timber.species.size()) {
    return nullptr;
  }
  const PlantableSpecies& row = config.timber.species[species.value];
  return row.years_to_logs > 0.0F ? &row : nullptr;
}

/// A stand's contour area, hectares: a planting's own, else its table row's.
float StandHectares(const ProductionConfig& config, const TimberStandRow& stand) {
  if (stand.kind == TimberStandKind::kPlanted) {
    return stand.planted_area_ha;
  }
  return stand.table_row < config.timber.stands.size()
             ? config.timber.stands[stand.table_row].area_ha
             : 0.0F;
}

/// A grove, belt or grown planting felled to nothing, with nothing marked and
/// no load lying: ground a planting may be put on again.
bool FelledToNothing(const TimberStandRow& stand, std::uint32_t today) {
  // A planting counts only once it has GROWN and been felled: a young one
  // holds nothing too, and is not ground to plant over.
  const bool grown_planting = stand.kind == TimberStandKind::kPlanted &&
                              stand.matures_day != kNeverPlanted && today >= stand.matures_day;
  const bool fellable_kind = stand.kind == TimberStandKind::kGrove ||
                             stand.kind == TimberStandKind::kShelterbelt || grown_planting;
  return fellable_kind && !(stand.stock_m3 > 0.0F) && !(stand.marked_m3 > 0.0F) &&
         stand.load_grams <= 0;
}

/// Why a new zone at `place` of `hectares` may not stand there: kWrongLand
/// on a field, kTooClose on a unit's plot or another stand's contour.
OrderRefusal ZoneRefusal(const ProductionConfig& config,
                         const WorldState& world,
                         Vec2 place,
                         float hectares) {
  const float radius = RadiusOfHectares(hectares);
  for (const FieldRow& field : world.fields.rows) {
    if (DistanceMeters(place, field.center) < radius + RadiusOfHectares(field.area_ga)) {
      return OrderRefusal::kWrongLand;
    }
  }
  const PlotRules rules{.radius_by_type = config.plot_radius_m, .map_side_m = config.map_side_m};
  if (PlotOverlaps(world.units, rules, place, radius, UnitId{})) {
    return OrderRefusal::kTooClose;
  }
  for (const TimberStandRow& stand : world.stands.rows) {
    if (DistanceMeters(place, stand.position) <
        radius + RadiusOfHectares(StandHectares(config, stand))) {
      return OrderRefusal::kTooClose;
    }
  }
  return OrderRefusal::kNone;
}

void OpenPlanting(const ProductionConfig& config,
                  TimberStandRow& stand,
                  TreeSpeciesId species,
                  float hectares) {
  stand.kind = TimberStandKind::kPlanted;
  stand.species = species;
  stand.planted_area_ha = hectares;
  stand.stock_m3 = 0.0F;
  stand.marked_m3 = 0.0F;
  stand.work_days_remaining = hectares * config.timber.planting_days_per_ha;
  stand.planted_day = kNeverPlanted;
  stand.matures_day = kNeverPlanted;
}

}  // namespace

OrderRefusal OrderPlantForest(const ProductionConfig& config,
                              WorldState& current,
                              const OrderRow& order) {
  if (order.species.value >= config.timber.species.size()) {
    return OrderRefusal::kNoSuchSubject;
  }
  if (PlantingOf(config, order.species) == nullptr) {
    return OrderRefusal::kNotEligible;  // a species that does not plant
  }
  if (!(order.area_ha > 0.0F) || order.area_ha > config.timber.planting_max_ha) {
    return OrderRefusal::kRuleForbids;
  }
  // THE FIRST FORM: a grove or belt felled to nothing, planted in its row.
  if (order.stand.value != kInvalidEntityIdValue) {
    const std::uint32_t row = FindRow(current.stands, order.stand);
    if (row == kNoRow) {
      return OrderRefusal::kNoSuchSubject;
    }
    TimberStandRow& stand = current.stands.rows[row];
    if (!FelledToNothing(stand, static_cast<std::uint32_t>(current.calendar.day))) {
      return OrderRefusal::kNotEligible;
    }
    if (order.area_ha > StandHectares(config, stand)) {
      return OrderRefusal::kRuleForbids;  // no more than its own ground
    }
    OpenPlanting(config, stand, order.species, order.area_ha);
    return OrderRefusal::kNone;
  }
  // THE SECOND FORM: a new zone at a position, outside the fields.
  // THE WHOLE ZONE ON THE MAP, as the plot rule asks of a plot (plot.cpp,
  // OnTheMap) — not its centre only (the static loop of 0.34.35). And a
  // position that is not a number is off every map.
  const float radius = RadiusOfHectares(order.area_ha);
  if (!std::isfinite(order.position.x) || !std::isfinite(order.position.y)) {
    return OrderRefusal::kRuleForbids;
  }
  if (config.map_side_m > 0.0F &&
      !(order.position.x - radius >= 0.0F && order.position.x + radius <= config.map_side_m &&
        order.position.y - radius >= 0.0F && order.position.y + radius <= config.map_side_m)) {
    return OrderRefusal::kRuleForbids;
  }
  const OrderRefusal place = ZoneRefusal(config, current, order.position, order.area_ha);
  if (place != OrderRefusal::kNone) {
    return place;
  }
  TimberStandRow zone;
  zone.table_row = kNoTableRow32;
  zone.position = order.position;
  OpenPlanting(config, zone, order.species, order.area_ha);
  AppendRow(current.stands, zone);
  return OrderRefusal::kNone;
}

void FinishPlantings(const ProductionConfig& config, WorldState& current) {
  const auto today = static_cast<std::uint32_t>(current.calendar.day);
  for (std::uint32_t row = 0; row < current.stands.rows.size(); ++row) {
    TimberStandRow& stand = current.stands.rows[row];
    if (stand.kind != TimberStandKind::kPlanted || stand.planted_day != kNeverPlanted ||
        stand.work_days_remaining > 0.0F) {
      continue;
    }
    const PlantableSpecies* const species = PlantingOf(config, stand.species);
    if (species == nullptr) {
      continue;  // a species the roster no longer plants: it stands unplanted
    }
    stand.work_days_remaining = 0.0F;
    stand.planted_day = today;
    const float grow_days = species->years_to_logs * static_cast<float>(kDaysPerYear);
    stand.matures_day = today + static_cast<std::uint32_t>(std::lround(grow_days));
    SimEvent& event = EmitEvent(current, EventKind::kForestPlanted, EventSeverity::kNotable);
    event.stand = current.stands.row_ids[row];
    event.amount = std::lround(stand.planted_area_ha * 100.0F);
  }
}

void GrowPlantings(const ProductionConfig& config, WorldState& current) {
  const auto today = static_cast<std::uint32_t>(current.calendar.day);
  for (std::uint32_t row = 0; row < current.stands.rows.size(); ++row) {
    TimberStandRow& stand = current.stands.rows[row];
    if (stand.kind != TimberStandKind::kPlanted || stand.matures_day == kNeverPlanted ||
        today != stand.matures_day) {
      continue;
    }
    const PlantableSpecies* const species = PlantingOf(config, stand.species);
    if (species == nullptr) {
      continue;
    }
    stand.stock_m3 = stand.planted_area_ha * species->m3_per_ha;
    SimEvent& event = EmitEvent(current, EventKind::kPlantingMatured, EventSeverity::kNotable);
    event.stand = current.stands.row_ids[row];
    event.amount = std::lround(stand.stock_m3);
  }
}

bool PlantedStandDef(const ProductionConfig& config,
                     const TimberStandRow& stand,
                     TimberStandDef& def) {
  if (stand.kind != TimberStandKind::kPlanted) {
    return false;
  }
  const PlantableSpecies* const species = PlantingOf(config, stand.species);
  if (species == nullptr) {
    return false;
  }
  def.kind = TimberStandKind::kPlanted;
  def.position = stand.position;
  def.area_ha = stand.planted_area_ha;
  def.log_share = species->log_share;
  return true;
}

}  // namespace core
