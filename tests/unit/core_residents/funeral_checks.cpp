/// @file
/// @brief Unit cases of the funeral step (funeral_checks.h).

#include "funeral_checks.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "core_common/event_state.h"
#include "core_common/funeral_reads.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "funeral_rules.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::SimDay kToday = 1000;
constexpr std::int32_t kAdultBorn = 500;  // ~42 biological years on kToday
constexpr std::int32_t kChildBorn = 976;  // 2 biological years on kToday

// Type 0 is the cemetery, type 1 a house.
core::FuneralConfig Config() {
  core::FuneralConfig config;
  config.cemetery_type = core::UnitTypeId{0};
  return config;
}

core::WorldState Empty() {
  core::WorldState world;
  world.calendar.day = kToday;
  return world;
}

core::UnitId Cemetery(core::WorldState& world, float x, std::uint32_t settlement) {
  core::UnitRow unit;
  unit.type = core::UnitTypeId{0};
  unit.position = core::Vec2{.x = x, .y = 0.0F};
  unit.level = 1;
  unit.settlement = core::SettlementId{settlement};
  return core::AppendRow(world.units, unit);
}

/// A household in a house at `x` of `settlement`.
core::FamilyId Household(core::WorldState& world, float x, std::uint32_t settlement) {
  core::UnitRow house;
  house.type = core::UnitTypeId{1};
  house.position = core::Vec2{.x = x, .y = 0.0F};
  house.level = 1;
  house.settlement = core::SettlementId{settlement};
  core::FamilyRow family;
  family.house = core::AppendRow(world.units, house);
  return core::AppendRow(world.families, family);
}

core::ResidentId Person(core::WorldState& world,
                        core::FamilyId family,
                        std::int32_t born,
                        core::ResidentId mother = {},
                        core::ResidentId spouse = {}) {
  core::ResidentRow person;
  person.family = family;
  person.birth_day = born;
  person.mother = mother;
  person.spouse = spouse;
  return core::AppendRow(world.residents, person);
}

core::ResidentRow& Row(core::WorldState& world, core::ResidentId id) {
  return world.residents.rows[core::FindRow(world.residents, id)];
}

bool Has(const std::vector<core::ResidentId>& list, core::ResidentId id) {
  return std::ranges::find(list, id) != list.end();
}

/// THE PARTY (social.md «Кто идёт»): two hops of adult kin, the household but
/// its small children, then one adult of every household within 300 m of his
/// house in his settlement; kin from another settlement still come.
int CheckTheParty() {
  int failures = 0;
  core::WorldState world = Empty();
  const core::FamilyId home = Household(world, 0.0F, 1);
  const core::FamilyId son_home = Household(world, 5000.0F, 2);  // another village
  const core::FamilyId near = Household(world, 200.0F, 1);
  const core::FamilyId far = Household(world, 400.0F, 1);
  const core::FamilyId other = Household(world, 100.0F, 2);  // near, but not his village
  const core::ResidentId grandmother = Person(world, home, 0);
  const core::ResidentId dead = Person(world, home, kAdultBorn, grandmother);
  const core::ResidentId wife = Person(world, home, kAdultBorn, {}, dead);
  Row(world, dead).spouse = wife;
  const core::ResidentId son = Person(world, son_home, kAdultBorn + 200, wife);
  Row(world, son).father = dead;
  const core::ResidentId grandson = Person(world, son_home, kAdultBorn + 300);
  Row(world, grandson).father = son;
  const core::ResidentId baby = Person(world, home, kChildBorn, wife);
  const core::ResidentId near_old = Person(world, near, kAdultBorn);
  const core::ResidentId near_young = Person(world, near, kAdultBorn + 100);
  const core::ResidentId far_man = Person(world, far, kAdultBorn);
  const core::ResidentId other_man = Person(world, other, kAdultBorn);
  std::uint32_t kin_count = 0;
  const std::vector<core::ResidentId> party =
      core::FuneralParty(Config(), world, dead, {}, kin_count);
  failures +=
      Expect(Has(party, grandmother) && Has(party, wife) && Has(party, son) && Has(party, grandson),
             "funeral: the mother, the wife, the son of another village and the "
             "grandson (two hops) walk");
  failures += Expect(!Has(party, baby) && !Has(party, dead),
                     "funeral: the baby and the dead himself do not walk");
  failures += Expect(kin_count == 4, "funeral: four kin, counted before the neighbours");
  failures += Expect(Has(party, near_old) && !Has(party, near_young),
                     "funeral: one adult of the household 200 m off, the oldest id");
  failures += Expect(!Has(party, far_man) && !Has(party, other_man),
                     "funeral: no neighbour past 300 m, none of another village");
  failures += Expect(party.size() == 5, "funeral: the party is the four kin and one neighbour");
  return failures;
}

/// THE JOINT FUNERAL (social.md «Какое кладбище»; boss,
/// core-boss-1084-funeral-body [2]): two dead of one village on one day are
/// one funeral at the cemetery of the least summed road, and both graves go
/// there - though one dead alone would have the other cemetery.
int CheckTheJointFuneral() {
  int failures = 0;
  core::WorldState world = Empty();
  const core::UnitId west = Cemetery(world, -100.0F, 1);
  const core::UnitId east = Cemetery(world, 950.0F, 1);
  const core::FamilyId first_home = Household(world, 0.0F, 1);
  const core::FamilyId second_home = Household(world, 900.0F, 1);
  const core::FamilyId third_home = Household(world, 950.0F, 1);
  const core::ResidentId first = Person(world, first_home, kAdultBorn);
  const core::ResidentId second = Person(world, second_home, kAdultBorn);
  Person(world, second_home, kAdultBorn);  // the second's household keeps an adult
  Person(world, third_home, kAdultBorn);
  Person(world, first_home, kAdultBorn);
  const std::vector<core::ResidentId> died = {first, second};
  failures += Expect(core::CemeteryFor(Config(), world, {&first, 1}) == west,
                     "funeral: alone, the first dead's cemetery is the west one");
  const std::vector<core::UnitId> laid = core::OpenFunerals(Config(), world, died);
  failures += Expect(world.funerals.rows.size() == 1,
                     "funeral: two dead of one village on one day are ONE funeral");
  failures += Expect(laid.size() == 2 && laid[0] == east && laid[1] == east,
                     "funeral: both graves go to the east cemetery (least summed road)");
  const core::FuneralRow& funeral = world.funerals.rows[0];
  failures += Expect(funeral.cemetery == east && funeral.burial_day == kToday + 1 &&
                         funeral.deceased.size() == 2 && funeral.households.size() == 2,
                     "funeral: buried tomorrow at the east cemetery, the two dead and their "
                     "households named");
  failures += Expect(funeral.road_km > 0.0F, "funeral: the procession's road is measured");
  const auto invitations = std::ranges::count_if(world.step_events, [](const core::SimEvent& e) {
    return e.kind == core::EventKind::kFuneralInvitation;
  });
  failures += Expect(invitations == 1, "funeral: one invitation for the joint funeral");
  return failures;
}

/// LOUD AND ITS CAP (social.md «Председатель — по приглашению»): a child's
/// funeral is loud; a second within 14 days is quiet; after them, loud again.
int CheckTheLoudCap() {
  int failures = 0;
  core::WorldState world = Empty();
  const core::FamilyId home = Household(world, 0.0F, 1);
  Person(world, home, kAdultBorn);  // the household keeps its adult
  const core::FuneralConfig config = Config();
  const core::ResidentId child = Person(world, home, kChildBorn);
  const core::ResidentId other_child = Person(world, home, kChildBorn);
  const core::ResidentId third_child = Person(world, home, kChildBorn);
  const core::ResidentId grown = Person(world, home, kAdultBorn);
  core::OpenFunerals(config, world, std::vector<core::ResidentId>{child});
  failures += Expect(world.funerals.rows.back().loud == 1, "funeral: a child's is loud");
  world.calendar.day = kToday + 5;
  core::OpenFunerals(config, world, std::vector<core::ResidentId>{other_child});
  failures += Expect(world.funerals.rows.back().loud == 0,
                     "funeral: a second child's five days later is quiet (the 14-day cap)");
  world.calendar.day = kToday + 20;
  core::OpenFunerals(config, world, std::vector<core::ResidentId>{grown});
  failures += Expect(world.funerals.rows.back().loud == 0,
                     "funeral: a grown man's whose household keeps adults is quiet");
  core::OpenFunerals(config, world, std::vector<core::ResidentId>{third_child});
  failures +=
      Expect(world.funerals.rows.back().loud == 1, "funeral: a child's past the cap is loud again");
  return failures;
}

/// THE CHAIRMAN'S «ПОЙТИ», THE CLOSE, AND THE MOOD (social.md): the order
/// books «went» once; the close the day after the burial gives the kin +10
/// and the household +3; a loud one missed gives -8 and -3; the grief fades
/// over 8 days.
int CheckTheChairman() {
  int failures = 0;
  const core::FuneralConfig config = Config();
  core::WorldState world = Empty();
  const core::FamilyId home = Household(world, 0.0F, 1);
  const core::ResidentId dead = Person(world, home, kChildBorn);  // a child: loud
  const core::ResidentId mother = Person(world, home, kAdultBorn);
  Row(world, dead).mother = mother;
  core::OpenFunerals(config, world, std::vector<core::ResidentId>{dead});
  const core::FuneralId id = world.funerals.row_ids[0];
  failures += Expect(core::FuneralMoodTerm(config, Row(world, mother), kToday) == -10.0F,
                     "funeral: the mother's grief is -10 on the death day");
  failures += Expect(core::FuneralMoodTerm(config, Row(world, mother), kToday + 4) == -5.0F &&
                         core::FuneralMoodTerm(config, Row(world, mother), kToday + 8) == 0.0F,
                     "funeral: it fades to -5 in four days and to nought in eight");
  const auto order = [&](core::Grams amount) {
    core::OrderRow row;
    row.kind = core::OrderKind::kAttendFuneral;
    row.amount = amount;
    return core::AppendRow(world.orders, row);
  };
  const core::OrderId went = order(id.value);
  const core::OrderId again = order(id.value);
  const core::OrderId none = order(id.value + 50);
  core::TakeAttendOrders(world);
  const auto status = [&](core::OrderId order_id) {
    return world.orders.rows[core::FindRow(world.orders, order_id)];
  };
  failures += Expect(status(went).status == core::OrderStatus::kDone &&
                         world.funerals.rows[0].chairman == core::FuneralChairman::kWent,
                     "funeral: «Пойти» books «went»");
  failures += Expect(status(again).refusal == core::OrderRefusal::kRuleForbids &&
                         status(none).refusal == core::OrderRefusal::kNoSuchSubject,
                     "funeral: a second «Пойти» is refused, and one for no funeral");
  bool kin = false;
  world.calendar.day = kToday + 1;
  failures += Expect(core::FuneralOfToday(world, mother, kin) == 0 && kin,
                     "funeral: on the burial day the mother is at it, as kin");
  core::RunFuneralDay(config, world);
  failures += Expect(world.funerals.rows.size() == 1, "funeral: it stands on its burial day");
  world.calendar.day = kToday + 2;
  core::RunFuneralDay(config, world);
  failures += Expect(world.funerals.rows.empty(), "funeral: it closes the day after");
  const core::FamilyRow& household = world.families.rows[core::FindRow(world.families, home)];
  failures += Expect(household.funeral_satisfaction == 3.0F &&
                         household.funeral_satisfaction_until == kToday + 2 + core::kDaysPerMonth,
                     "funeral: his presence gives the household +3 for a month");
  const float term = core::FuneralMoodTerm(config, Row(world, mother), kToday + 2);
  failures += Expect(std::fabs(term - (-10.0F * 0.75F + 10.0F)) < 1e-4F,
                     "funeral: and the mother +10 on what was left of her grief");
  failures += Expect(Row(world, mother).funeral_mood_applied == term,
                     "funeral: the day folds the term into the mood");
  // MISSED: a loud one, not attended, the chairman in the village.
  core::WorldState missed = Empty();
  const core::FamilyId yard = Household(missed, 0.0F, 1);
  const core::ResidentId child = Person(missed, yard, kChildBorn);
  const core::ResidentId father = Person(missed, yard, kAdultBorn);
  Row(missed, child).father = father;
  core::OpenFunerals(config, missed, std::vector<core::ResidentId>{child});
  missed.calendar.day = kToday + 2;
  core::RunFuneralDay(config, missed);
  failures += Expect(
      missed.families.rows[core::FindRow(missed.families, yard)].funeral_satisfaction == -3.0F,
      "funeral: a loud one missed costs the household -3");
  return failures;
}

}  // namespace

int CheckFunerals() {
  int failures = 0;
  failures += CheckTheParty();
  failures += CheckTheJointFuneral();
  failures += CheckTheLoudCap();
  failures += CheckTheChairman();
  return failures;
}
