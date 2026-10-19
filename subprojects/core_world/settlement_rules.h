/// @file
/// @brief The settlement step (#1089; core_common/settlement_state.h): join
/// daily, form, merge, split and move the gong at the month's turn.
/// @threading SINGLE_THREADED
/// Run by core_world in the events slot at the day's last tick, after every
/// writer of houses and families has written the day.
///
/// CONTRACT ONLY in this delivery's first commit: the declarations, no body.

#ifndef CORE_WORLD_SETTLEMENT_RULES_H_
#define CORE_WORLD_SETTLEMENT_RULES_H_

#include "core_common/ids.h"
#include "core_common/world_state.h"

namespace core {

/// @brief What the settlement step needs of the tables: the unit types of
/// `class` housing, and the fire_gong's type.
struct SettlementConfig {
  /// Dense by UnitTypeId: 1 for a unit of class housing.
  std::vector<std::uint8_t> housing;

  /// unit_types.csv `fire_gong`; invalid in a world without one (no gong is
  /// created or moved, the settlements still form).
  UnitTypeId fire_gong;

  /// A gong frozen where it stands - the identity run of V6 (boss, the
  /// #1056 thread [53]); off in every other run.
  bool gong_frozen = false;
};

/// @brief Whether `unit` is an inhabited dwelling of `world`: of class
///        housing, standing (level >= 1, not dead), and some family's house.
/// @param inhabited Dense by unit row: 1 for a unit some family lives in,
///        as InhabitedUnits returns it.
bool IsDwelling(const SettlementConfig& config,
                const UnitRow& unit,
                std::uint32_t unit_row,
                const std::vector<std::uint8_t>& inhabited);

/// @brief Dense by unit row: 1 for a unit some family names as its house.
std::vector<std::uint8_t> InhabitedUnits(const WorldState& world);

/// @brief The day's step: every dwelling with no settlement joins one (or
///        stays a candidate); every other unit takes its nearest
///        settlement. At the month's turn (the day's last tick of a month's
///        last day): form, merge and split by the two-turns rule, the
///        centres written, and the gongs created and moved.
/// @pre The start world has the start village (id kStartSettlement) with
///      the start layout's gong (SeedStartSettlement).
/// @post Every unit's `settlement` names a live settlement, or is invalid
///       when the world has none.
/// Side effects: rows of world.settlements made, merged away and removed;
/// fire_gong units created, moved and removed; the four events said.
void RunSettlementDay(const SettlementConfig& config, WorldState& current);

/// @brief Seeds the start village on the genesis world: id
///        kStartSettlement, formed day 0, the start layout's fire_gong as
///        its gong (the first of the type), every unit its member.
void SeedStartSettlement(const SettlementConfig& config, WorldState& world);

}  // namespace core

#endif  // CORE_WORLD_SETTLEMENT_RULES_H_
