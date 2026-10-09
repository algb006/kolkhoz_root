// Marking a meadow (meadow_mark.h): the answer and the act.
#include "core_common/meadow_mark.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core_common/emit_event.h"
#include "core_common/obstacle_raster.h"
#include "core_common/plot.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core {
namespace {

constexpr float kSquareMetresInHectare = 10000.0F;
constexpr float kPi = 3.14159265F;

float RadiusOf(float hectares) {
  return hectares > 0.0F ? std::sqrt(hectares * kSquareMetresInHectare / kPi) : 0.0F;
}

float Distance(Vec2 a, Vec2 b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

float DistanceToEdge(Vec2 point, Vec2 from, Vec2 to) {
  const float span_x = to.x - from.x;
  const float span_y = to.y - from.y;
  const float length_squared = (span_x * span_x) + (span_y * span_y);
  float along = 0.0F;
  if (length_squared > 0.0F) {
    along =
        std::clamp((((point.x - from.x) * span_x) + ((point.y - from.y) * span_y)) / length_squared,
                   0.0F,
                   1.0F);
  }
  return Distance(point, Vec2{.x = from.x + (span_x * along), .y = from.y + (span_y * along)});
}

bool InsideOutline(Vec2 point, const std::vector<Vec2>& outline) {
  bool inside = false;
  for (std::size_t index = 0, previous = outline.size() - 1; index < outline.size();
       previous = index++) {
    const Vec2 a = outline[index];
    const Vec2 b = outline[previous];
    if ((a.y > point.y) != (b.y > point.y) &&
        point.x < a.x + ((point.y - a.y) * (b.x - a.x) / (b.y - a.y))) {
      inside = !inside;
    }
  }
  return inside;
}

/// What the raster says of a mark's circle: how many of its cells there are,
/// how many lie on the floodplain, and the first cell of each obstacle.
struct CircleOnMap {
  std::uint32_t cells = 0;
  std::uint32_t flooded = 0;
  std::uint8_t found = 0;  ///< The obstacle flags met.
  Vec2 water{};
  Vec2 river{};
  Vec2 forest{};
  Vec2 trees{};
  Vec2 reserve{};
  Vec2 ruins{};
};

void Note(CircleOnMap& seen, std::uint8_t flags, std::uint8_t flag, Vec2& where, Vec2 cell) {
  if ((flags & flag) != 0 && (seen.found & flag) == 0) {
    seen.found = static_cast<std::uint8_t>(seen.found | flag);
    where = cell;
  }
}

CircleOnMap ReadCircle(const ObstacleRaster& raster, Vec2 centre, float radius_m) {
  CircleOnMap seen;
  const float cell = raster.CellMetres();
  // Cell CENTRES inside the circle, south to north and west to east — the
  // same centres the raster was filled by, so a share is a share of cells.
  const auto first = [cell](float metres) { return (std::floor(metres / cell) + 0.5F) * cell; };
  for (float y = first(centre.y - radius_m); y <= centre.y + radius_m; y += cell) {
    for (float x = first(centre.x - radius_m); x <= centre.x + radius_m; x += cell) {
      const Vec2 point{.x = x, .y = y};
      if (Distance(point, centre) > radius_m) {
        continue;
      }
      ++seen.cells;
      const std::uint8_t flags = raster.FlagsAt(point);
      if (flags == 0) {
        continue;
      }
      seen.flooded += (flags & kObstacleFloodplain) != 0 ? 1U : 0U;
      Note(seen, flags, kObstacleWater, seen.water, point);
      Note(seen, flags, kObstacleRiver, seen.river, point);
      Note(seen, flags, kObstacleForest, seen.forest, point);
      Note(seen, flags, kObstacleTrees, seen.trees, point);
      Note(seen, flags, kObstacleReserve, seen.reserve, point);
      Note(seen, flags, kObstacleRuins, seen.ruins, point);
    }
  }
  return seen;
}

MeadowMarkAnswer Refused(MeadowMarkRefusal refusal, Vec2 at, float flood_share = 0.0F) {
  return MeadowMarkAnswer{.refusal = refusal, .flood_share = flood_share, .at = at};
}

/// Form 2: the named arable field mown as it lies.
MeadowMarkAnswer PreviewFallow(const MeadowMarkGround& ground,
                               const WorldState& world,
                               FieldId field_id) {
  const std::uint32_t row = FindRow(world.fields, field_id);
  if (row == kNoRow) {
    return Refused(MeadowMarkRefusal::kNoSuchField, Vec2{});
  }
  const FieldRow& field = world.fields.rows[row];
  if (field.mown_fallow != 0) {
    return Refused(MeadowMarkRefusal::kAlreadyMown, field.center);
  }
  // Idle ground only: a field with a chain is worked, and one whose furrow,
  // crop or heap of a chain just taken back still stands is not fallow yet.
  if (field.kind != LandKind::kArable || HasRotation(field) || field.phase != FieldPhase::kIdle ||
      field.reaped_grams > 0) {
    return Refused(MeadowMarkRefusal::kNotFallow, field.center);
  }
  return MeadowMarkAnswer{.refusal = MeadowMarkRefusal::kNone,
                          .kind = LandKind::kMeadow,
                          .flood_share = 0.0F,
                          .hay_kg_a_cut = field.area_ga * ground.dry_yield_kg_per_ha,
                          .mow_man_days = field.area_ga * ground.mow_days_per_ha,
                          .at = field.center};
}

}  // namespace

bool CircleTouchesPolygon(Vec2 centre, float radius_m, const std::vector<Vec2>& outline) {
  if (outline.size() < 3) {
    return false;
  }
  if (InsideOutline(centre, outline)) {
    return true;
  }
  for (std::size_t index = 0, previous = outline.size() - 1; index < outline.size();
       previous = index++) {
    // Strict, as the plots' rule is: shapes that only touch do not overlap.
    if (DistanceToEdge(centre, outline[previous], outline[index]) < radius_m) {
      return true;
    }
  }
  return false;
}

bool CircleTouchesLand(const MeadowMarkGround& ground,
                       const FieldRow& land,
                       Vec2 centre,
                       float radius_m) {
  if (land.start_shape != 0 && ground.start_land != nullptr &&
      land.start_shape <= ground.start_land->size()) {
    return CircleTouchesPolygon(
        centre, radius_m, (*ground.start_land)[land.start_shape - 1U].outline);
  }
  return Distance(centre, land.center) < radius_m + RadiusOf(land.area_ga);
}

MeadowMarkAnswer PreviewMeadowMark(const MeadowMarkGround& ground,
                                   const WorldState& world,
                                   Vec2 position,
                                   float area_ha,
                                   FieldId field) {
  if (field.value != kInvalidEntityIdValue) {
    return PreviewFallow(ground, world, field);
  }
  if (!(area_ha > 0.0F)) {
    return Refused(MeadowMarkRefusal::kBadArea, position);
  }
  const float radius = RadiusOf(area_ha);
  if (ground.map_side_m > 0.0F &&
      (position.x - radius < 0.0F || position.y - radius < 0.0F ||
       position.x + radius > ground.map_side_m || position.y + radius > ground.map_side_m)) {
    return Refused(MeadowMarkRefusal::kOutsideMap, position);
  }
  float flood_share = 0.0F;
  if (ground.raster != nullptr) {
    const CircleOnMap seen = ReadCircle(*ground.raster, position, radius);
    flood_share =
        seen.cells > 0 ? static_cast<float>(seen.flooded) / static_cast<float>(seen.cells) : 0.0F;
    // One reason, in a fixed order, so the same mark is always refused by
    // the same word whichever cell the scan met first.
    if ((seen.found & kObstacleWater) != 0) {
      return Refused(MeadowMarkRefusal::kWater, seen.water, flood_share);
    }
    if ((seen.found & kObstacleRiver) != 0) {
      return Refused(MeadowMarkRefusal::kRiver, seen.river, flood_share);
    }
    if ((seen.found & kObstacleForest) != 0) {
      return Refused(MeadowMarkRefusal::kForest, seen.forest, flood_share);
    }
    if ((seen.found & kObstacleTrees) != 0) {
      return Refused(MeadowMarkRefusal::kTrees, seen.trees, flood_share);
    }
    if ((seen.found & kObstacleReserve) != 0) {
      return Refused(MeadowMarkRefusal::kReserve, seen.reserve, flood_share);
    }
    if ((seen.found & kObstacleRuins) != 0) {
      return Refused(MeadowMarkRefusal::kRuins, seen.ruins, flood_share);
    }
  }
  if (ground.pits != nullptr) {
    for (const std::vector<Vec2>& pit : *ground.pits) {
      if (CircleTouchesPolygon(position, radius, pit)) {
        return Refused(MeadowMarkRefusal::kPit, pit.front(), flood_share);
      }
    }
  }
  if (flood_share > kDryShareMax && flood_share < kFloodplainShareMin) {
    return Refused(MeadowMarkRefusal::kMixedFloodplain, position, flood_share);
  }
  const PlotRules rules{.radius_by_type = ground.plot_radius_m, .map_side_m = ground.map_side_m};
  if (PlotOverlaps(world.units, rules, position, radius, UnitId{})) {
    return Refused(MeadowMarkRefusal::kUnit, position, flood_share);
  }
  for (std::size_t row = 0; row < world.stands.rows.size(); ++row) {
    if (ground.stand_radius_m == nullptr || row >= ground.stand_radius_m->size()) {
      break;
    }
    const Vec2 stand = world.stands.rows[row].position;
    if (Distance(position, stand) < radius + (*ground.stand_radius_m)[row]) {
      return Refused(MeadowMarkRefusal::kStand, stand, flood_share);
    }
  }
  for (const FieldRow& other : world.fields.rows) {
    if (CircleTouchesLand(ground, other, position, radius)) {
      return Refused(MeadowMarkRefusal::kLand, other.center, flood_share);
    }
  }
  const bool floodplain = flood_share >= kFloodplainShareMin;
  return MeadowMarkAnswer{.refusal = MeadowMarkRefusal::kNone,
                          .kind = floodplain ? LandKind::kFloodplainMeadow : LandKind::kMeadow,
                          .flood_share = flood_share,
                          .hay_kg_a_cut = area_ha * (floodplain ? ground.floodplain_yield_kg_per_ha
                                                                : ground.dry_yield_kg_per_ha),
                          .mow_man_days = area_ha * ground.mow_days_per_ha,
                          .at = position};
}

OrderRefusal OrderRefusalOf(MeadowMarkRefusal refusal) {
  switch (refusal) {
    case MeadowMarkRefusal::kNone:
      return OrderRefusal::kNone;
    case MeadowMarkRefusal::kBadArea:
    case MeadowMarkRefusal::kOutsideMap:
      return OrderRefusal::kRuleForbids;
    case MeadowMarkRefusal::kWater:
    case MeadowMarkRefusal::kRiver:
    case MeadowMarkRefusal::kForest:
    case MeadowMarkRefusal::kTrees:
    case MeadowMarkRefusal::kReserve:
    case MeadowMarkRefusal::kRuins:
    case MeadowMarkRefusal::kPit:
    case MeadowMarkRefusal::kMixedFloodplain:
    case MeadowMarkRefusal::kNotFallow:
      return OrderRefusal::kWrongLand;
    case MeadowMarkRefusal::kUnit:
    case MeadowMarkRefusal::kStand:
    case MeadowMarkRefusal::kLand:
      return OrderRefusal::kTooClose;
    case MeadowMarkRefusal::kNoSuchField:
      return OrderRefusal::kNoSuchSubject;
    case MeadowMarkRefusal::kAlreadyMown:
      return OrderRefusal::kConflictsWithActive;
    case MeadowMarkRefusal::kMeadowMarkRefusalCount:
      return OrderRefusal::kRuleForbids;
  }
  return OrderRefusal::kRuleForbids;
}

OrderRefusal MarkMeadow(const MeadowMarkGround& ground,
                        WorldState& current,
                        const OrderRow& order) {
  const MeadowMarkAnswer answer =
      PreviewMeadowMark(ground, current, order.position, order.area_ha, order.field);
  if (answer.refusal != MeadowMarkRefusal::kNone) {
    return OrderRefusalOf(answer.refusal);
  }
  FieldId marked = order.field;
  float hectares = 0.0F;
  if (order.field.value != kInvalidEntityIdValue) {
    // THE ROW BECOMES A DRY MEADOW THAT REMEMBERS IT WAS A FIELD: the kind
    // changes, so every rule of a meadow — the mowing's window and hands, the
    // hay lying till carted, the hay forecast — holds for it with no second
    // copy of itself, and every rule of arable (the plan, the seed, the
    // furrow) stops asking it. Its fertility, its last crop and its polygon
    // stay on the row untouched for the day kSetRotation ploughs it up.
    FieldRow& field = current.fields.rows[FindRow(current.fields, order.field)];
    field.kind = LandKind::kMeadow;
    field.mown_fallow = 1;
    field.phase = FieldPhase::kGrowing;
    field.work_days_remaining = 0.0F;
    hectares = field.area_ga;
  } else {
    // A meadow like the start's (genesis.cpp, PlaceMeadow): standing grass
    // from the day it is marked, no crop, no rotation, no polygon.
    FieldRow meadow;
    meadow.kind = answer.kind;
    meadow.center = order.position;
    meadow.area_ga = order.area_ha;
    meadow.phase = FieldPhase::kGrowing;
    marked = AppendRow(current.fields, meadow);
    hectares = order.area_ha;
  }
  SimEvent& event = EmitEvent(current, EventKind::kMeadowMarked);
  event.field = marked;
  event.amount = static_cast<std::int64_t>(std::lround(hectares * 100.0F));
  return OrderRefusal::kNone;
}

}  // namespace core
