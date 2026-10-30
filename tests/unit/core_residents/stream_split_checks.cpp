/// @file
/// @brief Unit cases of the keyed draws (stream_split_checks.h).

#include "stream_split_checks.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <utility>

#include "core_common/calendar.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "demography.h"
#include "life_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr std::uint32_t kCouples = 24;
constexpr core::SimDay kDays = 40;

core::LifeConfig Config() {
  core::LifeConfig config;
  config.definitions.units.is_housing = {1};
  // Deaths and births both common, so a re-deal shows in a few weeks.
  config.mortality_young_percent_per_year = 60.0F;
  config.mortality_mid_percent_per_year = 60.0F;
  config.mortality_old_percent_per_year = 60.0F;
  config.fertility_from_years = 16.0F;
  config.fertility_to_years = 46.0F;
  for (core::EpochDemography& epoch : config.epochs) {
    epoch.children_per_family = 18.0F;
    epoch.child_mortality_percent = 20.0F;
  }
  return config;
}

/// The village: kCouples households of a wife and a husband.
core::WorldState Village() {
  core::WorldState world;
  world.world_seed = 1938;
  world.rng = core::SeedRngState(1938, 0);
  world.calendar.tick = core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  for (std::uint32_t i = 0; i < kCouples; ++i) {
    core::UnitRow house;
    house.level = 1;
    const core::UnitId house_id = AppendRow(world.units, house);
    core::FamilyRow family;
    family.house = house_id;
    family.satisfaction = 60.0F;
    family.satiety_year_mean = 80.0F;
    const core::FamilyId family_id = AppendRow(world.families, family);
    world.units.rows.back().household = family_id;
    core::ResidentRow wife;
    wife.family = family_id;
    wife.sex = core::Sex::kFemale;
    wife.birth_day = -300 - static_cast<std::int32_t>(i);
    wife.satiety = 80.0F;
    wife.health = 80.0F;
    const core::ResidentId wife_id = AppendRow(world.residents, wife);
    core::ResidentRow husband = wife;
    husband.sex = core::Sex::kMale;
    husband.spouse = wife_id;
    const core::ResidentId husband_id = AppendRow(world.residents, husband);
    world.residents.rows[FindRow(world.residents, wife_id)].spouse = husband_id;
  }
  return world;
}

struct Fates {
  std::map<std::uint64_t, core::SimDay> died;             // resident id -> the day gone
  std::set<std::pair<std::uint64_t, core::SimDay>> born;  // (mother id, day)
};

enum class Arm : std::uint8_t {
  kBase,
  kExtraDraw,   // another subsystem draws once more off the world's stream
  kOneMoreRow,  // one more resident in the table, a bachelor who bears nobody
};

Fates Live(Arm arm) {
  const core::LifeConfig config = Config();
  core::WorldState world = Village();
  core::ResidentId stranger{};
  if (arm == Arm::kOneMoreRow) {
    // His own death draw sits between the village's deaths and its births in
    // the day, so on one stream every birth after it was re-dealt. A widower
    // in his OWN house: roofless, the ladder lodged him with a family, and
    // that family's day moved by the world, not by the draw.
    core::UnitRow house;
    house.level = 1;
    const core::UnitId house_id = AppendRow(world.units, house);
    core::FamilyRow family;
    family.house = house_id;
    family.satisfaction = 60.0F;
    family.satiety_year_mean = 80.0F;
    const core::FamilyId family_id = AppendRow(world.families, family);
    world.units.rows.back().household = family_id;
    core::ResidentRow old;
    old.family = family_id;
    old.sex = core::Sex::kMale;
    old.birth_day = -2000;
    old.satiety = 80.0F;
    old.health = 80.0F;
    stranger = AppendRow(world.residents, old);
  }
  std::set<std::uint64_t> alive;
  for (const core::ResidentId id : world.residents.row_ids) {
    alive.insert(id.value);
  }
  Fates fates;
  for (core::SimDay day = 1; day <= kDays; ++day) {
    world.calendar.tick = static_cast<std::uint64_t>(day) * core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    if (arm == Arm::kExtraDraw) {
      core::NextRandomBits(world.rng);
    }
    core::RunDemographyDay(config, world);
    std::set<std::uint64_t> now;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const std::uint64_t id = world.residents.row_ids[row].value;
      now.insert(id);
      if (!alive.contains(id) &&
          world.residents.rows[row].birth_day == static_cast<std::int32_t>(world.calendar.day)) {
        fates.born.insert({world.residents.rows[row].mother.value, world.calendar.day});
      }
    }
    for (const std::uint64_t id : alive) {
      if (!now.contains(id) && id != stranger.value) {
        fates.died[id] = world.calendar.day;
      }
    }
    // Only the first village is compared: the stranger's own fate is his,
    // and newborns' ids are dealt in birth order.
    std::set<std::uint64_t> kept;
    for (const std::uint64_t id : now) {
      if (alive.contains(id)) {
        kept.insert(id);
      }
    }
    alive = std::move(kept);
  }
  return fates;
}

/// Every fate the two arms disagree on, by resident and day: a red line names
/// WHO moved, not only that something did.
void PrintDifferences(const char* arm, const Fates& base, const Fates& other) {
  for (const auto& [id, day] : base.died) {
    const auto it = other.died.find(id);
    if (it == other.died.end() || it->second != day) {
      std::cout << "  " << arm << ": resident " << id << " died on day " << day << " in the base, "
                << (it == other.died.end() ? 0U : it->second) << " here (0 = lived)\n";
    }
  }
  for (const auto& [id, day] : other.died) {
    if (!base.died.contains(id)) {
      std::cout << "  " << arm << ": resident " << id << " died on day " << day
                << " here and lived in the base\n";
    }
  }
  for (const auto& birth : base.born) {
    if (!other.born.contains(birth)) {
      std::cout << "  " << arm << ": mother " << birth.first << " bore on day " << birth.second
                << " in the base only\n";
    }
  }
  for (const auto& birth : other.born) {
    if (!base.born.contains(birth)) {
      std::cout << "  " << arm << ": mother " << birth.first << " bore on day " << birth.second
                << " here only\n";
    }
  }
}

}  // namespace

/// THE STREAM IS NOT A COIN ANY MORE (#1310; #1303). Until 0.37.341 every
/// death, birth and wedding drew off the one world generator in row order, so
/// one extra draw anywhere earlier in the day — the herd plan's, #1289 — or
/// one row more in the table re-dealt WHO died and WHO bore from then on, and
/// every A/B pair differed by that noise (seed 1938 y3 d9: resident 12
/// against 13). Keyed by site, resident and day, both arms deal the same.
int CheckDrawsAreKeyedByResident() {
  int failures = 0;
  const Fates base = Live(Arm::kBase);
  const Fates extra = Live(Arm::kExtraDraw);
  const Fates more = Live(Arm::kOneMoreRow);
  std::cout << "  keyed draws: base " << base.died.size() << " deaths, " << base.born.size()
            << " births in " << kDays << " days\n";
  PrintDifferences("one extra draw", base, extra);
  PrintDifferences("one resident more", base, more);
  failures += Expect(!base.died.empty() && !base.born.empty(),
                     "keyed draws: the base village has deaths and births to compare");
  failures += Expect(extra.died == base.died && extra.born == base.born,
                     "keyed draws: one extra draw off the world's stream moves no death and no "
                     "birth");
  failures += Expect(more.died == base.died && more.born == base.born,
                     "keyed draws: one resident more in the table moves no one else's death and "
                     "no birth");
  return failures;
}
