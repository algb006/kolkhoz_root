// The checks of the master's eye (master_eye_checks.h).

#include "master_eye_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "core_catalog/avatar_catalog.h"
#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/world_state.h"
#include "master_eye.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

bool Near(float value, float expected) {
  return std::abs(value - expected) < 0.001F;
}

/// `days` daily checks on `world`, the calendar's day moving with them.
void Days(const core::MasterEyeRules& rules,
          const std::vector<core::AvatarDef>& avatars,
          core::WorldState& world,
          std::uint32_t days) {
  for (std::uint32_t day = 0; day < days; ++day) {
    core::GrowMasterEye(rules, avatars, world);
    ++world.calendar.day;
  }
}

core::RedLamp Lamp(std::uint32_t subject) {
  return core::RedLamp{.kind = core::AlarmKind::kHousingShort, .subject = subject};
}

}  // namespace

int CheckTheMastersEye() {
  int failures = 0;
  const core::MasterEyeRules rules;  // econ's numbers, the documented defaults
  core::AvatarDef villager;
  villager.key = "villager";
  villager.eye_growth_factor = 2.0F;
  core::AvatarDef promoted;
  promoted.key = "promoted";
  promoted.eye_growth_factor = 0.5F;
  const std::vector<core::AvatarDef> avatars = {villager, promoted};

  // A YEAR IN OFFICE: one year of attention at the neutral speed, two for the
  // villager, a half for the promoted man.
  core::WorldState neutral;
  Days(rules, avatars, neutral, core::kDaysPerYear);
  core::WorldState village;
  village.chairman.avatar = core::AvatarId{0};
  Days(rules, avatars, village, core::kDaysPerYear);
  core::WorldState upward;
  upward.chairman.avatar = core::AvatarId{1};
  Days(rules, avatars, upward, core::kDaysPerYear);
  std::cout << "  master's eye after a year: neutral " << neutral.chairman.master_eye
            << ", villager " << village.chairman.master_eye << ", promoted "
            << upward.chairman.master_eye << '\n';
  failures +=
      Expect(Near(neutral.chairman.master_eye, 1.0F) && Near(village.chairman.master_eye, 2.0F) &&
                 Near(upward.chairman.master_eye, 0.5F),
             "eye: a year in office is one year of attention, x2 villager, x0.5 promoted");

  // A TALK, ONCE: a new talk season teaches 0.1 on the next check, and the
  // same season again teaches nothing.
  core::WorldState talk;
  talk.chairman.last_talk_season = 3;
  Days(rules, avatars, talk, 1);
  const float after_talk = talk.chairman.master_eye;
  Days(rules, avatars, talk, 1);
  failures += Expect(Near(after_talk, 0.1F + (1.0F / 48.0F)) &&
                         Near(talk.chairman.master_eye - after_talk, 1.0F / 48.0F),
                     "eye: a talk teaches 0.1 once; the same season again only the day");

  // A CRISIS OVERLOOKED: a red lamp lit 4 days teaches 0.05 on its 4th, and
  // its 5th to 9th teach nothing more; the pair is the same days unlit.
  core::WorldState crisis;
  crisis.red_lamps = {Lamp(7)};
  Days(rules, avatars, crisis, 3);
  const float before_fourth = crisis.chairman.master_eye;
  Days(rules, avatars, crisis, 1);
  const float fourth = crisis.chairman.master_eye - before_fourth;
  Days(rules, avatars, crisis, 5);
  core::WorldState quiet;
  Days(rules, avatars, quiet, 9);
  failures += Expect(Near(fourth, 0.05F + (1.0F / 48.0F)) &&
                         Near(crisis.chairman.master_eye - quiet.chairman.master_eye, 0.05F),
                     "eye: a lamp red 4 days teaches 0.05 once in its episode, the quiet "
                     "chairman nothing");
  // Out and lit again: a new episode teaches again.
  crisis.red_lamps.clear();
  Days(rules, avatars, crisis, 1);
  crisis.red_lamps = {Lamp(7)};
  Days(rules, avatars, crisis, 4);
  failures += Expect(Near(crisis.chairman.eye_crisis_gained, 0.10F),
                     "eye: the lamp out and red again is a new episode, and teaches again");

  // THE YEAR'S CAP: ten lamps red 4 days teach 0.25, not 0.5.
  core::WorldState storm;
  for (std::uint32_t subject = 1; subject <= 10; ++subject) {
    storm.red_lamps.push_back(Lamp(subject));
  }
  Days(rules, avatars, storm, 4);
  failures += Expect(Near(storm.chairman.eye_crisis_gained, 0.25F),
                     "eye: ten crises in a year teach the year's cap of 0.25");

  // THE RUNG: 2.2 years opens rung 3 (2.1), not 4 (3.5); 0.2 opens none.
  core::WorldState seen;
  seen.chairman.master_eye = 2.2F;
  core::WorldState fresh;
  fresh.chairman.master_eye = 0.2F;
  failures += Expect(core::MasterEyeOf(rules, avatars, seen).rung == 3 &&
                         core::MasterEyeOf(rules, avatars, fresh).rung == 0,
                     "eye: the view opens rung 3 at 2.2 years and none at 0.2");
  return failures;
}
