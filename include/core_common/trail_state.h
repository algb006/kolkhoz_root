/// @file
/// @brief TRODDEN PATHS (#2072; roads design «Как именно протаптывается тропа — механика, 16
///        сентября 2026», Alex 23 Nov 2026: «Надо добавлять механику троп сейчас, от этого
///        зависит геймплей»): the strength of the ground people walk, on the EDGES OF A LATTICE
///        over open ground (boss 23 Nov, «(A)»; roads.md) - the state, its rules and the pure
///        functions that keep them (trails.cpp; the lattice's routing: trail_lattice.cpp).
/// @threading SINGLE_THREADED
/// Written once a day by the morning placement (phase 3, decisions and assignment: the day's
/// decay before it, the placed people's tread after their work is written); takes effect only
/// at the NEXT morning's re-dating of the road index (road_route.h, WithTrails), so the day's
/// costs are frozen - today's tread changes tomorrow's ways.
///
/// WHAT A TRAIL IS. Open ground is a lattice of kTrailSnapMetres cells; a trail is ONE edge
/// between two neighbouring cells (8 neighbours), so walkers from different houses converge on
/// the same edges where their ways meet (the first, one-leg-a-trail build drew a fan of
/// straight per-house lines that never merged: LEGS2072 and the limit arm, 23 Nov 2026). Only
/// trodden edges are rows; untrodden ground has no row.
///
/// TWO DOORS, NAMED (boss 23 Nov). THE WALKER'S: a placed walker's open-ground legs are ROUTED
/// over the lattice (A*, a trodden edge cheaper; WalkRouteKm) - his world time is that route
/// and he treads its edges. THE QUERY'S: every other walking query (the accountant's measures,
/// reach) is the straight line weighted by the edges it crosses (RoadKm). Untrodden, the two
/// agree exactly (the walker's km is normalised to the straight line). STRAIGHT IS NEVER THE
/// CHEAPER (boss 23 Nov, roads.md), BY CONSTRUCTION (save 230): the query door prices a km of
/// the line at min(open, cost(P) / octile), P the line's own 8-neighbour cell path - a way the
/// walker's search sees too, so his route is never dearer than the straight answer. The doors
/// before it (a 4-connected staircase, then a 1 / 1.0824 octile bound) left the straight door
/// cheaper in 2.83 % of pairs (24 Nov, 229, B2, 64 villages); with it, 0 of 7.23 M (B2) and 0 of
/// 5.91 M (B3). The planner reads the route for yesterday's walks (save 229, option 1).
/// CPU (boss 24 Nov): the trails may cost up to +15 % user CPU of a run (trail_leg_tally.h).
/// THE LATTICE'S WEAK CASE: an 8-neighbour path runs up to 8.2 % over the line it follows -
/// about a full trail's whole discount (1.2 -> 1.10), so a trail at a slant of ~22° barely
/// beats the line.
///
/// WHO TREADS. The accountant's placed people of the morning, by the world's own predicates
/// (WorkRidesOut): who walks to his work treads his route home -> work; who rides from the horse
/// yard treads his walk home -> yard; the brigade cart's, the people's cart's riders and the
/// horsemen tread nothing (rule 8) - once a day, for the round trip. STUB of the rest: the
/// chairman's ordered crews and the day's top-up tread nothing yet; the bathers and the day-off
/// walkers have no walked way in the core at all (their reach is a check, not a trip), so they
/// tread nothing (their reach reads the trails all the same).
///
/// THE RULES (design table, each a test of the body):
///   1. Grows with walking: each walker-pass over an edge adds trail_gain_per_pass.
///   2. Decays with disuse: every morning each row loses its decay, earth and snow each at
///      its own rate.
///   3. Vanishes COMPLETELY at 0: the row is deleted - no memory of a former trail.
///   4. Snow and earth differ: a pass while snow lies (WeatherState::snow_cover_days > 0)
///      writes `snow`, else `earth`. A day of falling snow (Precipitation::kSnow) zeroes
///      `snow` before the day's tread; so does a bare day (the thaw: no cover). Earth decays
///      in weeks; snow lives from one snowfall to the next.
///   5. IN THE FOREST TOO (Alex 23 Nov 2026: «Давай отменим запрет на тропы в лесной зоне …
///      Лес будет менее диким и будут сигналы где ходят люди»; the design's «В лес не
///      заходит» is struck, roads.md §12): a leg in the forest treads like any other.
///      trail_forest_decay_factor (1.5, the undergrowth) is READ AND NOT APPLIED: the core
///      holds no stand outline (TimberStandRow keeps a loading point, not a shape), so «inside
///      the forest» has no answer here. STUB until the map's forest outlines enter the core.
///      Still no new ROAD is laid in the forest.
///   6. A laid path beats a trodden one: a full-strength trail weighs TrailTable::weight_full
///      a km, always above a laid path's 1.0, so a laid path as short or shorter wins and the
///      old trail starves (rules 2-3). Nothing is torn down.
///   7. A trodden path never becomes a laid one: no rule writes a RoadTable row.
///   8. People only: carts, teams and log carts tread nothing (boss 23 Nov).
///   9. ACCEPTED by econ's review (23 Nov; «a path through a field?»): an edge whose midpoint
///      lies on an arable field with a standing crop (FieldPhase growing or harvest) treads
///      nothing, and costs the crop nothing - no loss is modelled. The field is its disc
///      (centre, area): the core keeps no field outline (STUB of the shape). Returns as a crop
///      loss only on boss's word that it yields a decision (design limits §1).
///
/// THE WEIGHT. A lattice edge weighs off_road_weight[kWalk] (1.2) a km, lowered linearly by its
/// trail's strength s (0..1, earth and snow summed, capped at 1) toward weight_full:
/// w = 1.2 − s × (1.2 − weight_full); then divided by the ground's factor (roads.md §3).
///
/// ONE INDEX FOR EVERY WALKING RULE (econ's review question, 23 Nov). The weights enter INSIDE
/// the road index, not at a caller: WithTrails wraps the network's index (its node-to-node
/// distances shared) with a copy of the trails and the day's ground, and every builder of
/// WorldState::road_index - genesis, load, a laid road, the morning's re-dating - goes through
/// it. RoadKm, RoadMeasure, the accountant's found-place queries and home_reach's cache (keyed
/// on the index's address) all read it; reach reads its distance, without the ground's factors
/// (ReachMeasure: «a beach does not move farther in the rain», boss 23 Nov).
///
/// THE START. No trail exists at genesis (boss 23 Nov; Alex kept the four start paths, which
/// are LAID map paths in roads.csv).
///
/// SAVED: save 226 → 228, one section «trails»: weight_full, visible_strength, the twelve ground
/// factors (60 bytes), then the rows, 24 bytes each. The index is derived and rebuilt on load
/// from roads + trails + the weather's beds.

#ifndef CORE_COMMON_TRAIL_STATE_H_
#define CORE_COMMON_TRAIL_STATE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/geometry.h"

namespace core {

/// @brief The lattice's cell, metres (boss 23 Nov: «~25 m LATTICE»; STUB, 50 m is the named
///        cheaper lever if the walker's routing costs too much).
inline constexpr float kTrailSnapMetres = 25.0F;

/// @brief A lattice cell: cells of kTrailSnapMetres from the map's origin (rounded).
struct TrailEnd {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

/// @brief One trodden lattice edge between neighbouring cells. `a` is the lesser end (x, then
///        y), so an edge walked either way is one row.
struct TrailRow {
  TrailEnd a;
  TrailEnd b;
  /// Strength trodden on bare ground, 0..1.
  float earth = 0.0F;
  /// Strength trodden over snow, 0..1; zeroed by a snowfall and by the thaw.
  float snow = 0.0F;
};

/// @brief The ground's state under a walker or a horse (roads design §3 «Пешком и верхом», boss
///        23 Nov 2026; ISession::GroundStateAt). kIce is a word the core NEVER produces: its
///        weather makes no ice on land (STUB, гололёд, until a «thaw then frost» rule exists).
enum class GroundState : std::uint8_t {
  kDry = 0,
  kWet,
  kMud,
  kSnow,
  kFrozen,
  kIce,
  kGroundStateCount,
};

inline constexpr std::size_t kGroundStateCountValue =
    static_cast<std::size_t>(GroundState::kGroundStateCount);

/// @brief The speed factors by ground state (world_params `walk_*` / `ride_*`, STUBs, roads.md
///        §3): a leg's cost is its weight ÷ the factor, in the same road index the trails
///        use (econ's «one composition»). `walk` is TravelMode::kWalk; `ride` is kTeam, the
///        horse to its field work. Index by GroundState.
struct GroundFactors {
  std::array<float, kGroundStateCountValue> walk{1.0F, 0.9F, 0.75F, 0.6F, 1.0F, 0.8F};
  std::array<float, kGroundStateCountValue> ride{1.0F, 0.9F, 0.7F, 0.7F, 1.0F, 1.0F};
};

/// @brief Every trail of the village, sorted by (a, b): the order is the key's, so the save
///        and the index read one deterministic sequence. Empty at genesis.
struct TrailTable {
  /// The ground factors, kept here and saved for the reason weight_full is: every builder of
  /// the index weighs alike, the loader included (save 228). The morning writes them.
  GroundFactors ground;
  /// A km of full trail (rule 6), kept here and saved so that every builder of the road
  /// index - the loader and the road laying included, which read no world_params - weighs
  /// trails alike. The morning writes it from TrailRules::weight_full.
  float weight_full = 1.10F;
  /// The strength the layer draws a trail at (TrailView::visible), kept here for the same
  /// reason: the session reads no world_params. The morning writes it.
  float visible_strength = 0.3F;
  std::vector<TrailRow> rows;
};

/// @brief One edge trodden today and not yet laid on the trails: the passes over it.
struct TrailPass {
  TrailEnd a;
  TrailEnd b;
  std::uint32_t passes = 0;
};

/// @brief One placed walker's way of a day, home to his work (or to the horse yard): the
///        positions the timing door walks him between.
struct WalkPair {
  Vec2 from;
  Vec2 to;
};

/// @brief THE DAY'S WALKS, SAVED (save 229; boss 24 Nov, roads.md's lattice paragraph): what
///        keeps a loaded game the continued one.
///        - `pending`: today's tread, laid on WorldState::trails only NEXT MORNING before the
///          decay (the same steps, rule 9 and snow flag as when trodden). So the trails stay
///          the morning's all day, and an index rebuilt mid-day (the loader, a laid road)
///          equals the one the morning built. Sorted by edge, lesser end first.
///        - `today`: today's placed walkers' ways, written right after the morning's
///          placement (the day's close clears the work itself, so the next morning could not
///          read it: save_continue's first run found `yesterday` always empty).
///        - `yesterday`: the last day's `today`. The morning prices them on the day's index
///          by the walker's route, and the planner reads that price before the straight
///          door's (option 1 of 24 Nov).
struct TrailWalks {
  std::vector<TrailPass> pending;
  /// The snow flag the pending passes were trodden under (TreadTrailEdge's `over_snow`).
  bool pending_over_snow = false;
  std::vector<WalkPair> today;
  std::vector<WalkPair> yesterday;
};

/// @brief The rule's numbers (world_params, all `trail_*`; STUBs, econ's review 23 Nov).
struct TrailRules {
  float gain_per_pass = 0.02F;        ///< `trail_gain_per_pass`: 50 passes make a full trail
  float earth_decay_per_day = 0.01F;  ///< `trail_earth_decay_per_day`: ~25 game days to vanish
  float snow_decay_per_day = 0.02F;   ///< `trail_snow_decay_per_day`
  /// `trail_forest_decay_factor`: earth decay in the forest, times the open ground's (1.5:
  /// the undergrowth). READ, NOT APPLIED - rule 5's STUB.
  float forest_decay_factor = 1.5F;
  /// `trail_weight_full`: a km of full trail, > 1.0 (rule 6). 1.10, not 1.05 (econ's review,
  /// 23 Nov): a laid path on a busy trail still saves ~9 %, so laying stays a decision.
  float weight_full = 1.10F;
  float visible_strength = 0.3F;  ///< `trail_visible_strength`: the layer draws it above
  /// roads.md §3: `walk_wet`, `walk_mud`, `walk_snow`, `walk_frozen`, `walk_ice` and the same
  /// `ride_*` (the horse team) - STUBs; ice can never fire (no ice on land).
  GroundFactors ground;
};

/// @brief The trails' count (ISession::CountTrails).
struct TrailCount {
  std::uint32_t all = 0;
  std::uint32_t visible = 0;
};

/// @brief One trail as the layer and the game read it (ISession::Trails): the two ends in
///        metres, its strength, which ground, and whether it is visible - the core's one
///        threshold, so the layer keeps no number of its own.
struct TrailView {
  Vec2 from;
  Vec2 to;
  float strength = 0.0F;   ///< earth + snow, capped at 1
  bool over_snow = false;  ///< the snow part is the larger
  bool visible = false;    ///< strength >= TrailRules::visible_strength
};

/// @brief A point on the snap grid (rounded to the nearest cell).
TrailEnd SnapTrailEnd(Vec2 point);

/// @brief A grid end back in metres.
Vec2 TrailEndPosition(TrailEnd end);

/// @brief earth + snow, capped at 1.
float TrailStrength(const TrailRow& row);

/// @brief The row of the leg from `from` to `to` (either way), or nullptr; ends snapped.
const TrailRow* FindTrail(const TrailTable& trails, Vec2 from, Vec2 to);

/// @brief The row of the lattice edge between cells `a` and `b` (either way), or nullptr.
const TrailRow* FindTrailEdge(const TrailTable& trails, TrailEnd a, TrailEnd b);

/// @brief Rule 1 on one lattice edge: `gain` to its earth (or snow), capped at 1; the row is
///        inserted in key order if new. An edge of one cell treads nothing.
void TreadTrailEdge(TrailTable& trails, TrailEnd a, TrailEnd b, bool over_snow, float gain);

/// @brief Rule 1: one pass over the leg adds `gain` to its earth (or snow, `over_snow`),
///        capped at 1; the row is inserted in key order if new. A leg whose snapped ends are
///        one cell treads nothing (no length on the grid).
void TreadTrail(TrailTable& trails, Vec2 from, Vec2 to, bool over_snow, float gain);

/// @brief Rules 2-4: the morning's decay - snow zeroed on a snowfall or a bare day, every row
///        loses its decay, and a row at 0 on both grounds is deleted (rule 3).
/// @param snow_lies WeatherState::snow_cover_days > 0.
/// @param snow_falls Today's precipitation is snow.
void DecayTrails(TrailTable& trails, const TrailRules& rules, bool snow_lies, bool snow_falls);

/// @brief The views of the trails (ISession::Trails), in the table's order; visible by
///        trails.visible_strength.
std::vector<TrailView> TrailViews(const TrailTable& trails, bool visible_only);

/// @brief How many trails, and how many visible (ISession::CountTrails).
TrailCount CountTrailRows(const TrailTable& trails);

}  // namespace core

#endif  // CORE_COMMON_TRAIL_STATE_H_
