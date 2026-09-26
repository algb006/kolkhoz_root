#include "road_laying.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core_catalog/road_cost_catalog.h"
#include "core_common/emit_event.h"
#include "core_common/road_cut.h"
#include "core_common/road_draft.h"
#include "core_common/road_graph.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "site_supply.h"

namespace core {
namespace {

/// How near a strip's axis a new stretch's middle must be to take its wear:
/// half a bed (roads design §7), the boss's «a new axis within half-width
/// inherits it» (the roads work plan, answer 8).
constexpr float kStripReachMetres = 4.0F;

/// A work whose labour is below this is done: the working hour drains the
/// seam to nought in float steps, and a crumb left by rounding is no work.
constexpr float kLabourDoneDays = 1.0e-4F;

/// Metres in the 100 m a road level's price is stated for (roads design,
/// «Полотно целиком»; boss [81]).
constexpr float kPriceMetres = 100.0F;

std::size_t SurfaceIndex(RoadSurface surface) {
  return static_cast<std::size_t>(surface);
}

/// The materials `metres` of `surface` take: its level's per-100-m amounts,
/// rounded to the gram as the selection's estimate rounds them.
ResourceAmounts MaterialsFor(const RoadWorkCatalog& catalog, RoadSurface surface, float metres) {
  const ResourceAmounts& per_100m = catalog.levels[SurfaceIndex(surface)].materials_per_100m;
  ResourceAmounts need(per_100m.size(), 0);
  for (std::size_t resource = 0; resource < per_100m.size(); ++resource) {
    need[resource] = static_cast<Grams>(std::llround(static_cast<double>(per_100m[resource]) *
                                                     static_cast<double>(metres / kPriceMetres)));
  }
  return need;
}

void AddAmounts(ResourceAmounts& into, const ResourceAmounts& more) {
  if (into.size() < more.size()) {
    into.resize(more.size(), 0);
  }
  for (std::size_t resource = 0; resource < more.size(); ++resource) {
    into[resource] += more[resource];
  }
}

/// kStartBuild's rule: the village holds `need` in full — every built unit's
/// stock it may give (UnreservedOf). The first line short, invalid when none.
ResourceId FirstShort(const WorldState& current, const ResourceAmounts& need) {
  for (std::size_t resource = 0; resource < need.size(); ++resource) {
    if (need[resource] <= 0) {
      continue;
    }
    const ResourceId id{static_cast<std::uint16_t>(resource)};
    Grams held = 0;
    for (const UnitRow& unit : current.units.rows) {
      if (unit.level > 0) {
        held += UnreservedOf(unit, id);
      }
    }
    if (held < need[resource]) {
      return id;
    }
  }
  return ResourceId{};
}

/// Takes `need` off the standing stores, whole (the caller asked FirstShort).
void TakeAll(WorldState& current, const ResourceAmounts& need) {
  for (std::size_t resource = 0; resource < need.size(); ++resource) {
    if (need[resource] > 0) {
      TakeFromStores(
          current, kNoRow, ResourceId{static_cast<std::uint16_t>(resource)}, need[resource]);
    }
  }
}

/// Opens one work and says so (kRoadWorkStarted, after the write).
void OpenWork(WorldState& current,
              RoadId road_id,
              float from_m,
              float to_m,
              RoadWorkKind kind,
              RoadSurface surface,
              float labor_days,
              const RoadSurfaceLevel& level,
              OrderId order_id) {
  const std::uint32_t row = FindRow(current.roads, road_id);
  RoadWorkRow work;
  work.road = road_id;
  work.from_m = from_m;
  work.to_m = to_m;
  work.kind = kind;
  work.surface = surface;
  work.labor_days_remaining = labor_days;
  work.max_crew = level.max_crew;
  work.winter_works = level.winter_works;
  work.place = row == kNoRow
                   ? Vec2{}
                   : PointAtChainage(current.roads.rows[row].axis, 0.5F * (from_m + to_m));
  AppendRow(current.road_works, work);
  SimEvent& event = EmitEvent(current, EventKind::kRoadWorkStarted, EventSeverity::kNotable);
  event.road = road_id;
  event.order = order_id;
  event.amount = kind == RoadWorkKind::kPave ? static_cast<std::int64_t>(surface) : 0;
}

/// THE CUT WRITTEN BACK: the road's row takes the first remnant (or `piece`,
/// or goes, when nothing remains), the other remnants are appended, and every
/// other work of the road is moved to the remnant it lies on, its metres made
/// that remnant's. `piece` — a paved stretch as a row of its own — is appended
/// after, and its id returned; invalid when there is none.
RoadId WriteCut(WorldState& current,
                std::uint32_t row,
                RoadId road_id,
                RoadCut& cut,
                std::optional<RoadRow> piece,
                RoadWorkId done) {
  std::vector<RoadId> remnant_ids;
  RoadId piece_id;
  if (cut.remnants.empty()) {
    if (piece) {
      current.roads.rows[row] = std::move(*piece);  // the whole road paved: it keeps its id
      piece_id = road_id;
      piece.reset();
    } else {
      RemoveRow(current.roads, road_id);
    }
  } else {
    current.roads.rows[row] = std::move(cut.remnants.front());
    remnant_ids.push_back(road_id);
    for (std::size_t index = 1; index < cut.remnants.size(); ++index) {
      remnant_ids.push_back(AppendRow(current.roads, cut.remnants[index]));
    }
  }
  if (piece) {
    piece_id = AppendRow(current.roads, std::move(*piece));
  }
  for (std::size_t index = 0; index < current.road_works.rows.size(); ++index) {
    RoadWorkRow& other = current.road_works.rows[index];
    if (other.road.value != road_id.value ||
        current.road_works.row_ids[index].value == done.value) {
      continue;
    }
    for (std::size_t part = 0; part < cut.remnant_spans.size(); ++part) {
      const auto [from, to] = cut.remnant_spans[part];
      if (other.from_m >= from - kShortestSpanMetres && other.to_m <= to + kShortestSpanMetres) {
        other.road = remnant_ids[part];
        other.from_m -= from;
        other.to_m -= from;
        break;
      }
    }
  }
  return piece_id;
}

std::optional<RoadOperation> UpgradeOf(RoadSurface target) {
  switch (target) {
    case RoadSurface::kGravel:
      return RoadOperation::kUpgradeToGravel;
    case RoadSurface::kAsphalt:
      return RoadOperation::kUpgradeToAsphalt;
    case RoadSurface::kAsphaltWalks:
      return RoadOperation::kUpgradeToAsphaltWalks;
    default:
      return std::nullopt;
  }
}

}  // namespace

RoadWorkCatalog ReadRoadWorkCatalog(const ITableSet& tables,
                                    float take_up_labor_share,
                                    std::string& error) {
  RoadWorkCatalog catalog;
  // No `road` row at all — a set built for another question (this module's
  // unit tests): unread, and road work refused, not the whole module.
  const ITable* const levels = tables.FindTable("unit_levels");
  const std::uint32_t unit_column = levels == nullptr ? kNoTableColumn : levels->FindColumn("unit");
  bool any_road = false;
  for (std::uint32_t row = 0; unit_column != kNoTableColumn && row < levels->RowCount(); ++row) {
    any_road = any_road || levels->CellText(row, unit_column) == "road";
  }
  if (!any_road) {
    return catalog;
  }
  if (!ReadRoadSurfaceLevels(tables, catalog.levels, error)) {
    return RoadWorkCatalog{};
  }
  catalog.take_up_labor_share = take_up_labor_share;
  catalog.read = true;
  return catalog;
}

OrderRefusal LayRoad(const RoadTracer& trace,
                     const RoadWorkCatalog& catalog,
                     WorldState& current,
                     OrderId order_id,
                     OrderRow& order) {
  if (!trace) {
    return OrderRefusal::kNoConsumer;
  }
  // A path and a dirt road are marked; gravel is a dirt road and a work of
  // gravel over it (7e). Asphalt is Epoch II and not laid in one go: STUB.
  const bool gravel = order.road_surface == RoadSurface::kGravel;
  if (order.road_surface != RoadSurface::kNone && order.road_surface != RoadSurface::kDirt &&
      !gravel) {
    return OrderRefusal::kNoConsumer;
  }
  if (gravel && !catalog.read) {
    return OrderRefusal::kNoConsumer;
  }
  RoadDraft draft;
  draft.kind = order.road_kind;
  draft.surface = order.road_surface;
  draft.point_count = order.road_point_count;
  draft.points = order.road_points;
  const RoadDraftResult traced = trace(current, draft);
  if (!traced.blocks.empty() || traced.axis.size() < 2) {
    return OrderRefusal::kRuleForbids;
  }
  ResourceAmounts need;
  if (gravel) {
    need = MaterialsFor(catalog, RoadSurface::kGravel, traced.length_m);
    const ResourceId short_of = FirstShort(current, need);
    if (short_of.value != kInvalidDefIdValue) {
      order.resource = short_of;
      return OrderRefusal::kMaterialsShort;  // nothing is laid
    }
  }
  RoadRow road;
  road.kind = draft.kind;
  road.surface = gravel ? RoadSurface::kDirt : draft.surface;
  road.origin = RoadOrigin::kPlayer;  // the axis lives in the save, as traced
  road.removable = 1;
  // The start's word does not reach a road laid in play; the default stands
  // until the core counts traffic (road_state.h).
  road.traffic_word = RoadTrafficWord::kRegular;
  road.axis.reserve(traced.axis.size());
  for (const RoadAxisPoint& point : traced.axis) {
    road.axis.push_back(RoadPoint{.position = point.position, .mark = point.mark});
  }
  // A fresh bed — unless the land under it was a road's: a stretch along a
  // strip takes the strip's wear back (7d, «Износ принадлежит ЗЕМЛЕ»). A path
  // wears nothing (roads design §4) and takes nothing.
  road.stretches.assign(StretchCountForLength(traced.length_m), RoadStretch{});
  if (road.kind == RoadKind::kRoad && !current.land_strips.rows.empty()) {
    for (std::size_t index = 0; index < road.stretches.size(); ++index) {
      const float middle =
          std::min(traced.length_m, (static_cast<float>(index) + 0.5F) * kRoadStretchMetres);
      const float wear = StripWearAt(
          current.land_strips.rows, PointAtChainage(road.axis, middle), kStripReachMetres);
      road.stretches[index].wear_pct = std::max(0.0F, wear);
    }
  }
  const RoadId laid = AppendRow(current.roads, road);
  current.road_index = BuildRoadIndex(current.roads);
  order.road = laid;
  SimEvent& event = EmitEvent(current, EventKind::kRoadLaid, EventSeverity::kNotable);
  event.road = laid;
  event.order = order_id;
  if (gravel) {
    // THE GRAVEL OVER IT, the whole length: the upgrade's price (boss [81]
    // p. 3) — the clearing the dirt road took is its own and already done.
    TakeAll(current, need);
    const RoadSurfaceLevel& level = catalog.levels[SurfaceIndex(RoadSurface::kGravel)];
    OpenWork(current,
             laid,
             0.0F,
             traced.length_m,
             RoadWorkKind::kPave,
             RoadSurface::kGravel,
             level.man_days_per_100m * traced.length_m / kPriceMetres,
             level,
             order_id);
  }
  return OrderRefusal::kNone;
}

OrderRefusal DemolishRoad(const RoadSelector& select,
                          const RoadWorkCatalog& catalog,
                          WorldState& current,
                          OrderId order_id,
                          OrderRow& order) {
  if (!select) {
    return OrderRefusal::kNoConsumer;
  }
  const std::uint32_t row = FindRow(current.roads, order.road);
  if (row == kNoRow) {
    return OrderRefusal::kRuleForbids;
  }
  const RoadSurface surface = current.roads.rows[row].surface;
  const bool paved = surface != RoadSurface::kNone && surface != RoadSurface::kDirt;
  if (paved && !catalog.read) {
    return OrderRefusal::kNoConsumer;
  }
  const RoadPieces pieces = select(
      current,
      RoadSelection{.road = order.road, .from = order.road_points[0], .to = order.road_points[1]},
      RoadOperation::kDemolish);
  std::vector<std::pair<float, float>> taken;
  for (const RoadPiece& piece : pieces.pieces) {
    if (piece.refusal == RoadPieceRefusal::kNone) {
      taken.emplace_back(piece.s_from_m, piece.s_to_m);
    }
  }
  if (taken.empty()) {
    return OrderRefusal::kRuleForbids;
  }
  const RoadId road_id = order.road;
  if (paved) {
    // TAKEN UP BY HAND (7e): labour alone, a share of the laying's, no
    // material back — gravel trodden into the bed is not gathered (boss
    // [81], STUB). The cut comes the day the work is done.
    const RoadSurfaceLevel& level = catalog.levels[SurfaceIndex(surface)];
    for (const auto& [from, to] : taken) {
      OpenWork(current,
               road_id,
               from,
               to,
               RoadWorkKind::kTakeUp,
               surface,
               catalog.take_up_labor_share * level.man_days_per_100m * (to - from) / kPriceMetres,
               level,
               order_id);
    }
    return OrderRefusal::kNone;
  }
  const bool path = current.roads.rows[row].kind == RoadKind::kPath;
  RoadCut cut = CutRoad(current.roads.rows[row], taken);
  if (cut.strips.empty()) {
    // Nothing of length came out (a sliver under a centimetre): the road is
    // as it was, and no event may say otherwise (static review of 0.36.31).
    return OrderRefusal::kRuleForbids;
  }
  // A path wears nothing (roads design §4) and leaves no strip. The strips
  // only grow — one a demolition, none consumed by a road laid again — and
  // their forgetting waits for 3b (road_state.h); named, not bounded.
  if (!path) {
    for (const LandStripRow& strip : cut.strips) {
      AppendRow(current.land_strips, strip);
    }
  }
  WriteCut(current, row, road_id, cut, std::nullopt, RoadWorkId{});
  current.road_index = BuildRoadIndex(current.roads);
  SimEvent& event = EmitEvent(current, EventKind::kRoadDemolished, EventSeverity::kNotable);
  event.road = road_id;
  event.order = order_id;
  return OrderRefusal::kNone;
}

OrderRefusal UpgradeRoad(const RoadSelector& select,
                         const RoadWorkCatalog& catalog,
                         WorldState& current,
                         OrderId order_id,
                         OrderRow& order) {
  if (!select || !catalog.read) {
    return OrderRefusal::kNoConsumer;
  }
  const std::optional<RoadOperation> operation = UpgradeOf(order.road_surface);
  const std::uint32_t row = FindRow(current.roads, order.road);
  if (!operation || row == kNoRow) {
    return OrderRefusal::kRuleForbids;
  }
  const RoadPieces pieces = select(
      current,
      RoadSelection{.road = order.road, .from = order.road_points[0], .to = order.road_points[1]},
      *operation);
  std::vector<std::pair<float, float>> taken;
  bool district = false;
  bool closed = false;
  for (const RoadPiece& piece : pieces.pieces) {
    if (piece.refusal == RoadPieceRefusal::kNone) {
      taken.emplace_back(piece.s_from_m, piece.s_to_m);
    }
    district = district || piece.refusal == RoadPieceRefusal::kDistrictRoad;
    closed = closed || piece.refusal == RoadPieceRefusal::kClosedByEpoch;
  }
  if (taken.empty()) {
    // The refusal the pieces give, the district's road and the epoch named
    // before the generic: the player's answer differs for each.
    return district ? OrderRefusal::kDistrictRoad
                    : (closed ? OrderRefusal::kGateClosed : OrderRefusal::kRuleForbids);
  }
  ResourceAmounts need;
  for (const auto& [from, to] : taken) {
    AddAmounts(need, MaterialsFor(catalog, order.road_surface, to - from));
  }
  const ResourceId short_of = FirstShort(current, need);
  if (short_of.value != kInvalidDefIdValue) {
    order.resource = short_of;
    return OrderRefusal::kMaterialsShort;  // no work is opened
  }
  TakeAll(current, need);
  const RoadSurfaceLevel& level = catalog.levels[SurfaceIndex(order.road_surface)];
  for (const auto& [from, to] : taken) {
    OpenWork(current,
             order.road,
             from,
             to,
             RoadWorkKind::kPave,
             order.road_surface,
             level.man_days_per_100m * (to - from) / kPriceMetres,
             level,
             order_id);
  }
  return OrderRefusal::kNone;
}

void SettleRoadWorks(WorldState& current) {
  // The works done today, in row order, collected before any is applied:
  // applying one moves the others (WriteCut).
  std::vector<RoadWorkId> done;
  for (std::size_t index = 0; index < current.road_works.rows.size(); ++index) {
    if (current.road_works.rows[index].labor_days_remaining <= kLabourDoneDays) {
      done.push_back(current.road_works.row_ids[index]);
    }
  }
  for (const RoadWorkId work_id : done) {
    const std::uint32_t work_row = FindRow(current.road_works, work_id);
    if (work_row == kNoRow) {
      continue;
    }
    const RoadWorkRow work = current.road_works.rows[work_row];
    const std::uint32_t row = FindRow(current.roads, work.road);
    if (row == kNoRow) {
      RemoveRow(current.road_works, work_id);  // its road gone: nothing to apply to
      continue;
    }
    const RoadId road_id = work.road;
    const RoadRow road = current.roads.rows[row];
    const std::array<std::pair<float, float>, 1> span = {{{work.from_m, work.to_m}}};
    RoadCut cut = CutRoad(road, span);
    // A SPAN THAT CUTS NOTHING is done with nothing to say (static review of
    // 0.36.38): no event may name a road paved or taken up that stands as it
    // was. The selection's pieces are longer than kShortestSpanMetres, so this
    // guards a work nobody opens.
    if (cut.strips.empty()) {
      RemoveRow(current.road_works, work_id);
      continue;
    }
    RoadId now = road_id;
    if (work.kind == RoadWorkKind::kPave) {
      RoadRow piece = RoadStretchAsRoad(road, work.from_m, work.to_m);
      piece.surface = work.surface;
      now = WriteCut(current, row, road_id, cut, std::move(piece), work_id);
    } else {
      if (road.kind == RoadKind::kRoad) {
        for (const LandStripRow& strip : cut.strips) {
          AppendRow(current.land_strips, strip);
        }
      }
      WriteCut(current, row, road_id, cut, std::nullopt, work_id);
    }
    RemoveRow(current.road_works, work_id);
    current.road_index = BuildRoadIndex(current.roads);
    SimEvent& finished = EmitEvent(current, EventKind::kRoadWorkFinished, EventSeverity::kNotable);
    finished.road = now;
    finished.amount =
        work.kind == RoadWorkKind::kPave ? static_cast<std::int64_t>(work.surface) : 0;
    if (work.kind == RoadWorkKind::kTakeUp) {
      SimEvent& gone = EmitEvent(current, EventKind::kRoadDemolished, EventSeverity::kNotable);
      gone.road = road_id;
    }
  }
}

}  // namespace core
