/// @file
/// The game's base conventions as rows of world_params.csv, and the one door
/// to the biology factor.
///
/// @threading SINGLE_THREADED — read once, while the simulation is assembled.
///
/// WHY THE CALENDAR IS CHECKED AND NOT READ. The design base became the home
/// of the base conventions on 2026-09-25 (boss core-boss-epoch1-6 [28], [32]:
/// `world_param` rows with convention = 1). Four days a month, 24 hours a
/// day and the seven-day week are the game's STRUCTURE, not balance: they
/// size arrays, time every schedule and are baked into saves. So the core
/// keeps its compiled constants (core_common/calendar.h) and refuses a table
/// that says otherwise, naming the key — the base cannot silently disagree
/// with the build, and nothing runs on a calendar the save format does not
/// know.
///
/// THE BIOLOGY FACTOR IS READ, and through one door. life_speedup lived in
/// life.csv and was read there by six modules and eight runs, each with its
/// own fallback. Its home is now world_params.csv; life.csv still carries it
/// until that file becomes an export of the design base. While both carry
/// it, they must agree — otherwise the day one of them is changed, half the
/// core would age people by the other.

#ifndef CORE_CATALOG_WORLD_CONVENTIONS_H_
#define CORE_CATALOG_WORLD_CONVENTIONS_H_

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core_tables/tables.h"

namespace core {

/// @brief The world_params.csv keys of the base conventions — the core
/// knows them, so the assembly's declared-readers check accepts them.
std::span<const std::string_view> ConventionWorldParamKeys();

/// @brief Refuses a table set whose base conventions disagree with the build.
///
/// Each calendar key present in world_params.csv must equal its compiled
/// constant: `clock_scale`, `months_per_year`, `days_per_month`,
/// `hours_per_day`, `days_per_week`; `real_minutes_per_day_x1` must equal
/// hours_per_day × 60 / clock_scale. A key ABSENT is accepted — the build's
/// constant stands. The biology factor must be in range and, when both homes
/// carry it, equal in both.
/// @param tables The table set being assembled.
/// @param error Set to the key, the table's value and the build's on refusal.
/// @return false when the set is refused.
bool CheckWorldConventions(const ITableSet& tables, std::string& error);

/// @brief The biology factor: biological years per game year (×4).
///
/// world_params.csv first, life.csv when world_params has no such row.
/// @param tables The table set.
/// @param value Set when either home carries the row; left empty otherwise,
///        so each caller keeps its own answer for a set without it.
/// @param error Set on a value that is not a number, out of 0.1..100, or
///        two homes that disagree.
/// @return false on error; `value` is then empty.
bool FindLifeSpeedup(const ITableSet& tables, std::optional<float>& value, std::string& error);

/// @brief FindLifeSpeedup for a reader that has no way to report.
/// @return The factor, or `fallback` for a set without the row AND for a
///         refused one — the refusal is the assembly's to report
///         (CheckWorldConventions stops a standard simulation first).
float LifeSpeedupOr(const ITableSet& tables, float fallback);

}  // namespace core

#endif  // CORE_CATALOG_WORLD_CONVENTIONS_H_
