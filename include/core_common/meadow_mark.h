/// @file
/// @brief Marking a meadow: the chairman's kMarkMeadow, the answer the layer
/// previews before sending it, and the start's land as the map draws it
/// (Livestock design §5 «Игрок размечает луг сам», boss's decision of
/// 2 October 2026; the contract's rulings — boss on core's moves [70] and
/// [71] of thread core-boss-c2-site-supply-2026-10-09, 10 October 2026).
/// @threading SINGLE_THREADED
/// The order is read in the production sub-step of the decisions slot
/// (phase 3) on the sim thread: it appends a row to the fields table or
/// rewrites one, and appends to the step's outbox. The preview is a pure
/// read, called between steps on the sim thread like PreviewRoad.
///
/// CONTRACT 0.37.210, IMPLEMENTED 0.37.211 (subprojects/core_common/
/// meadow_mark.cpp). What the implementation changed in the contract: the
/// ground gained the plots' radii and the stands' — the contract named «a
/// unit's plot» and «a stand's contour» and gave the function neither.
///
/// WHAT THE IMPLEMENTATION ADDS TO THE SEAM, named so boss enters the words in
/// the same move as the delivery:
///
///   OrderKind::kMarkMeadow (appended)        seam key `mark_meadow`
///   EventKind::kMeadowMarked (appended)      seam key `meadow_marked` —
///     field = the row marked, amount = its hectares × 100
///   MapAreaKind::kStartField, ::kStartMeadow (appended)
///     map_areas.csv kinds `start_field`, `start_meadow`; `area` = the
///     start_layout key. Today's reader refuses the whole file on a kind it
///     does not know, so the export lands WITH the code, not before.
///   MeadowMarkRefusal (new, below) — the preview's own reasons; the order's
///     refusal stays one of the old words (OrderRefusalOf).
///   ISimulation::PreviewMeadowMark (new door) — MeadowMarkAnswer.
///   OrderRow: no new field. `position` and `area_ha` (kPlantForest's) for a
///     new meadow; `field` for the second form.
///   FieldRow::start_shape (uint8: 1 + the row's polygon in MapObstacles'
///     start land, 0 for none) and FieldRow::mown_fallow (uint8). SAVE
///     FORMAT 144 → 145, composition: FieldRow + 2 bytes; the fields section
///     grows by 2 × rows and nothing else moves — predicted before the fields
///     are added.
///
/// TWO FORMS, ONE ORDER.
/// (1) A NEW MEADOW: `position` and `area_ha`, `field` invalid. Free, at
///     once, on any day; no ceiling on one mark or on all of them — «цена
///     луга — руки на косьбу», «лишний луг ничего не стоит». The row is a
///     meadow like the start's: standing grass from the day it is marked,
///     mown in the meadows' window at farming.csv meadow_mow_days_per_ha, its
///     yield by meadow_kinds.csv, its hay lying on it until carted.
/// (2) FORMER ARABLE MOWN AS IT LIES: `field` names an arable field whose
///     rotation is empty («залежь и бывшая пашня косятся как суходол, пока не
///     распаханы»). The row stays arable and is mown as dry meadow while its
///     rotation stays empty (FieldRow::mown_fallow). kSetRotation on it ends
///     the mowing — a chain ploughs it up, the empty chain («the word taken
///     back») simply stops the scythes; no unmarking order is needed.
///
/// THE CONTOUR OF A MARK IS A CIRCLE OF ITS AREA. STUB, and its return
/// condition is the field door: a FieldRow holds a centre and an area and no
/// shape, and the design's «как поле: многоугольник» comes into the core when
/// shapes do (the human, 10 October 2026: «Разметку поля в первой сборке
/// откладываем»). The layer draws what it likes inside the area it sends.
///
/// BUT THE START'S LAND IS TESTED BY THE MAP'S POLYGONS, not by circles: the
/// start's fields and meadows are long shapes, and a circle of a start
/// meadow's area round its centre both misses its far ends and covers ground
/// it never lay on (boss's check of sixteen candidate circles: twelve clean by
/// circles touched a start meadow or field by the map). So: a mark's circle
/// against the POLYGON of every start row still standing (FieldRow::
/// start_shape), and against the CIRCLE of every row the player marked. A
/// start row removed stops being asked. The same test replaces kPlantForest's
/// «touches a field» (timber_planting.cpp, ZoneRefusal), which had the same
/// hole.
///
/// THE KIND IS THE CORE'S READING OF THE FLOOD: the share of the circle's
/// cells that carry kObstacleFloodplain. At or above kFloodplainShareMin a
/// floodplain meadow, at or below kDryShareMax a dry one, between — refused,
/// with its own reason, because the player's cure differs («режь контур по
/// границе поймы»). «Flood year — after the water» is a STUB that cannot
/// fire: the core has no flood.
///
/// A ROAD OR A PATH THROUGH THE CIRCLE REFUSES NOTHING («дорога не нужна:
/// бригада едет напрямик»; the start's meadows lie across paths).
///
/// UNMARKING IS kRemoveField, AND IT TAKES ANY MEADOW, the start's too
/// («снять разметку — всегда и даром»; with a door to mark it back the
/// removal is no longer a one-way loss) — its kWrongLand for a meadow goes.
/// HAY LYING ON IT DOES NOT VANISH: the removal is refused kNotEmpty while
/// mown hay still lies on the meadow, the rule a field with reaped grain has
/// had since 2026-09-14. Standing grass is removed with the row («некошеный
/// луг ничего не стоит и ничего не даёт»).
///
/// THE RUN'S CHAIRMAN IS GIVEN HIS PLACES (the human, 2 October 2026: «Вы
/// должны ботам задать оптимальные места»): tables/meadow_suggestions.csv,
/// boss's export from the map database — `rank` (the order to mark in),
/// `key`, `x_m`, `y_m`, `area_ha`, `field` (a start_layout key for form 2,
/// blank for form 1). The core's tables carry it; the core itself reads it
/// only in its own runs. «Nearest free ground first» is not written anywhere.

#ifndef CORE_COMMON_MEADOW_MARK_H_
#define CORE_COMMON_MEADOW_MARK_H_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"

namespace core {

class ObstacleRaster;
struct WorldState;

/// @brief The share of a mark's cells on the floodplain from which it is a
///        floodplain meadow. The design's «не меньше трёх четвертей».
inline constexpr float kFloodplainShareMin = 0.75F;

/// @brief The share at or below which it is a dry meadow. «Не больше
///        четверти».
inline constexpr float kDryShareMax = 0.25F;

/// @brief Why a meadow may not be marked — the PREVIEW's word, finer than the
///        order's. Appended only; the count is for mirrors.
enum class MeadowMarkRefusal : std::uint8_t {
  kNone = 0,
  kBadArea,                 ///< `area_ha` is not positive.
  kOutsideMap,              ///< The circle leaves the map.
  kWater,                   ///< A lake, pond, backwater or shallows in the circle.
  kRiver,                   ///< The river's channel, by its width, in the circle.
  kForest,                  ///< The forest.
  kTrees,                   ///< A grove or old orchard («роща и лес в контур не входят»).
  kReserve,                 ///< A reserve.
  kRuins,                   ///< A ruins site.
  kPit,                     ///< A clay pit, sand pit or stone quarry.
  kMixedFloodplain,         ///< The flood share lies between the two bounds: neither kind.
  kUnit,                    ///< A unit's plot.
  kStand,                   ///< A timber stand's contour (a grove, a belt, a planting).
  kLand,                    ///< Another field or meadow: a start row by its polygon, a marked one
                            ///< by its circle.
  kNoSuchField,             ///< Form 2: the field named is not there.
  kNotFallow,               ///< Form 2: the field is a meadow, or its rotation is not empty.
  kAlreadyMown,             ///< Form 2: the field is mown as it lies already.
  kMeadowMarkRefusalCount,  ///< NOT A REASON: the count, for mirrors.
};

/// @brief The core's answer to a mark the player is drawing.
struct MeadowMarkAnswer {
  /// kNone: the order with the same fields would be taken this step.
  MeadowMarkRefusal refusal = MeadowMarkRefusal::kNone;

  /// What the row would be: kMeadow or kFloodplainMeadow for form 1; kArable
  /// for form 2 (the row keeps its kind and is mown as dry meadow). Not
  /// meaningful when refused for anything but kMixedFloodplain.
  LandKind kind = LandKind::kMeadow;

  /// The share of the circle's cells on the floodplain, 0..1. Printed with a
  /// kMixedFloodplain refusal so the layer can say how far off the mark is.
  float flood_share = 0.0F;

  /// Hay of a whole cut at the kind's yield, kilograms — area × the yield of
  /// meadow_kinds.csv, no weather in it. 0 when refused.
  float hay_kg_a_cut = 0.0F;

  /// Mowers' work for the whole cut, REAL man-days (area ×
  /// meadow_mow_days_per_ha). 0 when refused.
  float mow_man_days = 0.0F;

  /// Where the first obstacle lies, for the layer's red mark; the centre when
  /// the refusal has no place (kBadArea, kMixedFloodplain, form 2's three).
  Vec2 at{};
};

/// @brief One start field or meadow as the map draws it (map_areas.csv,
///        kinds `start_field` / `start_meadow`).
struct StartLandShape {
  std::string key;            ///< The start_layout key the polygon is filed under.
  std::vector<Vec2> outline;  ///< Closed polygon, metres from the south-west corner; >= 3 points.
};

/// @brief What the marking reads besides the world: the map's raster, the
///        start's shapes, and the numbers of the tables.
struct MeadowMarkGround {
  /// The obstacle raster (obstacle_raster.h). Null when the tables carry no
  /// map areas: then nothing of the map refuses and every mark is dry — said
  /// by the caller, not guessed here.
  const ObstacleRaster* raster = nullptr;

  /// The start's land, indexed by FieldRow::start_shape - 1.
  const std::vector<StartLandShape>* start_land = nullptr;

  /// The pits (clay, sand, stone) as polygons: they are areas of the map and
  /// not cells of the raster.
  const std::vector<std::vector<Vec2>>* pits = nullptr;

  /// The radius nothing may come inside, by UnitTypeId value (PlotRules,
  /// plot.h). ADDED BY THE IMPLEMENTATION (0.37.211): the contract named
  /// «a unit's plot» and gave the function no way to know one.
  std::span<const float> plot_radius_m;

  /// The radius of each timber stand's contour, by the stands table's ROW,
  /// metres — the caller's, because a stand's hectares are a table's number
  /// the map does not hold. Null or short: the stands past it refuse nothing.
  /// Added by the implementation, as above.
  const std::vector<float>* stand_radius_m = nullptr;

  float map_side_m = 0.0F;
  float mow_days_per_ha = 0.0F;             ///< farming.csv meadow_mow_days_per_ha, real man-days.
  float dry_yield_kg_per_ha = 0.0F;         ///< meadow_kinds.csv, the dry meadow.
  float floodplain_yield_kg_per_ha = 0.0F;  ///< meadow_kinds.csv, the floodplain meadow.
};

/// @brief What the core would answer to a kMarkMeadow with these fields —
///        the one function the preview and the order both ask, so the two
///        cannot answer apart.
/// @param ground The map and the tables' numbers.
/// @param world The world to mark on; read only.
/// @param position Form 1: the centre, metres. Ignored for form 2.
/// @param area_ha Form 1: hectares, > 0. Ignored for form 2.
/// @param field Form 2: the arable field to mow as it lies; invalid for
///        form 1.
/// @return The answer; refusal kNone when the order would be taken.
/// @note Pure: no side effects. Cost: the circle's cells of the raster (a
///       100 ha mark is ~160 000 cells of 2.5 m) plus one pass over fields,
///       units and stands.
MeadowMarkAnswer PreviewMeadowMark(const MeadowMarkGround& ground,
                                   const WorldState& world,
                                   Vec2 position,
                                   float area_ha,
                                   FieldId field);

/// @brief The order's word for a preview's reason.
/// @return kNone for kNone; kRuleForbids for kBadArea and kOutsideMap;
///         kWrongLand for the map's grounds (kWater … kPit) and for
///         kMixedFloodplain and kNotFallow; kTooClose for kUnit, kStand and
///         kLand; kNoSuchSubject for kNoSuchField; kConflictsWithActive for
///         kAlreadyMown.
OrderRefusal OrderRefusalOf(MeadowMarkRefusal refusal);

/// @brief Reads a kMarkMeadow: marks the meadow (form 1 — appends a FieldRow
///        of the answer's kind at `order.position`, `order.area_ha` hectares,
///        in kGrowing) or sets the named field mown as it lies (form 2 —
///        FieldRow::mown_fallow), and emits kMeadowMarked.
/// @pre Runs in the decisions slot on the sim thread.
/// @return OrderRefusalOf(PreviewMeadowMark(...).refusal). A refused order
///         changes nothing and emits nothing of its own.
/// @note Side effects on success: one row appended or one byte set; one
///       event. Nothing is spent and no work is opened — the mowing opens in
///       the meadows' window like any meadow's.
OrderRefusal MarkMeadow(const MeadowMarkGround& ground, WorldState& current, const OrderRow& order);

/// @brief Whether `circle` (centre, radius) touches the closed polygon
///        `outline` — inside it, or nearer than `radius` to an edge.
/// @pre `outline` holds at least three points.
/// @note The test kPlantForest's zone asks of a start field too.
bool CircleTouchesPolygon(Vec2 centre, float radius_m, const std::vector<Vec2>& outline);

/// @brief Whether a circle touches the field or meadow `land`: by the map's
///        polygon when the row has one (FieldRow::start_shape and
///        `ground.start_land`), by the row's own circle of its area when it
///        has none. THE ONE TEST for a mark and for a planting's zone
///        (kPlantForest), so the two cannot draw the start's land apart.
/// @note Strict: shapes that only touch do not overlap.
bool CircleTouchesLand(const MeadowMarkGround& ground,
                       const FieldRow& land,
                       Vec2 centre,
                       float radius_m);

}  // namespace core

#endif  // CORE_COMMON_MEADOW_MARK_H_
