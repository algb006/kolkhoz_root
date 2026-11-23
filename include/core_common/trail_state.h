/// @file
/// @brief TRODDEN PATHS (#2072; roads design «Как именно протаптывается тропа — механика, 16
///        сентября 2026», Alex 23 Nov 2026: «Надо добавлять механику троп сейчас, от этого
///        зависит геймплей»): the strength of the ground people walk, on the open-ground legs
///        of the core's own walking routes. CONTRACT - the state, its rules and its doors; the
///        body is the next task (core CLAUDE.md §12а).
/// @threading SINGLE_THREADED
/// Written once a day in the events phase (6); read by every route query through the road
/// index (road_route.h), which is immutable and shared.
///
/// WHAT A TRAIL IS. The core routes a walker (TravelMode::kWalk) over roads and paths plus
/// OPEN-GROUND LEGS: from his start straight to where he joins the network, from where he
/// leaves it straight to his end, or the whole straight line when that is shorter
/// (road_route.h, RouteLeg::on_road false). Those legs are the routing graph's edges the design
/// means: a trail is ONE such leg, addressed by its two ends snapped to a kTrailSnapMetres
/// grid, so every walker who takes «the same» leg treads the same row. Two legs that cross are
/// two trails; nothing merges them.
///
/// THE RULES (design table, each a test of the body):
///   1. Grows with walking: each walker-pass over a leg adds trail_gain_per_pass.
///   2. Decays with disuse: every day each row loses trail_decay_per_day, earth and snow alike
///      at their own rates.
///   3. Vanishes COMPLETELY at 0: the row is deleted - no memory of a former trail.
///   4. Snow and earth differ: a pass while snow lies writes `snow`, else `earth`. A snowfall
///      (fresh snow over the day's threshold) zeroes `snow`; the thaw (snow cover gone) zeroes
///      it too. Earth decays in weeks; snow lives from one snowfall to the next.
///   5. Never in the forest: a leg either of whose ends, or whose midpoint, lies inside a
///      timber stand's outline treads nothing.
///   6. A laid path beats a trodden one: a full-strength trail weighs trail_weight_full a km,
///      always above a laid path's 1.0, so a laid path as short or shorter wins and the old
///      trail starves (rules 2-3). Nothing is torn down.
///   7. A trodden path never becomes a laid one: no rule writes a RoadTable row.
///   8. People only: carts, teams and log carts tread nothing (boss 23 Nov).
///   9. PROPOSED, NOT DECIDED (econ's question «a path through a field?»): a leg whose midpoint
///      lies on an arable field with a standing crop (FieldPhase growing or harvest) treads
///      nothing, and costs the crop nothing - no loss is modelled. The walker still crosses at
///      open ground's weight. Returns as a crop loss only on boss's word that it yields a
///      decision (design limits §1).
///
/// THE WEIGHT. A walker's open-ground leg weighs off_road_weight[kWalk] (1.2) a km, lowered
/// linearly by its trail's strength s (0..1, earth and snow summed, capped at 1) toward
/// trail_weight_full: w = 1.2 − s × (1.2 − trail_weight_full).
///
/// ONE DOOR FOR EVERY WALKING RULE (econ's review question, 23 Nov). The weight enters INSIDE
/// the road index, not at a caller: the day's close builds a new index sharing the network's
/// node-to-node distances, which trails do not touch, and carrying the trail weights, and
/// puts it into WorldState::road_index. RoadKm, RoadMeasure, the accountant's found-place
/// queries and home_reach's cache (keyed on the index's address) all read it, so walking time,
/// bathing reach (#1421) and leisure reach (1 000 m) see the same trails or none.
///
/// THE START. No trail exists at genesis (boss 23 Nov; Alex: «На старте тропы оставить только
/// у старой деревни» - those are LAID map paths, kept in roads.csv).
///
/// SAVED: save 226 → 227, one section (the rows below). The index is derived and rebuilt on
/// load from roads + trails.

#ifndef CORE_COMMON_TRAIL_STATE_H_
#define CORE_COMMON_TRAIL_STATE_H_

#include <cstdint>
#include <vector>

#include "core_common/geometry.h"

namespace core {

/// @brief The grid a trail's ends are snapped to, metres. STUB (boss 23 Nov: 10 m): fine
///        enough that two houses ten metres apart tread two trails, coarse enough that a
///        walker's start inside one yard is one end.
inline constexpr float kTrailSnapMetres = 10.0F;

/// @brief A trail's end on the snap grid: cells of kTrailSnapMetres from the map's origin.
struct TrailEnd {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

/// @brief One trodden leg. `a` is the lesser end (x, then y), so a leg walked either way is
///        one row.
struct TrailRow {
  TrailEnd a;
  TrailEnd b;
  /// Strength trodden on bare ground, 0..1.
  float earth = 0.0F;
  /// Strength trodden over snow, 0..1; zeroed by a snowfall and by the thaw.
  float snow = 0.0F;
};

/// @brief Every trail of the village, sorted by (a, b): the order is the key's, so the save
///        and the index read one deterministic sequence. Empty at genesis.
struct TrailTable {
  std::vector<TrailRow> rows;
};

/// @brief The rule's numbers (world_params, all `trail_*`; STUBs until econ's review).
struct TrailRules {
  float gain_per_pass = 0.02F;        ///< `trail_gain_per_pass`: 50 passes make a full trail
  float earth_decay_per_day = 0.01F;  ///< `trail_earth_decay_per_day`: ~25 game days to vanish
  float snow_decay_per_day = 0.02F;   ///< `trail_snow_decay_per_day`
  float snowfall_cm = 3.0F;           ///< `trail_snowfall_cm`: a day's fresh snow that erases
  float weight_full = 1.05F;          ///< `trail_weight_full`: a km of full trail, > 1.0 (rule 6)
  float visible_strength = 0.3F;      ///< `trail_visible_strength`: the layer draws it above
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

}  // namespace core

#endif  // CORE_COMMON_TRAIL_STATE_H_
