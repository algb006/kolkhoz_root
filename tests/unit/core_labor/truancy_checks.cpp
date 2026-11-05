// The checks of #1568 link 1, truancy (truancy_checks.h).

#include "truancy_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "labor_config.h"
#include "truancy.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// One man of 30 biological years (life_speedup 4) at `alcoholism`.
core::WorldState OneMan(float alcoholism, core::Sex sex = core::Sex::kMale) {
  core::WorldState world;
  world.world_seed = 1931;
  core::ResidentRow man;
  man.sex = sex;
  man.alcoholism = alcoholism;
  man.birth_day = -static_cast<std::int32_t>(30 * core::kDaysPerYear / 4);
  core::AppendRow(world.residents, man);
  return world;
}

core::LaborConfig On() {
  core::LaborConfig config;
  config.life_speedup = 4.0F;
  config.truancy_on = true;
  return config;
}

}  // namespace

int CheckTruancy() {
  int failures = 0;
  const core::LaborConfig on = On();
  core::LaborConfig off = On();
  off.truancy_on = false;
  const auto chance = [](const core::LaborConfig& config, const core::WorldState& world) {
    return core::TruancyChance(config, world, 0);
  };
  failures += Expect(std::fabs(chance(on, OneMan(60.0F)) - 0.10F) < 1e-6F,
                     "truancy: a man at the cap of 60 has the share at cap, 10 %");
  failures += Expect(std::fabs(chance(on, OneMan(55.0F)) - 0.05F) < 1e-6F,
                     "truancy: at 55 half of it - linear from 50");
  failures += Expect(chance(on, OneMan(49.0F)) == 0.0F, "truancy: below 50 none");
  failures += Expect(chance(on, OneMan(60.0F, core::Sex::kFemale)) == 0.0F,
                     "truancy: a woman none (crime §6: the men's door)");
  failures += Expect(chance(off, OneMan(60.0F)) == 0.0F,
                     "truancy: with crime_truancy_on 0 none - the canon");
  // THE SHARE OF DAYS, by the day's own draw: a man at the cap over 480 days.
  core::WorldState world = OneMan(60.0F);
  std::uint32_t truant = 0;
  constexpr std::uint32_t kDays = 480;
  for (std::uint32_t day = 0; day < kDays; ++day) {
    world.calendar.day = day;
    truant += core::TruantToday(on, world, 0) ? 1U : 0U;
  }
  std::cout << "  truancy: a man at 60 stayed home " << truant << " of " << kDays << " days\n";
  failures += Expect(truant >= 24 && truant <= 72,
                     "truancy: a man at the cap stays home about a tenth of days (5-15 %)");
  bool same = true;
  world.calendar.day = 17;
  const bool first = core::TruantToday(on, world, 0);
  for (int ask = 0; ask < 5; ++ask) {
    same = same && core::TruantToday(on, world, 0) == first;
  }
  failures += Expect(same, "truancy: the same answer every time it is asked within a day");
  return failures;
}
