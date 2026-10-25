// The checks of the drinking raisers (alcohol_rise_checks.h).

#include "alcohol_rise_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>

#include "alcoholism.h"
#include "core_common/calendar.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

bool Near(float value, float expected) {
  return std::abs(value - expected) < 0.01F;
}

constexpr float kSpeedup = 4.0F;
constexpr std::int32_t kAdultBirth = -192;  // 20 biological years and over from day 48
// The second year's 1 February (closes January) and 1 November (closes
// October): the turns read below.
constexpr core::SimDay kFebruaryFirst = core::kDaysPerYear + core::kDaysPerMonth;
constexpr core::SimDay kNovemberFirst = core::kDaysPerYear + 10U * core::kDaysPerMonth;

/// Three yards in a row: the wedding's at 0 m, a neighbour at 600 m, a far
/// one at 2000 m; a man in each, a woman in the first, nobody drinking yet.
struct Village {
  core::WorldState world;
  core::FamilyId wedding_yard;
  core::FamilyId near_yard;
  core::FamilyId far_yard;
  core::ResidentId wedding_man;
  core::ResidentId near_man;
  core::ResidentId far_man;
  core::ResidentId woman;
};

Village MakeVillage() {
  Village village;
  core::WorldState& world = village.world;
  const auto yard_at = [&world](float x) {
    core::UnitRow house;
    house.level = 1;
    house.position = core::Vec2{.x = x, .y = 0.0F};
    core::FamilyRow family;
    family.house = AppendRow(world.units, house);
    family.satisfaction = 55.0F;
    return AppendRow(world.families, family);
  };
  village.wedding_yard = yard_at(0.0F);
  village.near_yard = yard_at(600.0F);
  village.far_yard = yard_at(2000.0F);
  const auto add = [&world](core::Sex sex, core::FamilyId family) {
    core::ResidentRow person;
    person.sex = sex;
    person.birth_day = kAdultBirth;
    person.family = family;
    person.alcoholism = 10.0F;
    return AppendRow(world.residents, person);
  };
  village.wedding_man = add(core::Sex::kMale, village.wedding_yard);
  village.near_man = add(core::Sex::kMale, village.near_yard);
  village.far_man = add(core::Sex::kMale, village.far_yard);
  village.woman = add(core::Sex::kFemale, village.wedding_yard);
  return village;
}

float Turned(Village village,
             core::SimDay day,
             const core::AlcoholismConfig& config,
             core::ResidentId who) {
  village.world.calendar.tick = static_cast<core::Tick>(day) * core::kTicksPerDay;
  core::RefreshCalendarCaches(village.world.calendar);
  core::TurnAlcoholismMonth(
      config, core::NightTradeConfig{}, core::SportConfig{}, kSpeedup, village.world);
  return village.world.residents.rows[FindRow(village.world.residents, who)].alcoholism;
}

core::FamilyRow& Family(Village& village, core::FamilyId id) {
  return village.world.families.rows[FindRow(village.world.families, id)];
}

}  // namespace

int CheckTheDrinkingRaisers() {
  int failures = 0;
  const core::AlcoholismConfig config;  // the row's numbers: 3, 1000 m, 2 in October, 2 x 2
  const Village quiet = MakeVillage();
  const std::uint32_t january = core::MonthTagOfDay(kFebruaryFirst - 1U);

  // A WEDDING IN JANUARY, read at its turn: +3 to its yard's man and to the
  // man 600 m off, nothing to the one 2 km off, the woman held at 0; against
  // the same village with no wedding, so every other rule cancels.
  {
    Village wedding = quiet;
    Family(wedding, wedding.wedding_yard).drink_occasion_month = january;
    const auto rise = [&](core::ResidentId who) {
      return Turned(wedding, kFebruaryFirst, config, who) -
             Turned(quiet, kFebruaryFirst, config, who);
    };
    failures += Expect(Near(rise(wedding.wedding_man), config.rise_wedding_wake) &&
                           Near(rise(wedding.near_man), config.rise_wedding_wake) &&
                           Near(rise(wedding.far_man), 0.0F) &&
                           Turned(wedding, kFebruaryFirst, config, wedding.woman) == 0.0F,
                       "raisers: a wedding's month gives +3 to its yard's men and the men within "
                       "1000 m, nothing beyond, and a woman stays at 0");
    // ONCE: a wedding of December is not read at January's turn.
    Village stale = quiet;
    Family(stale, stale.wedding_yard).drink_occasion_month = january - 1U;
    failures += Expect(Near(Turned(stale, kFebruaryFirst, config, stale.wedding_man),
                            Turned(quiet, kFebruaryFirst, config, quiet.wedding_man)),
                       "raisers: a wedding is read at its own month's turn and never again");
    // Two occasions near one yard still give the one rise.
    Village two = wedding;
    Family(two, two.near_yard).drink_occasion_month = january;
    failures += Expect(Near(Turned(two, kFebruaryFirst, config, two.near_man) -
                                Turned(quiet, kFebruaryFirst, config, two.near_man),
                            config.rise_wedding_wake),
                       "raisers: two weddings near a yard give its men the tradition's rise once");
  }

  // A SHOCK IN JANUARY: +2 at January's turn and at February's, none at
  // March's, to the family's men only.
  {
    const auto shock_rise = [&](std::uint32_t shock_tag, core::ResidentId who) {
      Village shocked = quiet;
      Family(shocked, shocked.far_yard).shock_month = shock_tag;
      return Turned(shocked, kFebruaryFirst, config, who) -
             Turned(quiet, kFebruaryFirst, config, who);
    };
    failures += Expect(Near(shock_rise(january, quiet.far_man), config.rise_shock) &&
                           Near(shock_rise(january - 1U, quiet.far_man), config.rise_shock) &&
                           Near(shock_rise(january - 2U, quiet.far_man), 0.0F) &&
                           Near(shock_rise(january, quiet.near_man), 0.0F),
                       "raisers: a shock gives its family's men +2 at the turns of its month and "
                       "the next, then nothing, and nobody else anything");
  }

  // THE STRADA'S END: every man +2 at the turn closing October, against the
  // same turn with the raiser at nought; the woman held at 0.
  {
    core::AlcoholismConfig no_strada = config;
    no_strada.rise_strada_end = 0.0F;
    failures += Expect(Near(Turned(quiet, kNovemberFirst, config, quiet.far_man) -
                                Turned(quiet, kNovemberFirst, no_strada, quiet.far_man),
                            config.rise_strada_end) &&
                           Near(Turned(quiet, kFebruaryFirst, config, quiet.far_man),
                                Turned(quiet, kFebruaryFirst, no_strada, quiet.far_man)) &&
                           Turned(quiet, kNovemberFirst, config, quiet.woman) == 0.0F,
                       "raisers: October's turn gives every man the strada's +2, another month's "
                       "nothing, and a woman stays at 0");
  }
  return failures;
}
