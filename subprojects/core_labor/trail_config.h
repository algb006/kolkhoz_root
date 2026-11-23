/// @file
/// @brief The trodden paths' and the ground's world_params (#2072, roads.md §3; TrailRules):
///        read here so labor_config.cpp stays inside its size (core CLAUDE.md §5).
/// @threading SINGLE_THREADED
/// Read once, at the labour's config parse.

#ifndef CORE_LABOR_TRAIL_CONFIG_H_
#define CORE_LABOR_TRAIL_CONFIG_H_

#include <span>
#include <string>
#include <string_view>

#include "core_common/trail_state.h"

namespace core {

class ITable;

/// @brief The world_params keys these rules declare: trail_* then walk_* and ride_*.
std::span<const std::string_view> TrailWorldParamKeys();

/// @brief `labor` followed by TrailWorldParamKeys, built once: the labour's one declared list
///        for the assembly's «declared for the core» guard (LaborWorldParamKeys). The first
///        call's `labor` is the one kept.
std::span<const std::string_view> WithTrailWorldParamKeys(std::span<const std::string_view> labor);

/// @brief Reads every optional trail_*, walk_* and ride_* row of `world` into `rules`; a row
///        out of range refuses with the key named. A full trail stays dearer than a laid path
///        (rule 6): trail_weight_full is bounded above 1.0; a ground factor is in [0.05, 2].
bool ParseTrailRules(const ITable& world, TrailRules& rules, std::string& error);

}  // namespace core

#endif  // CORE_LABOR_TRAIL_CONFIG_H_
