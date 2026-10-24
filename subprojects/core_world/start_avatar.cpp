// The chairman New Game chose, set on the start world (core_world/world.h,
// ApplyStartAvatar; #634).
//
// @threading SINGLE_THREADED
// Called once from CreateStandardSimulation, before the first step.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/avatar_catalog.h"
#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace core {
namespace {

void Refuse(std::string* error, std::string sentence) {
  if (error != nullptr) {
    *error = std::move(sentence);
  }
}

}  // namespace

bool ApplyStartAvatar(WorldState& world,
                      const ITableSet& tables,
                      std::string_view avatar_key,
                      std::string* error) {
  if (avatar_key.empty()) {
    return true;  // no avatar: the world every run and test starts in
  }
  if (tables.FindTable("avatars") == nullptr) {
    Refuse(error,
           "avatar '" + std::string(avatar_key) +
               "' was asked for and the table set carries no avatars.csv");
    return false;
  }
  // ONE READER OF THE TABLE (core_catalog/avatar_catalog.h, #635): the start
  // and every door a path changes read the same rows.
  std::vector<AvatarDef> avatars;
  std::string read_error;
  if (!ReadAvatars(tables, avatars, read_error)) {
    Refuse(error, std::move(read_error));
    return false;
  }
  const AvatarId avatar = FindAvatar(avatars, avatar_key);
  if (avatar.value == kInvalidDefIdValue) {
    // A MISSPELT SEAM WORD IS REFUSED, NOT SERVED A DEFAULT: the design has
    // no default chairman (boss, core-boss-634-avatar-door [2] (c)).
    Refuse(error, "avatar '" + std::string(avatar_key) + "' is not a key of avatars.csv");
    return false;
  }
  const AvatarDef& row = avatars[avatar.value];
  world.chairman.avatar = avatar;
  world.chairman.raikom_reputation = row.start_reputation;
  world.chairman.crime_modifier = row.crime_modifier;
  world.chairman.electrification_threshold_factor = row.electrification_threshold_factor;
  // «СТРАДА БЕЗ ВЫХОДНЫХ» IS A DAY OFF DECLARED WORKING TOO (boss, core-boss-
  // 635-avatar-paths [5] (1)): an avatar with closed day-off years starts
  // with the standing order off, and its switch refuses it in those years
  // (rush.cpp, SwitchHarvestWithoutDaysOff). After them it is his to turn on.
  if (row.day_off_working_closed_years > 0) {
    world.chairman.harvest_without_days_off = 0;
  }
  return true;
}

}  // namespace core
