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

#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_construction/construction_system.h"
#include "core_tables/tables.h"

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

class FakeTable final : public core::ITable {
 public:
  FakeTable(std::vector<std::string_view> columns, std::vector<std::vector<std::string_view>> rows)
      : columns_(std::move(columns)), rows_(std::move(rows)) {}

  std::uint32_t RowCount() const override { return static_cast<std::uint32_t>(rows_.size()); }

  std::uint32_t ColumnCount() const override { return static_cast<std::uint32_t>(columns_.size()); }

  std::uint32_t FindColumn(std::string_view name) const override {
    for (std::uint32_t index = 0; index < columns_.size(); ++index) {
      if (columns_[index] == name) {
        return index;
      }
    }
    return core::kNoTableColumn;
  }

  std::uint32_t FindRowByKey(std::string_view key) const override {
    for (std::uint32_t row = 0; row < rows_.size(); ++row) {
      if (!rows_[row].empty() && rows_[row][0] == key) {
        return row;
      }
    }
    return core::kNoTableRow;
  }

  std::string_view CellText(std::uint32_t row, std::uint32_t column) const override {
    if (row >= rows_.size() || column >= rows_[row].size()) {
      return {};
    }
    return rows_[row][column];
  }

  std::optional<std::int64_t> CellInteger(std::uint32_t row, std::uint32_t column) const override {
    const std::optional<float> value = CellReal(row, column);
    return value ? std::optional<std::int64_t>(static_cast<std::int64_t>(*value)) : std::nullopt;
  }

  std::optional<float> CellReal(std::uint32_t row, std::uint32_t column) const override {
    const std::string_view text = CellText(row, column);
    if (text.empty()) {
      return std::nullopt;
    }
    return static_cast<float>(std::stod(std::string(text)));
  }

 private:
  std::vector<std::string_view> columns_;

  std::vector<std::vector<std::string_view>> rows_;
};

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
  FakeTable types_{{"key",
                    "era",
                    "player_built",
                    "gate",
                    "has_plot",
                    "plot_radius_m",
                    "has_wear",
                    "wear_factor"},
                   {{"store", "1", "0", "start", "1", "10", "1", ""},
                    {"barn", "1", "1", "era", "1", "20", "1", "1.5"},
                    {"club", "2", "1", "era", "1", "20", "1", ""},
                    {"orchard", "1", "1", "era", "1", "", "0", ""},
                    {"old_house", "1", "0", "start", "1", "10", "1", ""}}};

  // 70 real man-days is 10 game man-days (root rules §9: real / 7).
  // Ten years standing, five in use: IN USE IS THE SHORTER TERM (unit rules
  // §15, and boss corrected his own criterion on it). Round numbers so the
  // daily share is exact arithmetic in the test below.
  FakeTable levels_{{"unit",
                     "level",
                     "era",
                     "labor_days",
                     "build_class",
                     "max_crew",
                     "wear_years_idle",
                     "wear_years_in_use"},
                    {{"store", "1", "1", "70", "wood_small", "5", "10", "5"},
                     {"barn", "1", "1", "70", "wood_small", "5", "10", "5"},
                     {"barn", "2", "1", "140", "wood_small_ext", "8", "20", "10"},
                     {"club", "1", "2", "70", "wood_small", "5", "10", "5"},
                     {"orchard", "1", "1", "0", "plot", "", "", ""},
                     {"old_house", "1", "1", "70", "wood_small", "5", "10", "5"}}};

  FakeTable costs_{{"unit", "level", "resource", "amount"},
                   {{"barn", "1", "log", "10"}, {"barn", "2", "log", "20"}}};

  FakeTable resources_{{"key", "measure", "kg_per_unit"},
                       {{"log", "pcs", "200"}, {"spare_part", "pcs", "5"}}};

  FakeTable knobs_{{"key", "value"},
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
  std::unique_ptr<core::IConstructionSystem> system = core::CreateConstructionSystem(tables);
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
  std::unique_ptr<core::IConstructionSystem> system = core::CreateConstructionSystem(tables);
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
  std::unique_ptr<core::IConstructionSystem> system = core::CreateConstructionSystem(tables);
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
/// honest answer rather than a refusal to exist.
class EmptyTableSet final : public core::ITableSet {
 public:
  const core::ITable* FindTable(std::string_view /*name*/) const override { return nullptr; }

  std::uint32_t TableCount() const override { return 0; }

  std::string_view TableName(std::uint32_t /*index*/) const override { return {}; }
};

/// Task A5: the building ages, and it ages at two speeds. Ten years empty,
/// five in use, and the barn's own pace is one and a half — so a day of
/// standing empty is 100 / (10 x 1.5 x 48) and a day in use is twice that.
int TestWearGrows(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system = core::CreateConstructionSystem(tables);
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

/// The scale stops at 100 and the unit goes on working — except the start's
/// old houses, which are the one kind that falls (start design §4).
int TestWearCeilingAndCollapse(const core::ITableSet& tables) {
  int failures = 0;
  std::unique_ptr<core::IConstructionSystem> system = core::CreateConstructionSystem(tables);
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
  std::unique_ptr<core::IConstructionSystem> system = core::CreateConstructionSystem(tables);
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
  std::unique_ptr<core::IConstructionSystem> system = core::CreateConstructionSystem(tables);
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
  const EmptyTableSet tables;
  std::unique_ptr<core::IConstructionSystem> system = core::CreateConstructionSystem(tables);
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

}  // namespace

int main() {
  int failures = 0;
  const BuildTables tables;
  failures += TestMarkAndBuild(tables);
  failures += TestRefusals(tables);
  failures += TestDemolition(tables);
  failures += TestTableLessWorld();
  failures += TestWearGrows(tables);
  failures += TestWearCeilingAndCollapse(tables);
  failures += TestRepair(tables);
  failures += TestUpgradeHeals(tables);
  if (failures == 0) {
    std::cout << "unit_core_construction: marking, building, upgrading, refusals and demolition\n";
  }
  return failures;
}
