/// @file
/// @brief The settlement as an entity of the game (#1089; terrain.md §3
/// «Селение — сущность игры»; econ's settlement-entity-review-2026-10-19;
/// boss, the #1056 thread [52], [53]): a village is its dwellings, derived by
/// the core - the player never draws a boundary and never places the gong.
/// @threading PARALLEL_READONLY for the reads; written by core_world's
/// settlement step (settlement_rules.h), single-threaded, in the events slot.
///
/// THE RULE, in short (the numbers are STUB, terrain.md):
///  - a DWELLING is a unit of `class` housing with a family living in it
///    (FamilyRow::house); an empty house or a visitors' dorm is none;
///  - JOIN, daily: a dwelling with no settlement joins the one with a
///    dwelling within kJoinMetres, the nearest; else, within kFormMetres of
///    some settlement, it joins the nearest as an OUTLYING yard;
///  - FORM, at the month's turn: >= kFormDwellings unassigned dwellings
///    chained within kJoinMetres, every one beyond kFormMetres of every
///    settlement's dwellings, seen so at two month turns in a row;
///  - MERGE, at the month's turn: two settlements with dwellings within
///    kJoinMetres at two month turns in a row - the older id survives;
///  - SPLIT, at the month's turn: a chain beyond kFormMetres of the rest at
///    two turns in a row, with >= kFormDwellings on each side - the side
///    holding the oldest dwelling keeps the id;
///  - THE GONG: the core's own fire_gong unit, created at the centre's
///    nearest free plot when a settlement forms, moved at a month's turn when
///    the centre has shifted beyond kGongMoveMetres; the start village keeps
///    the start layout's gong;
///  - the START VILLAGE is id kStartSettlement for ever.

#ifndef CORE_COMMON_SETTLEMENT_STATE_H_
#define CORE_COMMON_SETTLEMENT_STATE_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// A dwelling joins a settlement with a dwelling this close (STUB, m).
inline constexpr float kJoinMetres = 600.0F;

/// A new settlement forms only this far from every settlement's dwellings,
/// and a split side must stand this far from the rest (STUB, m).
inline constexpr float kFormMetres = 800.0F;

/// The fewest inhabited dwellings a settlement forms or splits with (STUB).
inline constexpr std::uint32_t kFormDwellings = 3;

/// The gong moves only for a centre shift beyond this (STUB, m).
inline constexpr float kGongMoveMetres = 100.0F;

/// The start village's id, for ever (V5).
inline constexpr std::uint32_t kStartSettlement = 1;

/// The names list's «not named yet» (strings.db `settlement.name.N`, rpg's).
inline constexpr std::uint16_t kNoSettlementName = 0xFFFF;

/// @brief One settlement. SAVED.
struct SettlementRow {
  /// The mean of its inhabited dwellings' positions, metres; written at the
  /// month's turn.
  Vec2 centre;

  /// The settlement's fire_gong unit; invalid only between a form and the
  /// gong's plot (the same step).
  UnitId gong;

  /// The day it formed (0 for the start village): the older survives a merge.
  SimDay formed_day = 0;

  /// The month's turn the gong last moved, kNoDay while never.
  SimDay gong_moved_day = 0xFFFFFFFFU;

  /// A merge seen at the last month's turn: the partner, invalid while none.
  SettlementId merge_with;

  /// A split seen at the last month's turn: 1 while pending.
  std::uint8_t split_pending = 0;

  /// The index into rpg's names list; kNoSettlementName for the start
  /// village (its map name) and while the list has no row for it (STUB).
  std::uint16_t name = kNoSettlementName;
};

using SettlementTable = StateTable<SettlementId, SettlementRow>;

}  // namespace core

#endif  // CORE_COMMON_SETTLEMENT_STATE_H_
