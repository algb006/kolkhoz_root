// Unit test of core_construction: a unit marked, started, delivered to,
// built, upgraded and taken down — plus every refusal the subsystem can
// give, and the crew ceiling that keeps a brigade a brigade.
//
// The tables are in-memory: five little ones with exactly the columns the
// parser reads. That is on purpose — the point of this test is the rules,
// and a test that needs tables/ on disk stops being a unit test.

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../common/fake_tables.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_construction/construction_system.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

static_assert(std::is_abstract_v<core::IConstructionSystem>, "IConstructionSystem is a contract");
static_assert(static_cast<int>(core::ConstructionPhase::kNone) == 0,
              "a zeroed unit row is a unit that is simply standing");

namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// The five tables the subsystem reads, and nothing else.
class BuildTables final : public core::ITableSet {
 public:
  const core::ITable* FindTable(std::string_view name) const override {
    if (name == "unit_types") {
      return &types_;
    }
    if (name == "unit_levels") {
      return &levels_;
    }
    if (name == "unit_level_cost") {
      return &costs_;
    }
    if (name == "resources") {
      return &resources_;
    }
    if (name == "construction") {
      return &knobs_;
    }
    return nullptr;
  }

  std::uint32_t TableCount() const override { return 5; }

  std::string_view TableName(std::uint32_t /*index*/) const override { return {}; }

 private:
  // Row 0 is the store the materials come from, row 1 the barn we build,
  // row 2 an Epoch-II type whose gate is shut, row 3 an outline the player
  // draws (build class "plot": no work, no materials).
  // has_wear and wear_factor are task A5's: the orchard is an outline with
  // nothing to wear, the barn ages half again as fast as its class (damp,
  // animals), and old_house is the one type that collapses.
  test::FakeTable types_{{"key",
                          "era",
                          "player_built",
                          "gate",
                          "has_plot",
                          "plot_radius_m",
                          "has_wear",
                          "wear_factor",
                          "footprint_r_m"},
                         {{"store", "1", "0", "start", "1", "10", "1", "", ""},
                          {"barn", "1", "1", "era", "1", "20", "1", "1.5", ""},
                          {"club", "2", "1", "era", "1", "20", "1", "", ""},
                          {"orchard", "1", "1", "era", "1", "", "0", "", ""},
                          {"old_house", "1", "0", "start", "1", "10", "1", "", ""},
                          // A well: no plot, but a body of 1.2 m that
                          // nothing else may stand inside (task of
                          // 2026-09-05). has_plot is 0 and that is now three
                          // different facts, not one.
                          {"well", "1", "1", "era", "0", "", "0", "", "1.2"}}};

  // wear_factor is the STEP's pace over its class's term, and it stands on
  // three rows on purpose: the store carries it where the type has none
  // (1.0 x 1.1), the barn's second step carries it where the type has 1.5
  // (1.5 x 1.1 — the product, which one factor alone cannot show), and the
  // old house carries it where the code used to hard-code 1.0.
  //
  // 70 real man-days is 10 game man-days (root rules §9: real / 7).
  // Ten years standing, five in use: IN USE IS THE SHORTER TERM (unit rules
  // §15, and boss corrected his own criterion on it). Round numbers so the
  // daily share is exact arithmetic in the test below.
  test::FakeTable levels_{{"unit",
                           "level",
                           "era",
                           "labor_days",
                           "build_class",
                           "max_crew",
                           "wear_years_idle",
                           "wear_years_in_use",
                           "wear_factor"},
                          {{"store", "1", "1", "70", "wood_small", "5", "10", "5", "1.1"},
                           {"barn", "1", "1", "70", "wood_small", "5", "10", "5", ""},
                           {"barn", "2", "1", "140", "wood_small_ext", "8", "20", "10", "1.1"},
                           {"club", "1", "2", "70", "wood_small", "5", "10", "5", ""},
                           {"orchard", "1", "1", "0", "plot", "", "", "", ""},
                           {"old_house", "1", "1", "70", "wood_small", "5", "10", "5", "1.1"},
                           {"well", "1", "1", "14", "earthwork", "2", "", "", ""}}};

  test::FakeTable costs_{{"unit", "level", "resource", "amount"},
                         {{"barn", "1", "log", "10"}, {"barn", "2", "log", "20"}}};

  test::FakeTable resources_{{"key", "measure", "kg_per_unit"},
                             {{"log", "pcs", "200"}, {"spare_part", "pcs", "5"}}};

  test::FakeTable knobs_{{"key", "value"},
                         {{"demolition_labor_share", "0.5"},
                          {"repair_labor_share", "0.5"},
                          {"repair_spare_parts_per_labor_day", "1"},
                          {"old_house_collapse_years", "2"}}};
};

constexpr std::uint16_t kStoreType = 0;
constexpr std::uint16_t kBarnType = 1;
constexpr std::uint16_t kClubType = 2;
constexpr std::uint16_t kOrchardType = 3;
constexpr std::uint16_t kOldHouseType = 4;

constexpr std::uint16_t kWellType = 5;

/// Grams of one spare part, as the fixture states it: 5 kg a piece.
constexpr core::Grams kPartGrams = 5 * core::kGramsPerKilogram;

/// Grams of one log, as the table states it: 200 kg a piece.
constexpr core::Grams kLogGrams = 200 * core::kGramsPerKilogram;

core::UnitId PlaceStore(core::WorldState& world, core::Grams logs) {
  core::UnitRow store;
  store.type = core::UnitTypeId{kStoreType};
  store.position = core::Vec2{.x = 500.0F, .y = 500.0F};
  store.stock.assign(1, logs);
  return core::AppendRow(world.units, store);
}

core::OrderId Issue(core::WorldState& world, const core::OrderRow& order) {
  return core::AppendRow(world.orders, order);
}

core::OrderRow BuildOrder(std::uint16_t type, float x, float y) {
  core::OrderRow order;
  order.kind = core::OrderKind::kBuildUnit;
  order.unit_type = core::UnitTypeId{type};
  order.position = core::Vec2{.x = x, .y = y};
  return order;
}

core::OrderRow UnitOrder(core::OrderKind kind, core::UnitId unit) {
  core::OrderRow order;
  order.kind = kind;
  order.unit = unit;
  return order;
}

core::OrderRefusal RefusalOf(const core::WorldState& world, core::OrderId order) {
  const std::uint32_t row = core::FindRow(world.orders, order);
  return row == core::kNoRow ? core::OrderRefusal::kNone : world.orders.rows[row].refusal;
}

/// One step of the sub-step, at the hour it is given: hour 0 is when the
/// delivery stub runs.
void Run(core::IConstructionSystem& system, core::WorldState& world, std::uint32_t hour) {
  world.calendar.tick = hour;
  const core::WorldState previous = world;
  system.RunConstructionDecisions(previous, world);
}

int TestMarkAndBuild(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  if (!system) {
    std::cout << "FAIL: the subsystem refused its tables\n";
    return 1;
  }
  core::WorldState world;
  const core::UnitId store = PlaceStore(world, 30 * kLogGrams);

  // Marking: the plot is taken, nothing is spent, and the row waits.
  const core::OrderId marked = Issue(world, BuildOrder(kBarnType, 1000.0F, 1000.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, marked) == core::OrderRefusal::kNone, "a barn may be marked");
  failures += Expect(world.units.rows.size() == 2, "the site is a unit row");
  const core::UnitId site = world.units.row_ids.back();
  const core::UnitRow& site_row = world.units.rows[core::FindRow(world.units, site)];
  failures +=
      Expect(site_row.level == 0 && site_row.construction.phase == core::ConstructionPhase::kMarked,
             "a marked site is a level-0 unit");
  failures += Expect(site_row.stock.empty(), "and nothing has been spent on it");

  // Nothing happens until the chairman says so.
  Run(*system, world, 0);
  failures += Expect(world.units.rows[core::FindRow(world.units, site)].construction.phase ==
                         core::ConstructionPhase::kMarked,
                     "the works do not start by themselves when materials exist");

  const core::OrderId started = Issue(world, UnitOrder(core::OrderKind::kStartBuild, site));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, started) == core::OrderRefusal::kNone, "the works start");
  const core::UnitRow& building = world.units.rows[core::FindRow(world.units, site)];
  failures += Expect(building.construction.phase == core::ConstructionPhase::kBuilding,
                     "the recipe was on hand, so delivery finished in the same day");
  failures += Expect(building.construction.labor_days_remaining > 9.9F &&
                         building.construction.labor_days_remaining < 10.1F,
                     "70 real man-days is 10 game man-days of work left");
  failures +=
      Expect(building.construction.max_crew == 5, "the class's brigade rides with the site");
  failures += Expect(world.units.rows[core::FindRow(world.units, store)].stock[0] == 20 * kLogGrams,
                     "ten logs left the store for the site");

  // The labour seam is the labor sub-step's to drain; here it is drained by
  // hand, which is exactly what a crew does over its days.
  world.units.rows[core::FindRow(world.units, site)].construction.labor_days_remaining = 0.0F;
  Run(*system, world, 5);
  const core::UnitRow& built = world.units.rows[core::FindRow(world.units, site)];
  failures += Expect(built.level == 1 && built.construction.phase == core::ConstructionPhase::kNone,
                     "at zero the level moves and the site is a unit");
  failures += Expect(built.stock.empty() || built.stock[0] == 0,
                     "and the recipe is consumed, not left lying on the site");
  bool announced = false;
  for (const core::SimEvent& event : world.step_events) {
    announced =
        announced || (event.kind == core::EventKind::kUnitBuilt && event.unit.value == site.value);
  }
  failures += Expect(announced, "the world is told a unit was built");

  // An upgrade: the unit keeps working at its level while the next is built.
  const core::OrderId upgrade = Issue(world, UnitOrder(core::OrderKind::kUpgradeUnit, site));
  Run(*system, world, 0);
  failures +=
      Expect(RefusalOf(world, upgrade) == core::OrderRefusal::kNone, "a barn may be raised");
  const core::UnitRow& raising = world.units.rows[core::FindRow(world.units, site)];
  failures += Expect(raising.level == 1, "and it stands at its old level while it is raised");
  failures +=
      Expect(raising.construction.target_level == 2, "the site names the level being built");
  failures += Expect(raising.construction.phase == core::ConstructionPhase::kBuilding,
                     "twenty more logs were on hand");
  failures +=
      Expect(raising.construction.max_crew == 8, "the bigger class allows a bigger brigade");
  return failures;
}

int TestRefusals(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  core::WorldState world;
  PlaceStore(world, 0);

  const core::OrderId shut = Issue(world, BuildOrder(kClubType, 2000.0F, 2000.0F));
  const core::OrderId unknown = Issue(world, BuildOrder(200, 2500.0F, 2000.0F));
  const core::OrderId standing = Issue(world, BuildOrder(kStoreType, 3000.0F, 2000.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, shut) == core::OrderRefusal::kGateClosed,
                     "an Epoch-II type is refused in Epoch I, by its gate");
  failures += Expect(RefusalOf(world, unknown) == core::OrderRefusal::kNoSuchSubject,
                     "a type the tables do not have is refused");
  failures += Expect(RefusalOf(world, standing) == core::OrderRefusal::kRuleForbids,
                     "what stands from day one is not built by the player");

  // The plot: two units may not stand closer than the sum of their radii.
  const core::OrderId first = Issue(world, BuildOrder(kBarnType, 1000.0F, 1000.0F));
  Run(*system, world, 0);
  const core::OrderId crowded = Issue(world, BuildOrder(kBarnType, 1030.0F, 1000.0F));
  const core::OrderId apart = Issue(world, BuildOrder(kBarnType, 1100.0F, 1000.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, first) == core::OrderRefusal::kNone, "the first barn fits");
  failures += Expect(RefusalOf(world, crowded) == core::OrderRefusal::kTooClose,
                     "the second is refused thirty metres from it, forty being the sum");
  failures += Expect(RefusalOf(world, apart) == core::OrderRefusal::kNone,
                     "and accepted a hundred metres away");

  // An outline the player draws costs nothing and is finished at once.
  const core::OrderId orchard = Issue(world, BuildOrder(kOrchardType, 4000.0F, 4000.0F));
  Run(*system, world, 0);
  const core::UnitId orchard_id = world.units.row_ids.back();
  Issue(world, UnitOrder(core::OrderKind::kStartBuild, orchard_id));
  Run(*system, world, 0);
  failures +=
      Expect(RefusalOf(world, orchard) == core::OrderRefusal::kNone, "an orchard is marked");
  failures += Expect(world.units.rows[core::FindRow(world.units, orchard_id)].level == 1,
                     "and an outline needs no work: it stands the moment it is started");
  return failures;
}

int TestDemolition(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  core::WorldState world;
  const core::UnitId store = PlaceStore(world, 10 * kLogGrams);

  // A marked contour goes at once and for free.
  Issue(world, BuildOrder(kBarnType, 1000.0F, 1000.0F));
  Run(*system, world, 0);
  const core::UnitId contour = world.units.row_ids.back();
  Issue(world, UnitOrder(core::OrderKind::kDemolishUnit, contour));
  Run(*system, world, 0);
  failures += Expect(core::FindRow(world.units, contour) == core::kNoRow,
                     "a contour nobody has spent anything on is simply removed");

  // A standing unit with a herd at it is not demolished: the living is not
  // demolished (unit rules §14).
  core::HerdRow herd;
  herd.unit = store;
  core::AppendRow(world.herds, herd);
  const core::OrderId alive = Issue(world, UnitOrder(core::OrderKind::kDemolishUnit, store));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, alive) == core::OrderRefusal::kNotEmpty,
                     "a barn with a herd standing in it is refused");
  world.herds.rows.clear();
  world.herds.row_ids.clear();
  core::RebuildLookup(world.herds);

  // Build a barn to take down, then take it down: the stock leaves first.
  Issue(world, BuildOrder(kBarnType, 2000.0F, 2000.0F));
  Run(*system, world, 0);
  const core::UnitId barn = world.units.row_ids.back();
  Issue(world, UnitOrder(core::OrderKind::kStartBuild, barn));
  Run(*system, world, 0);
  world.units.rows[core::FindRow(world.units, barn)].construction.labor_days_remaining = 0.0F;
  Run(*system, world, 1);
  const core::OrderId down = Issue(world, UnitOrder(core::OrderKind::kDemolishUnit, barn));
  Run(*system, world, 0);
  failures +=
      Expect(RefusalOf(world, down) == core::OrderRefusal::kNone, "an empty barn comes down");
  const core::UnitRow& falling = world.units.rows[core::FindRow(world.units, barn)];
  failures += Expect(falling.level == 0, "a unit being taken down is a level-0 unit at once");
  // Half of the level's ten game man-days, by the knob this test's table sets.
  failures += Expect(falling.construction.labor_days_remaining > 4.9F &&
                         falling.construction.labor_days_remaining < 5.1F,
                     "taking down costs a share of building, not the whole of it");
  world.units.rows[core::FindRow(world.units, barn)].construction.labor_days_remaining = 0.0F;
  Run(*system, world, 2);
  failures += Expect(core::FindRow(world.units, barn) == core::kNoRow, "and then the row is gone");
  bool announced = false;
  for (const core::SimEvent& event : world.step_events) {
    announced = announced || event.kind == core::EventKind::kUnitDemolished;
  }
  failures += Expect(announced, "the world is told a unit came down");
  return failures;
}

/// A table set with no tables at all: nothing can be built, and that is the

/// Task A5: the building ages, and it ages at two speeds. Ten years empty,
/// five in use, and the barn's own pace is one and a half — so a day of
/// standing empty is 100 / (10 x 1.5 x 48) and a day in use is twice that.
int TestWearGrows(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  if (system == nullptr) {
    std::cout << "FAIL: the wear table set builds no system\n";
    return 1;
  }
  core::WorldState world;
  core::UnitRow barn;
  barn.type = core::UnitTypeId{kBarnType};
  barn.level = 1;
  const core::UnitId empty = core::AppendRow(world.units, barn);
  core::UnitRow lived_in;
  lived_in.type = core::UnitTypeId{kBarnType};
  lived_in.level = 1;
  lived_in.household = core::FamilyId{7};  // somebody lives here
  const core::UnitId busy = core::AppendRow(world.units, lived_in);
  core::UnitRow orchard;
  orchard.type = core::UnitTypeId{kOrchardType};
  orchard.level = 1;
  const core::UnitId nothing_to_wear = core::AppendRow(world.units, orchard);
  core::UnitRow site;
  site.type = core::UnitTypeId{kBarnType};
  site.level = 0;  // a marked site: not a building yet
  const core::UnitId unbuilt = core::AppendRow(world.units, site);

  const auto wear_of = [&](core::UnitId id) {
    const std::uint32_t row = core::FindRow(world.units, id);
    return row == core::kNoRow ? -1.0F : world.units.rows[row].wear;
  };

  world.calendar.tick = 0;
  Run(*system, world, 0);
  const float idle_day = wear_of(empty);
  const float used_day = wear_of(busy);
  // A FASTER pace is a shorter life, so the type's factor multiplies the
  // daily share: 100 / (10 years x 48 days) x 1.5.
  const float expected_idle = 100.0F / (10.0F * 48.0F) * 1.5F;
  failures += Expect(idle_day > expected_idle * 0.999F && idle_day < expected_idle * 1.001F,
                     "a day standing empty is the level's idle term, at the type's own pace");
  failures += Expect(used_day > idle_day * 1.99F && used_day < idle_day * 2.01F,
                     "and a day in use wears twice as fast: work consumes, standing preserves");
  failures += Expect(wear_of(nothing_to_wear) == 0.0F, "an outline with no building never wears");
  failures += Expect(wear_of(unbuilt) == 0.0F, "and neither does a site that is not built yet");

  // Only at the day boundary: an hour is not a day.
  Run(*system, world, 5);
  failures += Expect(wear_of(empty) == idle_day, "wear moves once a day, not once an hour");

  // Something merely LYING in a unit keeps it in use (boss, 2026-09-03).
  const std::uint32_t empty_row = core::FindRow(world.units, empty);
  world.units.rows[empty_row].stock.assign(1, kLogGrams);
  Run(*system, world, 0);
  failures += Expect(wear_of(empty) - idle_day > idle_day * 1.9F,
                     "a store with grain in it is not abandoned, and wears like it is used");

  // Task A8: A STOPPED UNIT DOES NOT WEAR (unit rules §15, and §5 lists it
  // among what a pause does). This is the one effect of a pause the slice
  // can show, so it is the one that has to be measured — and measured
  // BESIDE a running twin, because "nothing moved" is also what a broken
  // day boundary looks like.
  core::UnitRow stopped_row;
  stopped_row.type = core::UnitTypeId{kBarnType};
  stopped_row.level = 1;
  stopped_row.paused = 1;
  const core::UnitId stopped = core::AppendRow(world.units, stopped_row);
  const float running_before = wear_of(empty);
  Run(*system, world, core::kTicksPerDay);  // hour 0 of the next day: a wear boundary
  failures += Expect(wear_of(stopped) == 0.0F, "a stopped unit does not wear out");
  failures += Expect(wear_of(empty) > running_before,
                     "while the one standing beside it wore that same day");
  return failures;
}

/// A table set with no `has_wear` column at all, for the third refusal.
class NoWearColumnTables final : public core::ITableSet {
 public:
  const core::ITable* FindTable(std::string_view name) const override {
    return name == "unit_types" ? &types_ : inner_.FindTable(name);
  }

  std::uint32_t TableCount() const override { return inner_.TableCount(); }

  std::string_view TableName(std::uint32_t index) const override { return inner_.TableName(index); }

 private:
  BuildTables inner_;

  // The same rows, minus the column. Absent is not the same as zero: zero
  // says "this unit has nothing to wear", absent says "nobody wrote it down".
  test::FakeTable types_{{"key", "era", "player_built", "gate", "has_plot", "plot_radius_m"},
                         {{"store", "1", "0", "start", "1", "10"},
                          {"barn", "1", "1", "era", "1", "20"},
                          {"club", "2", "1", "era", "1", "20"},
                          {"orchard", "1", "1", "era", "1", ""},
                          {"old_house", "1", "0", "start", "1", "10"},
                          // The ladder is the inner set's and names every
                          // type in it; a level row naming a type this
                          // roster lacks refuses the whole config, and the
                          // refusal would be right.
                          {"well", "1", "1", "era", "0", ""}}};
};

/// THE DEADLINE MUST AGREE WITH THE WORLD, and that is the only test of a
/// forecast worth writing: it promises a number of days, so run those days
/// and see whether the unit is where it was promised to be. Checking a
/// forecast against its own formula checks the formula against itself.
///
/// THE BARN STARTS AT SEVEN, NOT AT ZERO, and that is the whole point of the
/// number. At zero the daily share divides the scale exactly — 320 days
/// either way — and the first draft of this test used it, so it could not
/// see rounding at all. At seven the division says 297 and the world takes
/// 298: the forecast has to name the day the world reaches, not the day
/// before it.
///
/// The three refusals are checked here too, because they are not the same
/// refusal (core_common/deadline.h): a stopped unit and a site will wear
/// later (kNever), a stack never will (kNotApplicable), and a table with no
/// wear column says nothing at all (kNoData).
int TestWearDeadline(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  if (system == nullptr) {
    std::cout << "FAIL: the deadline table set builds no system\n";
    return 1;
  }
  core::WorldState world;
  core::UnitRow barn;
  barn.type = core::UnitTypeId{kBarnType};
  barn.level = 1;
  barn.wear = 7.0F;  // the corner: 297.6 days away, so 298 and not 297
  const core::UnitId ageing = core::AppendRow(world.units, barn);
  core::UnitRow stopped = barn;
  stopped.paused = 1;
  const core::UnitId halted = core::AppendRow(world.units, stopped);
  core::UnitRow orchard;
  orchard.type = core::UnitTypeId{kOrchardType};
  orchard.level = 1;
  const core::UnitId outline = core::AppendRow(world.units, orchard);
  core::UnitRow site;
  site.type = core::UnitTypeId{kBarnType};
  site.level = 0;
  const core::UnitId unbuilt = core::AppendRow(world.units, site);
  core::UnitRow finished;
  finished.type = core::UnitTypeId{kBarnType};
  finished.level = 1;
  finished.wear = 100.0F;
  finished.paused = 1;  // worn out AND stopped: the limit outranks the pause
  const core::UnitId spent = core::AppendRow(world.units, finished);

  failures += Expect(system->WearDeadline(world, halted).kind == core::DeadlineKind::kNever,
                     "a stopped unit is not wearing — an answer that changes when it starts");
  failures += Expect(system->WearDeadline(world, unbuilt).kind == core::DeadlineKind::kNever,
                     "and a site is not either: it will wear the day it is built, so the "
                     "question is not to be dropped");
  failures +=
      Expect(system->WearDeadline(world, outline).kind == core::DeadlineKind::kNotApplicable,
             "an outline with no building has no wear, today or ever");
  failures += Expect(
      system->WearDeadline(world, core::UnitId{404}).kind == core::DeadlineKind::kNotApplicable,
      "a unit that does not exist raises no question");
  const core::Deadline done = system->WearDeadline(world, spent);
  failures += Expect(done.kind == core::DeadlineKind::kDays && done.days == 0,
                     "a unit already at the end of the scale is there, stopped or not");

  const core::Deadline promised = system->WearDeadline(world, ageing);
  failures += Expect(promised.kind == core::DeadlineKind::kDays && promised.days == 298,
                     "the deadline names the day the world REACHES the limit, not the day "
                     "before: dividing gives 297.6 and truncating it would say 297");

  const auto wear_of = [&](core::UnitId id) {
    const std::uint32_t row = core::FindRow(world.units, id);
    return row == core::kNoRow ? -1.0F : world.units.rows[row].wear;
  };
  // Both sides of the promise. One day short must NOT be there, and the
  // promised day must — a test that only checks the second passes for any
  // forecast that is early.
  for (std::int32_t step = 1; step < promised.days; ++step) {
    Run(*system, world, static_cast<std::uint32_t>(step) * core::kTicksPerDay);
  }
  failures += Expect(wear_of(ageing) < 100.0F, "the day before the promised one it is not there");
  Run(*system, world, static_cast<std::uint32_t>(promised.days) * core::kTicksPerDay);
  failures += Expect(wear_of(ageing) >= 100.0F, "and on the promised day it is");
  failures += Expect(wear_of(halted) == 7.0F, "while the stopped one has not moved at all");

  // The third refusal: no column, so nothing is known about wear at all.
  const NoWearColumnTables silent;
  const auto blind = core::CreateConstructionSystem(silent, core::StubTables::kAllowed);
  if (blind != nullptr) {
    failures += Expect(blind->WearDeadline(world, ageing).kind == core::DeadlineKind::kNoData,
                       "no has_wear column is NO DATA — not 'nothing wears', which is what "
                       "the same zero used to mean");
  } else {
    failures += Expect(false, "the column-less table set builds a system");
  }
  return failures;
}

/// The scale stops at 100 and the unit goes on working — except the start's
/// old houses, which are the one kind that falls (start design §4).
int TestWearCeilingAndCollapse(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  core::WorldState world;
  core::UnitRow barn;
  barn.type = core::UnitTypeId{kBarnType};
  barn.level = 1;
  barn.wear = 99.99F;
  const core::UnitId ruin = core::AppendRow(world.units, barn);

  core::FamilyRow family;
  const core::FamilyId household = core::AppendRow(world.families, family);
  core::UnitRow old_house;
  old_house.type = core::UnitTypeId{kOldHouseType};
  old_house.level = 1;
  old_house.wear = 99.99F;
  old_house.household = household;
  const core::UnitId doomed = core::AppendRow(world.units, old_house);
  world.families.rows[core::FindRow(world.families, household)].house = doomed;

  Run(*system, world, 0);
  const std::uint32_t ruin_row = core::FindRow(world.units, ruin);
  failures += Expect(ruin_row != core::kNoRow && world.units.rows[ruin_row].wear == 100.0F,
                     "wear stops at a hundred: a ruin still stands and still works");
  failures += Expect(core::FindRow(world.units, doomed) == core::kNoRow,
                     "an old house at the top of the scale falls — the one unit that does");
  bool said_so = false;
  for (const core::SimEvent& event : world.step_events) {
    said_so = said_so ||
              (event.kind == core::EventKind::kUnitCollapsed && event.unit.value == doomed.value);
  }
  failures += Expect(said_so, "and it says so, rather than vanishing quietly");
  const std::uint32_t family_row = core::FindRow(world.families, household);
  failures += Expect(world.families.rows[family_row].house.value == 0,
                     "the family it sheltered is left pointing at no house, not at a dead id");

  // Another day must not fall over the hole it left.
  Run(*system, world, 0);
  failures +=
      Expect(core::FindRow(world.units, ruin) != core::kNoRow, "and the day after is uneventful");
  return failures;
}

/// The fifth order: a repair is a site on a standing unit, paid for by the
/// wear it was ordered at, made of spare parts and nothing else.
int TestRepair(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  core::WorldState world;
  // 200 parts in the store, which is more than any repair here asks for.
  core::UnitRow store;
  store.type = core::UnitTypeId{kStoreType};
  store.level = 1;
  store.stock.assign(2, 0);
  store.stock[1] = 200 * kPartGrams;
  core::AppendRow(world.units, store);

  core::UnitRow barn;
  barn.type = core::UnitTypeId{kBarnType};
  barn.level = 1;
  barn.wear = 50.0F;
  const core::UnitId worn = core::AppendRow(world.units, barn);

  const core::OrderId order = Issue(world, UnitOrder(core::OrderKind::kRepairUnit, worn));
  Run(*system, world, 0);
  const std::uint32_t row = core::FindRow(world.units, worn);
  const core::ConstructionState& site = world.units.rows[row].construction;
  // 10 game man-days of build norm x 0.5 share x 50/100 of wear = 2.5.
  failures += Expect(site.labor_days_total > 2.49F && site.labor_days_total < 2.51F,
                     "a repair costs the level's norm by the share, scaled by the wear it had");
  failures += Expect(site.target_level == world.units.rows[row].level,
                     "and the level does not move: the site says so out loud");
  failures += Expect(site.phase == core::ConstructionPhase::kRepairing,
                     "the parts were in the store, so the delivery is already done");
  failures += Expect(world.units.rows[row].stock.size() > 1 && world.units.rows[row].stock[1] > 0,
                     "the spare parts are on site");
  failures += Expect(RefusalOf(world, order) == core::OrderRefusal::kNone, "and nothing refused");

  // The labour is invested by somebody else's sub-step; here we just finish.
  world.units.rows[row].construction.labor_days_remaining = 0.0F;
  Run(*system, world, 1);
  failures += Expect(world.units.rows[row].wear == 0.0F, "a finished repair takes the wear away");
  failures += Expect(world.units.rows[row].construction.phase == core::ConstructionPhase::kNone,
                     "and the site is gone");
  failures += Expect(world.units.rows[row].stock[1] == 0,
                     "the parts were used up, not left lying on the site");
  bool repaired = false;
  for (const core::SimEvent& event : world.step_events) {
    repaired = repaired || event.kind == core::EventKind::kUnitRepaired;
  }
  failures += Expect(repaired, "and the outbox says so");

  // Refusals: nothing worn, nothing to wear, and the old houses.
  const core::OrderId again = Issue(world, UnitOrder(core::OrderKind::kRepairUnit, worn));
  core::UnitRow orchard;
  orchard.type = core::UnitTypeId{kOrchardType};
  orchard.level = 1;
  orchard.wear = 0.0F;
  const core::UnitId outline = core::AppendRow(world.units, orchard);
  const core::OrderId no_wear = Issue(world, UnitOrder(core::OrderKind::kRepairUnit, outline));
  core::UnitRow old_house;
  old_house.type = core::UnitTypeId{kOldHouseType};
  old_house.level = 1;
  old_house.wear = 60.0F;
  const core::UnitId ancient = core::AppendRow(world.units, old_house);
  const core::OrderId unfixable = Issue(world, UnitOrder(core::OrderKind::kRepairUnit, ancient));
  Run(*system, world, 2);
  failures += Expect(RefusalOf(world, again) == core::OrderRefusal::kRuleForbids,
                     "a unit with nothing worn is not repaired");
  failures += Expect(RefusalOf(world, no_wear) == core::OrderRefusal::kRuleForbids,
                     "nor is one with nothing to wear");
  failures += Expect(RefusalOf(world, unfixable) == core::OrderRefusal::kRuleForbids,
                     "and an old house is replaced, never mended");
  return failures;
}

/// "Any level upgrade repairs the unit entirely" (unit rules §11).
int TestUpgradeHeals(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  core::WorldState world;
  PlaceStore(world, 100 * kLogGrams);
  core::UnitRow barn;
  barn.type = core::UnitTypeId{kBarnType};
  barn.level = 1;
  barn.wear = 80.0F;
  const core::UnitId unit = core::AppendRow(world.units, barn);
  Issue(world, UnitOrder(core::OrderKind::kUpgradeUnit, unit));
  Run(*system, world, 0);
  const std::uint32_t row = core::FindRow(world.units, unit);
  // Still worn — and a shade worse, because the day that opened the site
  // also aged the barn. What matters is that nothing healed.
  failures += Expect(world.units.rows[row].wear >= 80.0F,
                     "an upgrade under way has not healed anything yet");
  world.units.rows[row].construction.labor_days_remaining = 0.0F;
  Run(*system, world, 1);
  failures += Expect(world.units.rows[row].level == 2 && world.units.rows[row].wear == 0.0F,
                     "and the finished upgrade repairs the unit on its way");
  return failures;
}

int TestTableLessWorld() {
  int failures = 0;
  const test::FakeTableSet tables;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  failures += Expect(system != nullptr, "a table-less world still gets a subsystem");
  if (!system) {
    return failures;
  }
  core::WorldState world;
  const core::OrderId nothing = Issue(world, BuildOrder(0, 100.0F, 100.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, nothing) == core::OrderRefusal::kNoSuchSubject,
                     "and it refuses everything, naming the reason");
  failures += Expect(world.units.rows.empty(), "nothing was placed");
  return failures;
}

/// A unit with no plot still has a BODY, and nothing may stand inside it.
///
/// `has_plot = 0` used to answer three questions with one word: no yard to
/// keep clear, no body to bump into, and no rule about where the thing may
/// stand. The first is true of a well, the second is not — two wells could
/// occupy the same metre, and the human named it: "two similar objects will
/// overlap each other… that is not decor" (boss relaying, 2026-09-05).
///
/// It is the same rule with a different number, so the same comparison
/// answers it: the type's plot where it has one, its footprint where it does
/// not. The third question — `placement_ref`: verge, wall, route — refers to
/// roads and walls the world does not have yet, and is deliberately unread.
int TestABodyKeepsItsMetre(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  if (system == nullptr) {
    std::cout << "FAIL: the body table set builds no system\n";
    return 1;
  }
  core::WorldState world;
  const core::OrderId first = Issue(world, BuildOrder(kWellType, 1000.0F, 1000.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, first) == core::OrderRefusal::kNone,
                     "the first well is dug where the chairman said");

  // Half a metre away: inside 1.2 + 1.2, so the two bodies would share
  // ground. Before the body was read this was allowed, and silently.
  const core::OrderId on_top = Issue(world, BuildOrder(kWellType, 1000.5F, 1000.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, on_top) == core::OrderRefusal::kTooClose,
                     "a second well half a metre away is refused: bodies do not overlap");

  // Three metres away: clear of 2.4, and legal. Without this the check above
  // would only be proving that the second well is always refused.
  const core::OrderId beside = Issue(world, BuildOrder(kWellType, 1003.0F, 1000.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, beside) == core::OrderRefusal::kNone,
                     "and three metres away it is dug: a body is small, not a plot");

  // A body is kept out of a PLOT as well — the barn's twenty metres are the
  // barn's, and a well is not exempt for being little.
  const core::OrderId yard = Issue(world, BuildOrder(kBarnType, 2000.0F, 2000.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, yard) == core::OrderRefusal::kNone, "the barn goes up");
  const core::OrderId inside = Issue(world, BuildOrder(kWellType, 2010.0F, 2000.0F));
  Run(*system, world, 0);
  failures += Expect(RefusalOf(world, inside) == core::OrderRefusal::kTooClose,
                     "and a well ten metres into its yard is refused");
  return failures;
}

/// The step's pace multiplies the type's, and the old house is not exempt.
///
/// Two facts, two columns, one product: the type says what the NATURE of a
/// unit does to it (a byre is damp, a mill shakes), the step says what it
/// STANDS ON (a timber frame on wooden stools lives shorter than the same
/// frame on stone — 1.1 across twenty-seven Epoch I first steps). A unit
/// that is both damp and badly founded is worse than one that is either, so
/// they multiply (boss, 2026-09-05).
///
/// ONE FACTOR CANNOT SHOW A PRODUCT, so the barn's second step is measured:
/// its type is 1.5 and its step is 1.1, and 1.65 is a different number from
/// either. And the old house is measured because the code used to write 1.0
/// for it in so many words — its own pace is empty (a hut has no nature that
/// ages it) but its stools are the worst in the village, and its collapse
/// term answers a different question, so applying the step to it is not one
/// fact counted twice.
int TestStepPaceMultipliesTypePace(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system =
      core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  if (system == nullptr) {
    std::cout << "FAIL: the wear table set builds no system\n";
    return 1;
  }
  core::WorldState world;
  core::UnitRow store;
  store.type = core::UnitTypeId{kStoreType};
  store.level = 1;
  const core::UnitId founded = core::AppendRow(world.units, store);
  core::UnitRow big_barn;
  big_barn.type = core::UnitTypeId{kBarnType};
  big_barn.level = 2;
  const core::UnitId both = core::AppendRow(world.units, big_barn);
  core::UnitRow hut;
  hut.type = core::UnitTypeId{kOldHouseType};
  hut.level = 1;
  const core::UnitId ancient = core::AppendRow(world.units, hut);

  world.calendar.tick = 0;
  Run(*system, world, 0);
  const auto wear_of = [&world](core::UnitId id) {
    const std::uint32_t row = core::FindRow(world.units, id);
    return row == core::kNoRow ? -1.0F : world.units.rows[row].wear;
  };
  const auto near = [](float value, float expected) {
    return value > expected * 0.999F && value < expected * 1.001F;
  };

  // Ten years idle, no pace of its own, a step of 1.1.
  failures += Expect(near(wear_of(founded), 100.0F / (10.0F * 48.0F) * 1.1F),
                     "a step's pace applies where the type names none");
  // Twenty years idle at the second step, type 1.5 times step 1.1.
  failures += Expect(near(wear_of(both), 100.0F / (20.0F * 48.0F) * 1.5F * 1.1F),
                     "and it multiplies the type's rather than replacing it");
  // Two years to collapse, no pace of its own, a step of 1.1. This one was
  // 1.0 by a branch in the code until 2026-09-05.
  failures += Expect(near(wear_of(ancient), 100.0F / (2.0F * 48.0F) * 1.1F),
                     "the old house wears at its stools' pace, not at one");
  return failures;
}

/// A capacity that no level row answers for must STOP the load.
///
/// The type row used to carry a storage figure of its own, and the export
/// filled it with a copy of level 1 (`LEFT JOIN unit_level ON level = 1`) —
/// so the fallback reading it could never differ from the step it fell back
/// from. Taking it out turns "no level row" into a silent zero, and a store
/// that holds nothing is indistinguishable from a store nobody filled. Hence
/// a refusal, and hence this test: a check nothing has ever seen fire is not
/// a check. The last case must LOAD, or all this would prove is that the
/// factory can return nothing.
class LadderTables final : public core::ITableSet {
 public:
  LadderTables(std::vector<std::string> type_columns,
               std::vector<std::vector<std::string>> types,
               std::vector<std::vector<std::string>> levels)
      : types_{std::move(type_columns), std::move(types)},
        levels_{{"unit", "level", "era", "labor_days", "build_class", "storage_capacity_t"},
                std::move(levels)} {}

  const core::ITable* FindTable(std::string_view name) const override {
    if (name == "unit_types") {
      return &types_;
    }
    if (name == "unit_levels") {
      return &levels_;
    }
    if (name == "resources") {
      return &resources_;
    }
    return nullptr;
  }

  std::uint32_t TableCount() const override { return 3; }

  std::string_view TableName(std::uint32_t /*index*/) const override { return {}; }

 private:
  test::FakeTable types_;
  test::FakeTable levels_;
  test::FakeTable resources_{{"key", "measure", "kg_per_unit"}, {{"log", "unit", "50"}}};
};

int TestCapacityNeedsALadder() {
  int failures = 0;
  const std::vector<std::string> with_column = {
      "key", "era", "player_built", "gate", "storage_capacity_t"};
  const auto loads = [](std::vector<std::string> type_columns,
                        std::vector<std::vector<std::string>> types,
                        std::vector<std::vector<std::string>> levels) {
    const LadderTables tables(std::move(type_columns), std::move(types), std::move(levels));
    return core::CreateConstructionSystem(tables, core::StubTables::kAllowed) != nullptr;
  };
  failures += Expect(!loads(with_column,
                            {{"barn", "1", "1", "era", "9"}},
                            {{"barn", "1", "1", "70", "wood_small", ""}}),
                     "a capacity whose ladder names none refuses the load");
  failures += Expect(!loads(with_column,
                            {{"barn", "1", "1", "era", "9"}},
                            {{"barn", "1", "1", "70", "wood_small", "1"},
                             {"barn", "2", "1", "140", "wood_small", ""}}),
                     "a blank step among named ones refuses the load");
  failures += Expect(loads(with_column,
                           {{"barn", "1", "1", "era", "9"}},
                           {{"barn", "1", "1", "70", "wood_small", "1"},
                            {"barn", "2", "1", "140", "wood_small", "5"}}),
                     "a ladder that answers for every step still loads");
  // The column is on its way out of the export, and the loader must not be
  // what breaks when it goes: the ladder alone is a complete answer.
  failures += Expect(loads({"key", "era", "player_built", "gate"},
                           {{"barn", "1", "1", "era"}},
                           {{"barn", "1", "1", "70", "wood_small", "1"},
                            {"barn", "2", "1", "140", "wood_small", "5"}}),
                     "the type column is not required at all");
  return failures;
}

}  // namespace

/// A table set with nothing in it builds only for a caller that SAYS it wants
/// the documented defaults (core_tables/stub_tables.h).
///
/// One assertion per factory and not one on the assembled simulation, which
/// is what this check first was: the assembly refuses if ANY of the five
/// refuses, so damaging one guard is masked by the other four. A guard has
/// to name its own subject.
int CheckStubTablesMustBeDeclared() {
  int failures = 0;
  const test::FakeTableSet nothing;
  failures += Expect(core::CreateConstructionSystem(nothing, core::StubTables::kRefused) == nullptr,
                     "construction: a caller that did not allow the defaults is refused");
  failures += Expect(core::CreateConstructionSystem(nothing, core::StubTables::kAllowed) != nullptr,
                     "construction: and one that did gets them");
  return failures;
}

/// THE STINK FIELD: the full zone, the band, and the half that has nothing
/// to read.
int TestTheStinkField() {
  int failures = 0;
  // A source table of its own, so that every assertion below has a subject
  // only it rejects. Four types: a strong always-source, a medium one, a
  // source that smells only while it works, and one that does not smell.
  const test::FakeTable types{
      {"key", "era", "player_built", "gate", "has_wear", "stink", "stink_when"},
      {{"heap", "1", "1", "era", "0", "strong", "always"},
       {"byre", "1", "1", "era", "1", "medium", "always"},
       {"tannery", "1", "1", "era", "1", "strong", "working"},
       {"house", "1", "1", "era", "1", "none", ""}}};
  // The ladder needs its two columns even with no rows in it: "no unit or
  // level column" is a refusal, and rightly — a ladder nobody can index is
  // not an empty ladder.
  const test::FakeTable no_levels{{"unit", "level"}, {}};
  const test::FakeTable no_costs{{"unit", "level", "resource", "amount"}, {}};
  const test::FakeTable no_resources{{"key", "measure", "kg_per_unit"}, {}};
  const test::FakeTable knobs{{"key", "value"}, {{"demolition_labor_share", "0.5"}}};
  const test::FakeTableSet tables{{{"unit_types", &types},
                                   {"unit_levels", &no_levels},
                                   {"unit_level_cost", &no_costs},
                                   {"resources", &no_resources},
                                   {"construction", &knobs}}};
  const auto system = core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the stink fixture builds a construction system") != 0) {
    return 1;
  }

  core::WorldState world;
  const auto place = [&world](std::uint16_t type, float x, float y, std::uint8_t level) {
    core::UnitRow unit;
    unit.type = core::UnitTypeId{type};
    unit.position = core::Vec2{.x = x, .y = y};
    unit.level = level;
    core::AppendRow(world.units, unit);
  };
  place(0, 0.0F, 0.0F, 1);     // heap, strong, radius 200
  place(1, 1000.0F, 0.0F, 1);  // byre, medium, radius 120
  place(2, 2000.0F, 0.0F, 1);  // tannery, strong but only while working
  place(0, 3000.0F, 0.0F, 0);  // a heap that is still a SITE

  const auto at = [&system, &world](float x, float y) {
    return system->StinkFullAt(world, core::Vec2{.x = x, .y = y});
  };
  failures += Expect(at(199.0F, 0.0F) == core::StinkStrength::kStrong,
                     "inside a strong source's full radius the air is strong");
  failures += Expect(at(201.0F, 0.0F) == core::StinkStrength::kNone,
                     "and two metres outside it is clean — the zone has an edge");
  failures += Expect(at(1119.0F, 0.0F) == core::StinkStrength::kMedium,
                     "a medium source gives a medium band, not a strong one");
  failures += Expect(at(1121.0F, 0.0F) == core::StinkStrength::kNone,
                     "and its edge is nearer than a strong source's");
  // The tannery sits at its own centre and answers nothing: the STUB, and
  // the guard exists so that the day unit work cycles arrive, whoever wires
  // them is told by a red test that this is where the wire goes.
  failures += Expect(at(2000.0F, 0.0F) == core::StinkStrength::kNone,
                     "a source that smells only WHILE IT WORKS reads as silent: this core has no "
                     "work at a unit to read (STUB)");
  failures += Expect(at(3000.0F, 0.0F) == core::StinkStrength::kNone,
                     "and a source that is still a building site does not smell either");

  // WHERE TWO ZONES OVERLAP THE WORSE ONE WINS. Measured on a point that is
  // inside both, or the assertion would pass on a rule that never met a
  // second source.
  place(1, 150.0F, 0.0F, 1);
  failures += Expect(at(150.0F, 0.0F) == core::StinkStrength::kStrong,
                     "a point inside both a medium and a strong zone reads strong");
  failures += Expect(at(250.0F, 0.0F) == core::StinkStrength::kMedium,
                     "and just outside the strong one it drops to the medium band, not to clean");

  // AND THE TABLE IS A CONTRACT OF TWO COLUMNS. Each refusal gets its own
  // damaged row, so a fixture that several rules reject cannot hide which
  // one spoke.
  const auto refuses = [&no_levels, &no_costs, &no_resources, &knobs](
                           std::vector<std::vector<std::string>> rows) {
    const test::FakeTable broken{
        {"key", "era", "player_built", "gate", "has_wear", "stink", "stink_when"}, std::move(rows)};
    const test::FakeTableSet set{{{"unit_types", &broken},
                                  {"unit_levels", &no_levels},
                                  {"unit_level_cost", &no_costs},
                                  {"resources", &no_resources},
                                  {"construction", &knobs}}};
    return core::CreateConstructionSystem(set, core::StubTables::kAllowed) == nullptr;
  };
  failures += Expect(refuses({{"heap", "1", "1", "era", "0", "stong", "always"}}),
                     "a misspelt strength is refused, not read as odourless");
  failures += Expect(refuses({{"heap", "1", "1", "era", "0", "strong", ""}}),
                     "a source that does not say whether its contents or its work smells is "
                     "refused");
  failures += Expect(refuses({{"heap", "1", "1", "era", "0", "strong", "sometimes"}}),
                     "and a word that is neither always nor working is refused");
  failures += Expect(refuses({{"house", "1", "1", "era", "1", "none", "always"}}),
                     "a type that does not smell may not say when it smells");
  // AN EMPTY CELL IN A PRESENT COLUMN IS A HOLE, not an answer — the same
  // line has_wear draws twelve lines above it in the parser. A blank
  // disappears a strong source exactly as quietly as a typo does.
  //
  // BOTH CELLS BLANK, and the second blank is the whole subject. The first
  // version of this row left stink_when = "always", and it passed the
  // mutation that removes the rule it is written for — because a blank
  // strength beside a named `when` is refused by the NEIGHBOURING rule
  // ("a type that does not smell may not say when it smells"). Second time
  // in one day that a guard drew its red from the rule next door; the fix
  // is the same one — give it a subject only its own rule rejects.
  failures += Expect(refuses({{"heap", "1", "1", "era", "0", "", ""}}),
                     "a blank strength in a present column is refused, not read as odourless");
  return failures;
}

/// THE ZONE OF A DAY: it grows, it goes out, and ONE speed tells both
/// stories.
///
/// The design tells two — "a forge that worked a morning does not smoke out
/// the street" and "a tannery that ran a season stinks a week into its
/// idleness" — and asks for one rule. A rule that needed two numbers would
/// pass a check written for either story alone, so this guard measures the
/// thing that makes them one: A BIG ZONE TAKES LONGER TO GO OUT THAN A SMALL
/// ONE, without anybody saying so.
int TestTheStinkZoneGrowsAndGoesOut() {
  int failures = 0;
  const test::FakeTable types{
      {"key", "era", "player_built", "gate", "has_wear", "stink", "stink_when"},
      {{"heap", "1", "1", "era", "0", "strong", "always"},
       {"fuel", "1", "1", "era", "0", "weak", "always"}}};
  const test::FakeTable no_levels{{"unit", "level"}, {}};
  const test::FakeTable no_costs{{"unit", "level", "resource", "amount"}, {}};
  const test::FakeTable no_resources{{"key", "measure", "kg_per_unit"}, {}};
  const test::FakeTable knobs{{"key", "value"}, {{"demolition_labor_share", "0.5"}}};
  const test::FakeTableSet tables{{{"unit_types", &types},
                                   {"unit_levels", &no_levels},
                                   {"unit_level_cost", &no_costs},
                                   {"resources", &no_resources},
                                   {"construction", &knobs}}};
  const auto system = core::CreateConstructionSystem(tables, core::StubTables::kAllowed);
  if (Expect(system != nullptr, "the stink-zone fixture builds a construction system") != 0) {
    return 1;
  }

  core::WorldState world;
  core::UnitRow heap;
  heap.type = core::UnitTypeId{0};
  heap.position = core::Vec2{.x = 0.0F, .y = 0.0F};
  core::AppendRow(world.units, heap);
  core::UnitRow fuel;
  fuel.type = core::UnitTypeId{1};
  fuel.position = core::Vec2{.x = 5000.0F, .y = 0.0F};
  core::AppendRow(world.units, fuel);

  const auto run_a_day = [&system, &world]() {
    world.calendar.tick += core::kTicksPerDay;
    core::RefreshCalendarCaches(world.calendar);
    const core::WorldState before = world;
    system->RunConstructionDecisions(before, world);
  };

  // A RAISED SOURCE STARTS AT NOTHING. Named first, because every claim
  // below is about a change from it.
  failures += Expect(world.units.rows[0].stink_radius_m == 0.0F,
                     "a source that has never run reaches nowhere");
  failures += Expect(
      system->StinkNowAt(world, core::Vec2{.x = 1.0F, .y = 0.0F}) == core::StinkStrength::kNone,
      "and today it does not smell even at its own feet");
  // ...while the FULL zone already answers, because that is what a plan is
  // judged against and a plan is about what the thing will become.
  failures += Expect(
      system->StinkFullAt(world, core::Vec2{.x = 1.0F, .y = 0.0F}) == core::StinkStrength::kStrong,
      "but its full zone answers from the first day: a plan is judged on what the "
      "source will be, not on what it is this morning");

  run_a_day();
  const float after_one_day = world.units.rows[0].stink_radius_m;
  failures += Expect(after_one_day > 0.0F && after_one_day < 200.0F,
                     "after one day the zone has started and has not arrived");
  // Grow it to the full radius, then count the days it takes to go out.
  for (int day = 0; day < 20; ++day) {
    run_a_day();
  }
  failures += Expect(world.units.rows[0].stink_radius_m == 200.0F,
                     "left running, the strong zone reaches its full radius and stops there");
  failures += Expect(world.units.rows[1].stink_radius_m == 40.0F,
                     "and the weak one stops at its own, which is nearer");
  failures += Expect(
      system->StinkNowAt(world, core::Vec2{.x = 199.0F, .y = 0.0F}) == core::StinkStrength::kStrong,
      "now today's zone answers where the full one does");

  // EVERY RUNG MAKES THE SOURCE CLEANER, and the core never goes away.
  //
  // Measured on ONE unit moved up the ladder rather than on two units of
  // different levels: two units would differ in their positions and their
  // history as well as in their level, and the assertion would be about the
  // pair rather than about the step.
  {
    const float at_level_one = world.units.rows[0].stink_radius_m;
    world.units.rows[0].level = 2;
    for (int day = 0; day < 20; ++day) {
      run_a_day();
    }
    const float at_level_two = world.units.rows[0].stink_radius_m;
    failures += Expect(at_level_two < at_level_one,
                       "a rung of the ladder narrows the zone: the upgrade is worth building for "
                       "the air as well as for the work");
    world.units.rows[0].level = 40;  // a ladder no unit will ever have
    for (int day = 0; day < 40; ++day) {
      run_a_day();
    }
    // THE FLOOR, AND NOT MERELY "MORE THAN NOTHING". The first version of
    // this line asked for a radius above zero, and forty rungs of 0.75
    // leave two millimetres — which is above zero and is not a core. The
    // guard passed with the floor deleted. The subject of a floor is the
    // floor's own value.
    constexpr float kCoreOfAStrongZone = 200.0F * 0.35F;
    failures += Expect(world.units.rows[0].stink_radius_m >= kCoreOfAStrongZone - 0.01F,
                       "and no ladder however long takes a strong source below its core: right up "
                       "against the byre it smells whatever you do");
    failures += Expect(world.units.rows[0].stink_radius_m < at_level_two,
                       "while it does keep narrowing on the way to that floor");
    // The FULL zone is unmoved by any of this: a plan is judged on the
    // widest the source ever is, which is its first rung.
    failures += Expect(system->StinkFullAt(world, core::Vec2{.x = 199.0F, .y = 0.0F}) ==
                           core::StinkStrength::kStrong,
                       "but the placement preview still shows the widest the source will ever be, "
                       "so a spot chosen today does not turn out to stink tomorrow");
    world.units.rows[0].level = 1;
    for (int day = 0; day < 20; ++day) {
      run_a_day();
    }
  }

  // BOTH SOURCES STOP ON THE SAME DAY, and the whole point is that they do
  // not go out together.
  world.units.rows[0].level = 0;
  world.units.rows[1].level = 0;
  int days_for_the_weak = 0;
  int days_for_the_strong = 0;
  for (int day = 0; day < 40; ++day) {
    run_a_day();
    if (world.units.rows[1].stink_radius_m > 0.0F) {
      ++days_for_the_weak;
    }
    if (world.units.rows[0].stink_radius_m > 0.0F) {
      ++days_for_the_strong;
    }
  }
  failures += Expect(
      world.units.rows[0].stink_radius_m == 0.0F && world.units.rows[1].stink_radius_m == 0.0F,
      "a source that has stopped goes out");
  failures += Expect(days_for_the_strong > days_for_the_weak,
                     "and the BIG zone takes longer than the small one — one speed, both of the "
                     "design's stories, and no second rule");
  // The number, not just the ordering: a week for the full strong zone is
  // the design's own worked example, and it is what the decay rate was set
  // from. If the rate moves, this says so.
  failures += Expect(days_for_the_strong >= 6 && days_for_the_strong <= 9,
                     "a full strong zone takes about a week to go out, as the design says of the "
                     "tannery");
  return failures;
}

/// THE ASSUMED RADII ARE MEASURED AGAINST THE SHIPPED SCENE, not against
/// themselves.
///
/// The design defers the radii to polish item P30be and names no number, so
/// this core picked three. A knob picked in isolation is a knob nobody has
/// checked, and this one has a checkable consequence: a dwelling may not
/// stand in a stink zone (water design §4), and the designed start places
/// twenty-one houses and three sources by hand. If the strong radius grows
/// past the 268 m between the manure heap and the nearest house, THE
/// DESIGNED START BREAKS ITS OWN RULE ON DAY ONE — and it would do it in
/// silence, because nothing else compares the two numbers.
int TestTheShippedStartHasNoHouseInAStinkZone() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  if (Expect(tables != nullptr, "the shipped tables load") != 0) {
    return 1;
  }
  const auto system = core::CreateConstructionSystem(*tables, core::StubTables::kRefused);
  if (Expect(system != nullptr, "and the shipped tables build a construction system") != 0) {
    return 1;
  }
  const core::WorldState world = core::CreateStartWorld(*tables, 12345, nullptr);

  const core::ITable* const unit_types = tables->FindTable("unit_types");
  const std::uint32_t house_row = unit_types->FindRowByKey("old_house");
  if (Expect(house_row != core::kNoTableRow, "the shipped tables name the start's house type") !=
      0) {
    return 1;
  }
  std::uint32_t houses = 0;
  std::uint32_t houses_in_a_zone = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    if (unit.type.value != house_row || unit.level == 0) {
      continue;
    }
    ++houses;
    houses_in_a_zone +=
        system->StinkFullAt(world, unit.position) != core::StinkStrength::kNone ? 1U : 0U;
  }
  // The control first: without it the check below passes on a start with no
  // houses in it, and on a field that answers kNone to everything.
  failures += Expect(houses == 21, "the designed start stands its twenty-one houses");
  bool any_zone_at_all = false;
  for (const core::UnitRow& unit : world.units.rows) {
    any_zone_at_all =
        any_zone_at_all || system->StinkFullAt(world, unit.position) != core::StinkStrength::kNone;
  }
  failures += Expect(any_zone_at_all,
                     "and the start's sources do make zones — otherwise the check below is "
                     "vacuous");
  failures += Expect(houses_in_a_zone == 0,
                     "no house of the designed start stands in a stink zone: the assumed radii do "
                     "not make the start illegal by its own rule");
  return failures;
}

int main() {
  int failures = 0;
  failures += CheckStubTablesMustBeDeclared();
  const BuildTables tables;
  failures += TestStepPaceMultipliesTypePace(tables);
  failures += TestABodyKeepsItsMetre(tables);
  failures += TestCapacityNeedsALadder();
  failures += TestMarkAndBuild(tables);
  failures += TestRefusals(tables);
  failures += TestDemolition(tables);
  failures += TestTableLessWorld();
  failures += TestWearGrows(tables);
  failures += TestWearDeadline(tables);
  failures += TestWearCeilingAndCollapse(tables);
  failures += TestRepair(tables);
  failures += TestUpgradeHeals(tables);
  failures += TestTheStinkField();
  failures += TestTheStinkZoneGrowsAndGoesOut();
  failures += TestTheShippedStartHasNoHouseInAStinkZone();
  if (failures == 0) {
    std::cout << "unit_core_construction: marking, building, upgrading, refusals and demolition\n";
  }
  return failures;
}
