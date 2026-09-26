/// @file
/// @brief The player's road tools as the core answers them (roads design §9,
///        «Свободная прокладка», «Инструмент прокладки»; construction design
///        §13, «Снос дорог»; delivery 7): the DRAFT of a road the player is
///        tracing and what the core says of it, the SELECTION of pieces of a
///        laid road to upgrade or demolish, which tools are open, and the
///        network as the layer draws it.
/// @threading SINGLE_THREADED
/// Plain data. The doors that fill it (ISimulation / ISession: PreviewRoad,
/// SelectRoadPieces, RoadKindsAvailable, Roads) are const and read the
/// completed world between steps.
///
/// ONE TRACE, TWO CALLERS. The axis a preview shows is the axis the order
/// lays: the order carries the player's points, not the drawn curve, and the
/// core traces them again with the same function on the same world. The
/// save keeps the laid axis as it came out (road_state.h), so it is never
/// traced a third time. THE SAME ONLY ON THE SAME WORLD: a unit raised or a
/// road laid between the preview and the step can move or refuse the axis the
/// order lays, and the event (kOrderRefused, kRoadLaid) is what says so.
///
/// LEFT FOR 7e, named so it is planned rather than discovered: an asphalt
/// upgrade straightens its piece (roads design, «стартовая после апгрейда
/// (спрямлённая)»), so the piece's axis changes and a map road becomes a
/// player road in the save; and the work goes on half the width with traffic
/// taking turns (§8) — the selection will have to say it of a piece that is
/// the only way across a bridge. Neither is in these types yet.
///
/// WHAT THE TRACER CANNOT SEE IS SAID, NOT HIDDEN (boss, boss-core-epoch1-
/// resume [55]): the map has no slope yet, no single trees outside the
/// forest, no yard plots — each such gap is a flag on the answer, so the
/// layer can say «уклон не проверен» rather than show a clean green line.
///
/// THE LAYER'S TEN LINES (ue, through boss, boss-core-epoch1-resume [59]):
/// metres of the map, x east and y north; every axis point carries its
/// running length `s`; a refusal is a list of spans along `s`; the corridor's
/// widths are numbers here, one home; the snapped ends say what they stuck
/// to; the estimate gives clearing area and timber, never a count of trunks
/// the frame would not show; the laid geometry is read through Roads(), and
/// an event names the road it concerns rather than carrying its axis.
///
/// AN EVENT COMES AFTER ITS WRITE (ue's condition, boss, boss-core-epoch1-
/// resume, after [60]): «road laid», «piece demolished» and «work finished»
/// are raised in the same tick AFTER the road table is written, so by the
/// time the layer reads the event Roads() already answers the change.

#ifndef CORE_COMMON_ROAD_DRAFT_H_
#define CORE_COMMON_ROAD_DRAFT_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/road_state.h"

namespace core {

/// @brief A road or path the player is tracing (tools 1-5 of the roads menu).
///        Two points: a straight line with its surface's gentle wander, which
///        goes round nothing. Three or four: a smooth curve through the
///        points that goes round what is in the way (roads design §9).
struct RoadDraft {
  RoadKind kind = RoadKind::kRoad;

  /// kNone for a path; kDirt, kGravel, kAsphalt or kAsphaltWalks for a road.
  RoadSurface surface = RoadSurface::kDirt;

  /// 2, 3 or 4; anything else is refused (RoadDraftRefusal::kBadPoints).
  std::uint8_t point_count = 0;

  /// Metres from the map's south-west corner, x east, y north
  /// (core_common/geometry.h); the first `point_count` are read.
  std::array<Vec2, kRoadDraftMaxPoints> points{};
};

/// @brief One point of an axis as the layer draws it: where, how far along
///        the axis from its first point, and what stands there (a junction,
///        the way out, a bridge's end, a ford).
struct RoadAxisPoint {
  Vec2 position{};
  float s_m = 0.0F;
  RoadMark mark = RoadMark::kNone;
};

/// @brief Why a draft, or a span of it, cannot be laid. Appended, never
///        renumbered: the layer keys its words by the value.
enum class RoadDraftRefusal : std::uint8_t {
  kNone = 0,
  kBadPoints,   ///< Not 2-4 points, two on one spot (after snapping too), or a surface a path has
                ///< not.
  kOutsideMap,  ///< A point or the axis leaves the map.
  kClosedByEpoch,    ///< The surface is not open yet (asphalt: Epoch II).
  kUnit,             ///< A unit's building is in the way (RoadUnitDisc).
  kWater,            ///< A lake, pond, backwater or shallows.
  kRiverNoCrossing,  ///< The river away from a ford; its bridges are the map's roads, joined, not
                     ///< built.
  kForest,           ///< The forest: never cut for a road (roads design §9).
  kTrees,            ///< A grove or old orchard, for a path or a dirt road (gravel clears them).
  kFloodplain,       ///< Gravel or asphalt on the floodplain (roads design §11а).
  kNoWayRound,       ///< Three or four points: no curve round the obstacles near the points.
  kOutsideVillage,   ///< Asphalt with walks away from the village (roads design §2).
  /// No tracer: the answer of the door before 7b, and since then only of the
  /// bare step engine, which has no tables to trace on. The full simulation
  /// never answers it.
  kSnapsToNothing,
  kReserve,                ///< A reserve (map_areas.csv `reserve`; 7b).
  kRuins,                  ///< A ruins site (map_areas.csv `ruins_site`; 7b).
  kAlongRoad,              ///< Runs along a laid road's bed, not across or into it (7c).
  kRoadDraftRefusalCount,  ///< NOT A REFUSAL: the count, for mirrors.
};

/// @brief One refused span of an axis: what, where along it, and a point for
///        the layer's mark. A crossing (a river away from a ford) is a span
///        of nought length.
struct RoadDraftBlock {
  RoadDraftRefusal refusal = RoadDraftRefusal::kNone;
  float s_from_m = 0.0F;
  float s_to_m = 0.0F;
  Vec2 at{};
};

/// @brief What an end of the axis stuck to after snapping.
enum class RoadEndSnap : std::uint8_t {
  kFree = 0,          ///< Open ground: a dead end.
  kRoad,              ///< Onto a road's axis between junctions: a new junction.
  kJunction,          ///< Onto an existing junction or a road's end.
  kRoadEndSnapCount,  ///< NOT A SNAP: the count, for mirrors.
};

/// @brief One end of a traced axis.
struct RoadDraftEnd {
  Vec2 point{};
  RoadEndSnap snap = RoadEndSnap::kFree;

  /// For kRoad and kJunction: the road stuck to, and how far along its axis.
  RoadId road;
  float road_s_m = 0.0F;
};

/// @brief What laying or working a stretch of road costs: nought for a path
///        and a dirt road (roads design §9: «Грунтовка ничего не стоит»).
///        No count of trees (boss [59] p.5): the clearing's area and the
///        timber the stand model gives off it; the layer clears its own trees
///        in the strip after the order.
struct RoadEstimate {
  /// Game man-days of road work (WorkKind::kRoadWork).
  float man_days = 0.0F;

  /// Materials the work takes, dense by ResourceId.
  ResourceAmounts materials;

  /// Hectares of the clearing strip in groves and orchards (the forest is
  /// refused whole, boss [58]).
  float clearing_ha = 0.0F;

  /// Cubic metres of timber off that strip, by the stand model.
  float timber_m3 = 0.0F;
};

/// @brief What the map cannot tell the tracer yet, said aloud (boss [55]).
///        Set on every answer while the gap stands, whatever the draft.
struct RoadTracerGaps {
  bool slope_unchecked = true;         ///< No slope in the map: «кручи нет» is assumed.
  bool single_trees_unchecked = true;  ///< No trees outside the forest, groves and orchards.
  bool yard_plots_unchecked = true;    ///< No yard plots; units the core knows itself.

  /// The table set carries no map_areas and map_lines: the trace saw no
  /// water, forest or river at all, and a clean answer means nothing (7b).
  bool obstacles_unread = false;
};

/// @brief The core's answer to a draft.
struct RoadDraftResult {
  /// Laid as drawn only when this is empty.
  std::vector<RoadDraftBlock> blocks;

  /// The traced axis, first point to last — what the order would lay. Empty
  /// when nothing could be traced; drawn red over `blocks` by the layer.
  std::vector<RoadAxisPoint> axis;

  RoadDraftEnd start;
  RoadDraftEnd end;

  float length_m = 0.0F;

  /// The corridor, one home for the numbers (boss [59] p.3): the carriageway
  /// (or the path's tread) and the clearing strip either side of the axis,
  /// metres, by kind and surface.
  float carriageway_m = 0.0F;
  float clearing_m = 0.0F;

  RoadEstimate estimate;

  RoadTracerGaps gaps;
};

/// @brief What the player does to the pieces he selects (tools 6-9).
enum class RoadOperation : std::uint8_t {
  kUpgradeToGravel = 0,
  kUpgradeToAsphalt,
  kUpgradeToAsphaltWalks,
  kDemolish,
  kRoadOperationCount,  ///< NOT AN OPERATION: the count, for mirrors.
};

/// @brief A drag along a laid road: the road under the cursor where the drag
///        began, and the two ends of the drag. The selection grows along the
///        road and snaps to whole pieces (construction design §13).
struct RoadSelection {
  RoadId road;
  Vec2 from{};
  Vec2 to{};
};

/// @brief Why a selected piece is left out of the operation. Appended, never
///        renumbered.
enum class RoadPieceRefusal : std::uint8_t {
  kNone = 0,
  kStartRoad,              ///< One of the four start roads, never removed (construction §13).
  kOnlyRoad,               ///< The only road to a unit, a settlement or the network's way out.
  kFloodplain,             ///< A dirt road on the floodplain is not upgraded (roads design §11а).
  kAlreadyThat,            ///< Already of the surface the upgrade makes.
  kNotThisStep,            ///< Asphalt over dirt: the chain goes through gravel (roads design §9).
  kClosedByEpoch,          ///< The target surface is not open yet.
  kUnderWork,              ///< Road work already stands on the piece.
  kOutsideVillage,         ///< Asphalt with walks away from the village (roads design §2).
  kSnapsToNothing,         ///< STUB until the selection is written (7d).
  kRoadPieceRefusalCount,  ///< NOT A REFUSAL: the count, for mirrors.
};

/// @brief One piece of a road, junction to junction or end — or cut where
///        both remnants are at least the STUB 50 m — already stretched to its
///        joints (boss [59] p.10), and whether it is in.
struct RoadPiece {
  RoadId road;
  float s_from_m = 0.0F;  ///< Metres along the road's axis from its start.
  float s_to_m = 0.0F;
  RoadPieceRefusal refusal = RoadPieceRefusal::kNone;

  /// For kOnlyRoad: the unit that would be left without a road; invalid when
  /// it is a settlement or the network's way out (`stranded_map_road`).
  UnitId stranded_unit;

  /// For kOnlyRoad with no unit: the map road (tables/roads.csv) that leads
  /// to what is stranded — the district's way out; the layer names it from
  /// its key.
  MapRoadId stranded_map_road;

  /// For kOnlyRoad: the place of map_places.csv that would be left without
  /// a road (a settlement, the dacha zone, the industry zone, a hay meadow;
  /// 7d2, boss [72]) — its index in file order; the layer names it by key.
  MapPlaceId stranded_place;

  /// For kOnlyRoad: the field that would be left with no way to the
  /// village (roads design §17; 7d2, boss [72]).
  FieldId stranded_field;
};

/// @brief The core's answer to a selection: the pieces, in order along the
///        drag, and the cost of the operation on the ones that are in.
struct RoadPieces {
  std::vector<RoadPiece> pieces;
  RoadEstimate estimate;
};

/// @brief One tool of the roads menu, in the menu's order (roads design §9,
///        «Инструмент прокладки», items 1-9).
enum class RoadTool : std::uint8_t {
  kLayPath = 0,
  kLayDirt,
  kLayGravel,
  kLayAsphalt,
  kLayAsphaltWalks,
  kUpgradeToGravel,
  kUpgradeToAsphalt,
  kUpgradeToAsphaltWalks,
  kDemolish,
  kRoadToolCount,  ///< NOT A TOOL: the count, for mirrors.
};

/// @brief Why a tool stands grey (roads design §9: «Дизаблим но не
///        скрываем»). Appended, never renumbered.
enum class RoadToolClosed : std::uint8_t {
  kOpen = 0,
  kByEpoch,              ///< Not open in this epoch (asphalt: Epoch II).
  kNoMaterial,           ///< Nothing of the material the surface takes in the stores.
  kNothingToWork,        ///< No laid road it could act on (upgrade, demolish).
  kNotYetBuilt,          ///< STUB: the core has not written this tool yet (delivery 7's parts).
  kRoadToolClosedCount,  ///< NOT A REASON: the count, for mirrors.
};

/// @brief Every tool's state in one call (boss [59]: the menu's grey without
///        an empty preview).
using RoadToolStates =
    std::array<RoadToolClosed, static_cast<std::size_t>(RoadTool::kRoadToolCount)>;

/// @brief Road work standing on one piece of a road (boss [59] p.8): an
///        upgrade, a gravel or asphalt laying, a paved piece's demolition. A
///        road may carry several at once, one a piece.
struct RoadWorkView {
  float s_from_m = 0.0F;  ///< The piece, metres along the road's axis.
  float s_to_m = 0.0F;

  /// What the piece becomes: kNone for a demolition.
  RoadSurface target = RoadSurface::kNone;

  /// Ready from `s_from_m` up to this `s`, metres (the layer draws the new
  /// surface that far).
  float ready_to_s_m = 0.0F;
};

/// @brief One road as the layer draws it (boss [59] p.7): for loading and the
///        first frame, and after an event names it.
struct RoadView {
  RoadId road;
  RoadKind kind = RoadKind::kRoad;
  RoadSurface surface = RoadSurface::kDirt;
  RoadOrigin origin = RoadOrigin::kMap;

  /// For a map road, which one (tables/roads.csv); invalid for a road the
  /// player laid; a remnant of a cut map road keeps it with origin kPlayer
  /// (RoadRow::map_road) — the name, not the axis.
  MapRoadId map_road;

  /// 0: the player may not take it away — the trunk road, its bridge, the
  /// ways to the neighbours (construction design §13; RoadRow::removable).
  std::uint8_t removable = 1;

  /// How often it is driven, the start's word (RoadRow::traffic_word) until
  /// the core counts traffic: what the layer's passing-place marks fade by.
  RoadTrafficWord traffic_word = RoadTrafficWord::kRegular;

  std::vector<RoadAxisPoint> axis;

  /// The stretches' wear, 0..100, one a kRoadStretchMetres of axis from its
  /// start (the last one shorter). A path's are all nought.
  std::vector<float> wear_pct;

  /// Road work standing on it, in order along the axis; empty when none
  /// (always, until delivery 7e).
  std::vector<RoadWorkView> works;
};

}  // namespace core

#endif  // CORE_COMMON_ROAD_DRAFT_H_
