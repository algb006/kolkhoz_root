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
  FakeTable types_{{"key", "era", "player_built", "gate", "has_plot", "plot_radius_m"},
                   {{"store", "1", "0", "start", "1", "10"},
                    {"barn", "1", "1", "era", "1", "20"},
                    {"club", "2", "1", "era", "1", "20"},
                    {"orchard", "1", "1", "era", "1", ""}}};

  // 70 real man-days is 10 game man-days (root rules §9: real / 7).
  FakeTable levels_{{"unit", "level", "era", "labor_days", "build_class", "max_crew"},
                    {{"store", "1", "1", "70", "wood_small", "5"},
                     {"barn", "1", "1", "70", "wood_small", "5"},
                     {"barn", "2", "1", "140", "wood_small_ext", "8"},
                     {"club", "1", "2", "70", "wood_small", "5"},
                     {"orchard", "1", "1", "0", "plot", ""}}};

  FakeTable costs_{{"unit", "level", "resource", "amount"},
                   {{"barn", "1", "log", "10"}, {"barn", "2", "log", "20"}}};

  FakeTable resources_{{"key", "measure", "kg_per_unit"}, {{"log", "pcs", "200"}}};

  FakeTable knobs_{{"key", "value"}, {{"demolition_labor_share", "0.5"}}};
};

constexpr std::uint16_t kStoreType = 0;
constexpr std::uint16_t kBarnType = 1;
constexpr std::uint16_t kClubType = 2;
constexpr std::uint16_t kOrchardType = 3;

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
  if (failures == 0) {
    std::cout << "unit_core_construction: marking, building, upgrading, refusals and demolition\n";
  }
  return failures;
}
