// The master's eye (core_world/master_eye.h; #636).

#include "master_eye.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 9> kMasterEyeWorldParamKeys = {
    "eye_rung_1_years",
    "eye_rung_2_years",
    "eye_rung_3_years",
    "eye_rung_4_years",
    "eye_rung_5_years",
    "eye_per_talk",
    "eye_per_missed_crisis",
    "eye_missed_crisis_days",
    "eye_missed_crisis_cap_per_year"};

float SpeedOf(std::span<const AvatarDef> avatars, const WorldState& world) {
  return AvatarOf(avatars, world.chairman.avatar).eye_growth_factor;
}

}  // namespace

std::span<const std::string_view> MasterEyeWorldParamKeys() {
  return kMasterEyeWorldParamKeys;
}

bool ReadMasterEyeRules(const ITableSet& tables, MasterEyeRules& rules, std::string& error) {
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  auto days = static_cast<float>(rules.missed_crisis_days);
  const Range years{.low = 0.0F, .high = 100.0F};
  const std::array<ScalarKnob, 9> knobs = {{
      {.key = kMasterEyeWorldParamKeys[0], .value = &rules.thresholds[0], .range = years},
      {.key = kMasterEyeWorldParamKeys[1], .value = &rules.thresholds[1], .range = years},
      {.key = kMasterEyeWorldParamKeys[2], .value = &rules.thresholds[2], .range = years},
      {.key = kMasterEyeWorldParamKeys[3], .value = &rules.thresholds[3], .range = years},
      {.key = kMasterEyeWorldParamKeys[4], .value = &rules.thresholds[4], .range = years},
      {.key = kMasterEyeWorldParamKeys[5],
       .value = &rules.per_talk,
       .range = {.low = 0.0F, .high = 10.0F}},
      {.key = kMasterEyeWorldParamKeys[6],
       .value = &rules.per_missed_crisis,
       .range = {.low = 0.0F, .high = 10.0F}},
      {.key = kMasterEyeWorldParamKeys[7],
       .value = &days,
       .range = {.low = 1.0F, .high = static_cast<float>(kDaysPerYear)}},
      {.key = kMasterEyeWorldParamKeys[8],
       .value = &rules.missed_crisis_cap_per_year,
       .range = {.low = 0.0F, .high = 10.0F}},
  }};
  if (!ReadKnobs(*world, "world_params", knobs, error)) {
    return false;
  }
  for (std::size_t rung = 1; rung < rules.thresholds.size(); ++rung) {
    if (!(rules.thresholds[rung] > rules.thresholds[rung - 1])) {
      error = "world_params eye_rung_*_years: the thresholds must rise rung by rung";
      return false;
    }
  }
  rules.missed_crisis_days = static_cast<std::uint16_t>(days);
  return true;
}

void GrowMasterEye(const MasterEyeRules& rules,
                   std::span<const AvatarDef> avatars,
                   WorldState& world) {
  ChairmanState& chairman = world.chairman;
  const float speed = SpeedOf(avatars, world);
  float gain = 1.0F / static_cast<float>(kDaysPerYear);  // a day in office
  // A TALK: the season the sport talk stamped is new to the eye.
  if (chairman.last_talk_season != 0 &&
      chairman.last_talk_season != chairman.eye_talk_season_seen) {
    chairman.eye_talk_season_seen = chairman.last_talk_season;
    gain += rules.per_talk;
  }
  // THE CRISES OVERLOOKED: each red lamp's days in a row, carried from the
  // memory by lamp (both lists sorted); an episode that reaches its days
  // teaches once, within the year's cap.
  const auto year = static_cast<std::uint16_t>((world.calendar.day / kDaysPerYear) + 1U);
  if (chairman.eye_crisis_year != year) {
    chairman.eye_crisis_year = year;
    chairman.eye_crisis_gained = 0.0F;
  }
  std::vector<WorldState::EyeLampAge> ages;
  ages.reserve(world.red_lamps.size());
  std::size_t old = 0;
  float crisis = 0.0F;
  for (const RedLamp& lamp : world.red_lamps) {
    while (old < world.eye_lamp_ages.size() && world.eye_lamp_ages[old].lamp < lamp) {
      ++old;
    }
    WorldState::EyeLampAge age{.lamp = lamp};
    if (old < world.eye_lamp_ages.size() && world.eye_lamp_ages[old].lamp == lamp) {
      age = world.eye_lamp_ages[old];
    }
    if (age.days < 0xFFFFU) {
      ++age.days;
    }
    if (age.counted == 0 && age.days >= rules.missed_crisis_days) {
      age.counted = 1;
      crisis += rules.per_missed_crisis;
    }
    ages.push_back(age);
  }
  world.eye_lamp_ages = std::move(ages);
  const float room = std::max(0.0F, rules.missed_crisis_cap_per_year - chairman.eye_crisis_gained);
  crisis = std::min(crisis, room);
  chairman.eye_crisis_gained += crisis;
  chairman.master_eye += (gain + crisis) * speed;
}

MasterEyeView MasterEyeOf(const MasterEyeRules& rules,
                          std::span<const AvatarDef> avatars,
                          const WorldState& world) {
  MasterEyeView view;
  view.years = world.chairman.master_eye;
  view.thresholds = rules.thresholds;
  view.speed = SpeedOf(avatars, world);
  for (std::size_t rung = 0; rung < rules.thresholds.size(); ++rung) {
    if (view.years >= rules.thresholds[rung]) {
      view.rung = static_cast<std::uint8_t>(rung + 1);
    }
  }
  return view;
}

}  // namespace core
