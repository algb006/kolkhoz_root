/// @file
/// @brief The ice at a point of the map today: its thickness and whether it
/// holds a walking man (#1859; epochs.md §9 «Гибель председателя», the first
/// case «полынья и тонкий лёд»: «Лёд тонкий осенью, весной и над течением;
/// толщина льда водоёма по месяцам — дверь core»).
/// @threading PARALLEL_READONLY
/// Plain data, built between steps by a pure read (ISession::IceAt).
///
/// THE RULE (boss's answers, design.db world_param sort 380-385, all STUB):
///   - thickness grows as Stefan's law, h = ice_growth_alpha × √(Σ frost
///     degree-days) in °C × REAL days, a game day counting as 7 real days (the
///     man-days ÷ 7 rule); days above nought melt it back;
///   - the sum is walked from the winter's first frost by the live seed's own
///     day weather (WeatherOfDay, a pure function of the seed and the day), so
///     the ice is no world state and no save field;
///   - the water's kind sets a share of the still-water ice: still 1.0,
///     ice_current_backwater 0.9, ice_current_river 0.6, the rapids none; a
///     disc of ice_inflow_disc_m metres round where a river line meets a still
///     water carries the river's share;
///   - the band: ice_safe_cm (10) and above holds, under ice_deadly_cm (5) a
///     man goes through, between them it is thin.
/// The accident and its autosave are the game's; the ice only says.

#ifndef CORE_COMMON_ICE_VIEW_H_
#define CORE_COMMON_ICE_VIEW_H_

#include <cstdint>

namespace core {

/// @brief What water stands at a point, as the ice reads it.
enum class IceWater : std::uint8_t {
  kNone = 0,   ///< Dry land: no ice to stand on.
  kStill,      ///< A lake or a pond: the full ice.
  kBackwater,  ///< A backwater or shallows: ice_current_backwater of it.
  kInflow,     ///< Within ice_inflow_disc_m of a river's mouth in still water.
  kRiver,      ///< The river's channel: ice_current_river of it.
  kRapids,     ///< Never sets (the ice-order mask's B).
  kIceWaterCount
};

/// @brief Whether the ice at a point holds a walking man.
enum class IceBand : std::uint8_t {
  kNoIce = 0,  ///< Open water, or no water at all (see IceView::water).
  kDeadly,     ///< Under ice_deadly_cm: he goes through.
  kThin,       ///< From ice_deadly_cm to under ice_safe_cm: it cracks and warns.
  kSafe,       ///< ice_safe_cm and above.
  kIceBandCount
};

/// @brief The ice at one point, today.
struct IceView {
  /// False until #1859's body: say «not measured», never «no ice».
  bool measured = false;

  /// The water at the point.
  IceWater water = IceWater::kNone;

  /// Thickness, centimetres, ≥ 0; 0 on open water and on land.
  float thickness_cm = 0.0F;

  /// The band of `thickness_cm` by ice_safe_cm and ice_deadly_cm.
  IceBand band = IceBand::kNoIce;
};

}  // namespace core

#endif  // CORE_COMMON_ICE_VIEW_H_
