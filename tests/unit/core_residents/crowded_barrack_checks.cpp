// The checks of row 456 (crowded_barrack_checks.h).

#include "crowded_barrack_checks.h"

#include <cstdint>
#include <iostream>
#include <vector>

#include "core_common/alarm_state.h"
#include "core_common/calendar.h"
#include "core_common/family_state.h"
#include "core_common/random.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "demography.h"
#include "housing.h"
#include "housing_ladder.h"
#include "life_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

/// Type 0 a family's house, type 1 a barrack of five places.
core::LifeConfig BarrackConfig() {
  core::LifeConfig config;
  config.definitions.units.is_housing = {1, 1};
  config.residents_capacity = {{}, {5.0F}};
  return config;
}

core::UnitId AddUnit(core::WorldState& world, std::uint16_t type) {
  core::UnitRow unit;
  unit.type = core::UnitTypeId{type};
  unit.level = 1;
  return AppendRow(world.units, unit);
}

/// A family of `people` adults living in barrack `barrack`.
core::FamilyId AddBarrackFamily(core::WorldState& world, core::UnitId barrack, int people) {
  core::FamilyRow family;
  family.house = barrack;
  family.in_barrack = 1;
  const core::FamilyId id = AppendRow(world.families, family);
  for (int person = 0; person < people; ++person) {
    core::ResidentRow resident;
    resident.family = id;
    resident.sex = person % 2 == 0 ? core::Sex::kFemale : core::Sex::kMale;
    resident.birth_day = -40 * 12;  // forty biological years
    AppendRow(world.residents, resident);
  }
  return id;
}

void SetDay(core::WorldState& world, std::uint32_t day) {
  world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
}

/// The crowded set, the lamp's (a) at three families and not at two, and the
/// pair: a barrack within its places lights nothing.
int CheckTheLamp() {
  int failures = 0;
  const core::LifeConfig config = BarrackConfig();
  core::WorldState world;
  SetDay(world, 100);
  const core::UnitId roomy = AddUnit(world, 1);
  const core::UnitId crowded = AddUnit(world, 1);
  AddBarrackFamily(world, roomy, 2);
  AddBarrackFamily(world, crowded, 3);
  AddBarrackFamily(world, crowded, 2);
  const core::FamilyId last = AddBarrackFamily(world, crowded, 1);
  const std::vector<bool> set = core::CrowdedBarracks(config, world);
  failures += Expect(set.size() == 2 && !set[0] && set[1],
                     "crowded barrack: six in five places is crowded, two in five is not");
  failures += Expect(core::FamiliesInCrowdedBarracks(config, world) == 3,
                     "crowded barrack: its three families counted, the roomy one's not");
  core::Alarm alarm;
  failures +=
      Expect(core::HousingShortAlarm(config, world, alarm) &&
                 alarm.kind == core::AlarmKind::kHousingShort && alarm.amount == 3,
             "crowded barrack: three families over the places light (a), three homes asked");
  // The family of one leaves: five in five places is not over them.
  for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
    if (world.residents.rows[row].family.value == last.value) {
      world.residents.rows[row].family = core::FamilyId{};
    }
  }
  world.families.rows.back().house = core::UnitId{};
  world.families.rows.back().in_barrack = 0;
  failures += Expect(core::FamiliesInCrowdedBarracks(config, world) == 0 &&
                         !core::HousingShortAlarm(config, world, alarm),
                     "crowded barrack: at its places the barrack is not crowded, the lamp out");
  return failures;
}

/// Two free houses: both go to the crowded barrack's families, though the
/// roomy barrack's family stands first in the rows.
int CheckTheClimb() {
  int failures = 0;
  const core::LifeConfig config = BarrackConfig();
  core::WorldState world;
  SetDay(world, 16);  // May
  const core::UnitId roomy = AddUnit(world, 1);
  const core::UnitId crowded = AddUnit(world, 1);
  const core::FamilyId first = AddBarrackFamily(world, roomy, 2);
  AddBarrackFamily(world, crowded, 3);
  AddBarrackFamily(world, crowded, 3);
  AddUnit(world, 0);
  AddUnit(world, 0);
  core::RunRoofless(config, world);
  const std::uint32_t first_row = FindRow(world.families, first);
  failures +=
      Expect(world.families.rows[first_row].in_barrack == 1 &&
                 world.families.rows[1].in_barrack == 0 && world.families.rows[2].in_barrack == 0,
             "crowded barrack: the free houses take the crowded barrack's families first");
  // The pair: a third house goes to the roomy barrack's family.
  AddUnit(world, 0);
  core::RunRoofless(config, world);
  failures += Expect(world.families.rows[first_row].in_barrack == 0,
                     "crowded barrack: with the crowded one emptied, the next house takes the "
                     "roomy barrack's family");
  return failures;
}

/// A couple waiting for a house with a barrack place free: it waits on;
/// the pair - a free house - marries it.
int CheckNoWeddingIntoABarrackPlace() {
  int failures = 0;
  const core::LifeConfig config = BarrackConfig();
  core::WorldState world;
  world.world_seed = 3;
  world.rng = core::SeedRngState(3, 0);  // an unseeded stream draws nought: every chance falls
  SetDay(world, 100);
  AddUnit(world, 1);  // an empty barrack of five
  const core::UnitId parents_house = AddUnit(world, 0);
  core::FamilyRow parents_family;
  parents_family.house = parents_house;
  const core::FamilyId parents = AppendRow(world.families, parents_family);
  world.units.rows[FindRow(world.units, parents_house)].household = parents;
  core::ResidentRow bride;
  bride.family = parents;
  bride.sex = core::Sex::kFemale;
  bride.birth_day = 100 - (20 * 12);
  const core::ResidentId bride_id = AppendRow(world.residents, bride);
  core::ResidentRow groom = bride;
  groom.sex = core::Sex::kMale;
  const core::ResidentId groom_id = AppendRow(world.residents, groom);
  core::WeddingWaitRow couple;
  couple.bride = bride_id;
  couple.groom = groom_id;
  couple.since_day = 90;
  AppendRow(world.wedding_waits, couple);
  core::RunDemographyDay(config, world);
  const std::uint32_t bride_row = FindRow(world.residents, bride_id);
  failures +=
      Expect(bride_row != core::kNoRow &&
                 world.residents.rows[bride_row].spouse.value == core::kInvalidEntityIdValue &&
                 world.wedding_waits.rows.size() == 1,
             "no wedding into a barrack: with only a barrack place free the couple waits");
  const core::UnitId house = AddUnit(world, 0);
  core::RunDemographyDay(config, world);
  const std::uint32_t wed_row = FindRow(world.residents, bride_id);
  const std::uint32_t home_row = wed_row != core::kNoRow
                                     ? FindRow(world.families, world.residents.rows[wed_row].family)
                                     : core::kNoRow;
  failures += Expect(
      wed_row != core::kNoRow && world.residents.rows[wed_row].spouse.value == groom_id.value &&
          home_row != core::kNoRow && world.families.rows[home_row].house.value == house.value &&
          world.families.rows[home_row].in_barrack == 0,
      "no wedding into a barrack: a free house - the couple marries into it");
  return failures;
}

/// A widow with her small son, waiting with a groom: her yard keeps a child
/// alone, so the groom would come into her yard (PlanWedding). In a barrack
/// that is a wedding into a barrack place - it waits; the pair, her yard in a
/// house, weds at once.
int CheckNoWeddingIntoABarrackYard() {
  int failures = 0;
  const core::LifeConfig config = BarrackConfig();
  for (const bool in_barrack : {true, false}) {
    core::WorldState world;
    world.world_seed = 5;
    world.rng = core::SeedRngState(5, 0);
    SetDay(world, 100);
    const core::UnitId barrack = AddUnit(world, 1);
    // No free house in the barrack's world: the climb would take her out first.
    const core::UnitId her_house = in_barrack ? core::UnitId{} : AddUnit(world, 0);
    core::FamilyRow her_yard;
    her_yard.house = in_barrack ? barrack : her_house;
    her_yard.in_barrack = in_barrack ? 1 : 0;
    const core::FamilyId hers = AppendRow(world.families, her_yard);
    if (!in_barrack) {
      world.units.rows[FindRow(world.units, her_house)].household = hers;
    }
    core::ResidentRow widow;
    widow.family = hers;
    widow.sex = core::Sex::kFemale;
    widow.birth_day = 100 - (30 * 12);
    const core::ResidentId widow_id = AppendRow(world.residents, widow);
    core::ResidentRow son;
    son.family = hers;
    son.sex = core::Sex::kMale;
    son.birth_day = 100 - (5 * 12);
    son.mother = widow_id;
    AppendRow(world.residents, son);
    const core::UnitId his_house = AddUnit(world, 0);
    core::FamilyRow his_yard;
    his_yard.house = his_house;
    const core::FamilyId his = AppendRow(world.families, his_yard);
    world.units.rows[FindRow(world.units, his_house)].household = his;
    core::ResidentRow groom;
    groom.family = his;
    groom.sex = core::Sex::kMale;
    groom.birth_day = 100 - (32 * 12);
    const core::ResidentId groom_id = AppendRow(world.residents, groom);
    core::WeddingWaitRow couple;
    couple.bride = widow_id;
    couple.groom = groom_id;
    couple.since_day = 90;
    AppendRow(world.wedding_waits, couple);
    core::RunDemographyDay(config, world);
    const std::uint32_t widow_row = FindRow(world.residents, widow_id);
    const bool wed =
        widow_row != core::kNoRow && world.residents.rows[widow_row].spouse.value == groom_id.value;
    failures += Expect(in_barrack ? !wed : wed,
                       in_barrack ? "no wedding into a barrack: the groom does not come into a "
                                    "widow's yard that lives in a barrack"
                                  : "no wedding into a barrack: into a widow's yard in a house "
                                    "he comes at once");
  }
  return failures;
}

/// A migrant's roof: a barrack place before a free house; with no barrack
/// place, the free house only while no couple waits for one.
int CheckTheMigrantGoesToTheBarrackFirst() {
  int failures = 0;
  const core::LifeConfig config = BarrackConfig();
  core::WorldState world;
  const core::UnitId house = AddUnit(world, 0);
  const core::UnitId barrack = AddUnit(world, 1);
  bool shared = false;
  failures +=
      Expect(core::HomeForNewcomers(config, world, 1, shared).value == barrack.value && shared,
             "migrant: a barrack place before the free house");
  AddBarrackFamily(world, barrack, 5);  // the barrack full
  failures +=
      Expect(core::HomeForNewcomers(config, world, 1, shared).value == house.value && !shared,
             "migrant: the barrack full and no couple waiting - the free house");
  core::WeddingWaitRow couple;
  couple.since_day = 0;
  AppendRow(world.wedding_waits, couple);
  failures +=
      Expect(core::HomeForNewcomers(config, world, 1, shared).value == core::kInvalidEntityIdValue,
             "migrant: a couple waiting - the free house is theirs, the migrant has none");
  return failures;
}

}  // namespace

int CheckNoWeddingIntoABarrackAndTheCrowdedBarrack() {
  int failures = 0;
  failures += CheckTheLamp();
  failures += CheckTheClimb();
  failures += CheckNoWeddingIntoABarrackPlace();
  failures += CheckNoWeddingIntoABarrackYard();
  failures += CheckTheMigrantGoesToTheBarrackFirst();
  return failures;
}
