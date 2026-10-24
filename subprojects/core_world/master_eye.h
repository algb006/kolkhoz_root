/// @file
/// @brief «Хозяйский глаз» — the chairman's hidden metric that opens the
///        office's statistics by itself (#636; role-lines §15, «Ступени
///        открывает сама метрика, перк — только раньше»; econ's P2).
/// @threading SINGLE_THREADED
/// The daily check (core_world, hour 0, after the red lamps are painted);
/// the view between steps.
///
/// WHAT GROWS IT, of the design's table, and what does not yet:
///   * time in office — 1 / 48 a day, one year of attention a game year;
///   * a talk — eye_per_talk for each kTalkToSport done (the one talk the
///     core has; dialogues have no door);
///   * a crisis overlooked — eye_per_missed_crisis once per red-lamp
///     episode that reaches eye_missed_crisis_days, capped a calendar year at
///     eye_missed_crisis_cap_per_year;
///   * all × the avatar's eye_growth_factor.
/// STUB until the layer asks (boss, core-boss-636-master-eye [2]): the walk
/// on foot (the layer's, the core has no chairman's position) and «работа со
/// статистикой» (the core cannot see a window opened). Return condition: a
/// layer door that reports them.
#ifndef CORE_WORLD_MASTER_EYE_H_
#define CORE_WORLD_MASTER_EYE_H_

#include <array>
#include <span>
#include <string_view>

#include "core_catalog/avatar_catalog.h"
#include "core_common/master_eye_view.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

/// @brief The eye's numbers (world_params, all STUB, econ's of 24 October).
struct MasterEyeRules {
  std::array<float, kMasterEyeRungs> thresholds = {0.25F, 1.0F, 2.1F, 3.5F, 5.0F};
  float per_talk = 0.1F;
  float per_missed_crisis = 0.05F;
  std::uint16_t missed_crisis_days = 4;
  float missed_crisis_cap_per_year = 0.25F;
};

/// @brief The world_params keys this file reads, for the assembly's union.
std::span<const std::string_view> MasterEyeWorldParamKeys();

/// @brief Reads the rules; a missing row keeps its documented number.
/// @return false with `error` naming the row on an out-of-range one, or on
///         thresholds that do not rise.
bool ReadMasterEyeRules(const ITableSet& tables, MasterEyeRules& rules, std::string& error);

/// @brief One day of the eye, at the daily check AFTER `world.red_lamps` was
///        painted: the day in office, a new talk season, the lamps' ages and
///        the episodes that reached their days.
/// Side effects: ChairmanState::master_eye and its three memory fields,
/// WorldState::eye_lamp_ages rebuilt in red_lamps' order.
void GrowMasterEye(const MasterEyeRules& rules,
                   std::span<const AvatarDef> avatars,
                   WorldState& world);

/// @brief The office's door: the metric, its rung and the thresholds.
MasterEyeView MasterEyeOf(const MasterEyeRules& rules,
                          std::span<const AvatarDef> avatars,
                          const WorldState& world);

}  // namespace core

#endif  // CORE_WORLD_MASTER_EYE_H_
