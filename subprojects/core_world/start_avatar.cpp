// The chairman New Game chose, set on the start world (core_world/world.h,
// ApplyStartAvatar; #634).
//
// @threading SINGLE_THREADED
// Called once from CreateStandardSimulation, before the first step.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/avatar_catalog.h"
#include "core_catalog/world_conventions.h"
#include "core_common/calendar.h"
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

/// The acting chairman's skill (econ, core-boss-1417-1421-columns [3], Q1:
/// «опытная», STUB) - skill_agriculture_earned has no other writer (#1424).
constexpr float kTrainedMilkmaidSkill = 75.0F;

/// THE ACTING CHAIRMAN'S THREE TRAINED MILKMAIDS (#1417; avatars.md l.184, as
/// boss wrote it 2 Nov: no post before the yard): the first `count` adult
/// women of the start by row, holding no post, get kTrainedMilkmaidSkill. The
/// start has no cattle yard, the milkmaid post's only unit (unit_staff.csv),
/// so the post is the player's move the day the yard stands. A start with
/// fewer such women gives as many as it has.
bool GiveTrainedMilkmaids(WorldState& world,
                          const ITableSet& tables,
                          std::uint8_t count,
                          std::string* error) {
  if (count == 0) {
    return true;
  }
  std::optional<float> speedup;
  std::string read_error;
  if (!FindLifeSpeedup(tables, speedup, read_error)) {
    Refuse(error, std::move(read_error));
    return false;
  }
  float adult_years = 16.0F;
  if (const ITable* const life = tables.FindTable("life")) {
    const std::uint32_t row = life->FindRowByKey("adult_age_years");
    const std::uint32_t column = life->FindColumn("value");
    if (row != kNoTableRow && column != kNoTableColumn) {
      adult_years = static_cast<float>(life->CellReal(row, column).value_or(adult_years));
    }
  }
  std::uint8_t given = 0;
  for (ResidentRow& resident : world.residents.rows) {
    if (given == count) {
      break;
    }
    const bool adult =
        BiologicalAgeYears(speedup.value_or(1.0F), resident.birth_day, world.calendar.day) >=
        adult_years;
    if (resident.sex != Sex::kFemale || !adult ||
        resident.post.profession.value != kInvalidDefIdValue) {
      continue;
    }
    resident.skill_agriculture_earned = kTrainedMilkmaidSkill;
    ++given;
  }
  return true;
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
  world.chairman.sees_theft_same_day = row.sees_theft_same_day ? 1U : 0U;  // P3, dormant
  // «СТРАДА БЕЗ ВЫХОДНЫХ» IS A DAY OFF DECLARED WORKING TOO (boss, core-boss-
  // 635-avatar-paths [5] (1)): an avatar with closed day-off years starts
  // with the standing order off, and its switch refuses it in those years
  // (rush.cpp, SwitchHarvestWithoutDaysOff). After them it is his to turn on.
  if (row.day_off_working_closed_years > 0) {
    world.chairman.harvest_without_days_off = 0;
  }
  return GiveTrainedMilkmaids(world, tables, row.start_trained_milkmaids, error);
}

}  // namespace core
