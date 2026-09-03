// World genesis (include/core_world/world.h, stage 3): the designed start —
// 80 residents in 21 yards (campaign table) with the age pyramid of the
// reference run (demography.py start distribution: 15.2% under 7, 22%
// school age, 49.2% working age, 13.6% old), deterministic from the seed.
//
// Household composition, derived from the start canon: old-timers keep
// their own yards (pairs, then a single); every other yard is a married
// couple; children and unmarried grown children live with their parents
// (families design §1, life-cycle §1). Fields the later stages own (houses,
// education mechanics) stay at their defaults.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "campaign_tables.h"
#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_log/log.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace core {
namespace {

/// Stream id of the world's sequential RNG (world.cpp uses the same).
constexpr std::uint64_t kWorldRngStream = 0;

/// Start pyramid shares, the reference run's distribution (demography.py).
constexpr float kShareUnderSeven = 0.152F;
constexpr float kShareSchool = 0.220F;
constexpr float kShareOld = 0.136F;

float DrawInRange(RngState& rng, float low, float high) {
  return low + NextRandomUnitFloat(rng) * (high - low);
}

/// @brief Birth day (negative: before day 0) for a biological age in years.
std::int32_t BirthDayForAge(float age_years, float life_speedup, RngState& rng) {
  const float game_days = age_years / life_speedup * static_cast<float>(kDaysPerYear);
  // A little jitter so a cohort does not share one birthday.
  return -static_cast<std::int32_t>(game_days + DrawInRange(rng, 0.0F, 6.0F));
}

/// @brief The shared shape of every starting person; kin and family are set
/// by the caller.
ResidentRow RollPerson(RngState& rng, Sex sex, float age_years, float life_speedup) {
  ResidentRow person;
  person.sex = sex;
  person.birth_day = BirthDayForAge(age_years, life_speedup, rng);
  person.intellect = DrawInRange(rng, 20.0F, 80.0F);
  person.stamina = DrawInRange(rng, 20.0F, 80.0F);
  person.optimism = DrawInRange(rng, 20.0F, 80.0F);
  person.ideology = DrawInRange(rng, 30.0F, 70.0F);
  person.health = DrawInRange(rng, 60.0F, 90.0F) - (age_years > 55.0F ? 15.0F : 0.0F);
  person.mood = DrawInRange(rng, 50.0F, 70.0F);
  // Most Epoch-I adults are illiterate; some finished primary school
  // (education design §2; the exact share is an ASSUMPTION until playtests).
  if (age_years >= 16.0F && NextRandomUnitFloat(rng) < 0.3F) {
    person.education_stage = EducationStage::kPrimary;
    person.education_grade = DrawInRange(rng, 3.0F, 5.0F);
  }
  return person;
}

FamilyRow RollFamily(RngState& rng) {
  FamilyRow family;
  family.component_satiety = DrawInRange(rng, 50.0F, 65.0F);
  family.component_common_cause = DrawInRange(rng, 45.0F, 60.0F);
  family.component_needs = DrawInRange(rng, 45.0F, 60.0F);
  family.component_rest = DrawInRange(rng, 50.0F, 65.0F);
  return family;
}

UnitTypeId TypeByKey(const ITable* unit_types, std::string_view key) {
  if (unit_types == nullptr) {
    return UnitTypeId{};
  }
  const std::uint32_t row = unit_types->FindRowByKey(key);
  return row == kNoTableRow ? UnitTypeId{} : UnitTypeId{static_cast<std::uint16_t>(row)};
}

ResourceId GenesisResource(const ITable* resources, std::string_view key) {
  if (resources == nullptr) {
    return ResourceId{};
  }
  const std::uint32_t row = resources->FindRowByKey(key);
  return row == kNoTableRow ? ResourceId{} : ResourceId{static_cast<std::uint16_t>(row)};
}

CropId CropByKey(const ITable* crops, std::string_view key) {
  if (crops == nullptr) {
    return CropId{};
  }
  const std::uint32_t row = crops->FindRowByKey(key);
  return row == kNoTableRow ? CropId{} : CropId{static_cast<std::uint16_t>(row)};
}

/// The same idea for a layout cell: coordinates, areas and flags. A map
/// twelve kilometres on a side leaves this six orders of magnitude of room.
constexpr float kLayoutNumberLimit = 1e9F;

/// The band the start's old houses begin their wear in (start design §4:
/// "a starting wear of 50 %", spread by boss's rule of 2026-09-03 so that
/// they do not all fall together). DEFAULTS ONLY: the numbers live in
/// tables/construction.csv beside the term those houses run on, and genesis
/// reads them below. They were constants here for one afternoon, and in that
/// afternoon the table said 45..60 while the code obeyed itself — a knob
/// nobody reads is worse than no knob at all.
constexpr float kOldHouseWearMinDefault = 45.0F;

constexpr float kOldHouseWearMaxDefault = 60.0F;

void PutStock(UnitRow& unit, ResourceId resource, float kilograms) {
  if (resource.value == kInvalidDefIdValue) {
    return;
  }
  // The conversion refuses nan, inf, negatives and overflow on its own
  // (quantities.h, the named cast pass) — but genesis SAYS SO as well,
  // because here a refused number is a start stock the campaign was
  // supposed to have and now has not. A silent zero at the founding is the
  // kind of thing found thirty years later.
  //
  // The two refusals are told apart, and NEITHER of them is "the amount
  // truncated to zero grams": half a gram of salt is a legitimate zero and
  // must still land in the vector. The first draft of this guard skipped
  // that case as well, and skipping it meant the row was never resized —
  // which the overfill trim below then wrote past (MEM-001). A guard that
  // refuses more than it was asked to is how a fix becomes a defect.
  if (!(kilograms >= 0.0F)) {  // positive test: nan fails it, and so do negatives
    LogWarning("genesis: a start stock amount is not a usable number; that row is skipped");
    return;
  }
  const Grams grams = GramsFromKilograms(kilograms);
  if (grams == 0 && kilograms >= 1.0F) {
    LogWarning("genesis: a start stock amount is too large to be a mass; that row is skipped");
    return;
  }
  if (unit.stock.size() <= resource.value) {
    unit.stock.resize(resource.value + 1U, 0);
  }
  unit.stock[resource.value] = grams;
}

/// @brief Puts kilograms of a resource into a family's own larder.
void PutPantry(FamilyRow& family, ResourceId resource, float kilograms) {
  if (resource.value == kInvalidDefIdValue) {
    return;
  }
  // Guarded like PutStock since the named cast pass: the two differed only
  // because PutPantry's four callers happened to pass literals, and "safe
  // because of who calls it today" is not a property of a function.
  if (family.pantry.size() <= resource.value) {
    family.pantry.resize(resource.value + 1U, 0);
  }
  family.pantry[resource.value] = GramsFromKilograms(kilograms);
}

UnitId PlaceUnit(WorldState& world, UnitTypeId type, float x_meters, float y_meters) {
  UnitRow unit;
  unit.type = type;
  unit.position = Vec2{.x = x_meters, .y = y_meters};
  return AppendRow(world.units, unit);
}

/// @brief One ploughed field with its three-year rotation (farming design
/// §7). An invalid slot is a fallow year.
FieldId PlaceField(WorldState& world,
                   float area_ga,
                   Vec2 center,
                   Metric fertility,
                   CropId year0,
                   CropId year1,
                   CropId year2) {
  FieldRow field;
  field.center = center;
  field.area_ga = area_ga;
  field.fertility = fertility;
  field.rotation_year0 = year0;
  field.rotation_year1 = year1;
  field.rotation_year2 = year2;
  return AppendRow(world.fields, field);
}

/// @brief One meadow: standing grass from day one, mown once a season.
/// No crop, no rotation, no fertility — a meadow is land, not a sowing
/// (land_state.h, LandKind; boss answer Q6, 2026-08-31).
void PlaceMeadow(WorldState& world, float area_ga, Vec2 center, bool floodplain) {
  FieldRow meadow;
  // Upland or floodplain: the best grass of the farm is the wet meadow by
  // the river, and it yields two and a half tonnes a hectare against one
  // and a half for the same cut and the same days (terrain design §8;
  // meadow_kinds.csv). Which contour is which comes from the layout — the
  // scene knows where the floodplain is, and the core never guesses it.
  meadow.kind = floodplain ? LandKind::kFloodplainMeadow : LandKind::kMeadow;
  meadow.center = center;
  meadow.area_ga = area_ga;
  meadow.phase = FieldPhase::kGrowing;
  AppendRow(world.fields, meadow);
}

/// @brief Reads one livestock knob; an absent, unreadable or absurd cell is
/// the fallback. The range test is the one its neighbours already have
/// (LayoutNumber, PutStock): CellReal now refuses inf and nan at the door,
/// but a finite 1e30 still has to be stopped before it reaches the ages and
/// the casts they feed. Written positively, so anything unexpected fails it.
float LivestockValue(const ITable& livestock,
                     std::uint32_t row,
                     std::string_view column,
                     float fallback) {
  const std::uint32_t index = livestock.FindColumn(column);
  if (index == kNoTableColumn) {
    return fallback;
  }
  const std::optional<float> value = livestock.CellReal(row, index);
  if (!value) {
    return fallback;
  }
  if (!(*value >= -kLayoutNumberLimit && *value <= kLayoutNumberLimit)) {
    LogWarning("genesis: a livestock cell is not a usable number; the default is used");
    return fallback;
  }
  return *value;
}

/// @brief Appends one herd; returns nothing, because genesis never needs the
/// id back.
///
/// THE AGES ARE DRAWN, HEAD BY HEAD, and that is canon rather than colour:
/// "the age of every head is rolled at the founding — the herd and the team
/// are not the same age, or they would die out all at once" (start canon §11,
/// livestock design §6). The core kept them all at the age of adulthood, and
/// the thirty-year run showed exactly the disaster the canon named: thirty-
/// nine cows crossed their age threshold together and twenty-two of them died
/// in one year, then twenty-one the next (69-reconciliation.md §3 D2).
///
/// Each adult is drawn uniformly across the whole adult band — from the age
/// it becomes an adult to the top of its lifespan — and the row keeps the
/// SUM, which is what the age model reads.
void AddHerd(WorldState& world,
             const ITable& livestock,
             RngState& rng,
             std::string_view key,
             std::uint16_t adults,
             std::uint16_t males,
             UnitId unit,
             FamilyId household,
             bool household_owned) {
  const std::uint32_t row = livestock.FindRowByKey(key);
  if (row == kNoTableRow) {
    return;  // a table set without this kind simply has none of it
  }
  HerdRow herd;
  herd.kind = LivestockKindId{static_cast<std::uint16_t>(row)};
  herd.unit = unit;
  herd.household = household;
  herd.household_owned = household_owned ? 1U : 0U;
  herd.adult_count = adults;
  herd.adult_male_count = males;
  const float adult_from_years = LivestockValue(livestock, row, "adult_from_game_months", 0.0F) /
                                 static_cast<float>(kMonthsPerYear);
  const float oldest = LivestockValue(livestock, row, "life_game_years_max", adult_from_years);
  const float youngest = adult_from_years < oldest ? adult_from_years : oldest;
  for (std::uint16_t head = 0; head < adults; ++head) {
    herd.adult_age_game_years_total += DrawInRange(rng, youngest, oldest);
  }
  AppendRow(world.herds, herd);
}

/// The start's animals (start canon §10-§11 and the household canon of
/// livestock design §2, boss answer 2026-08-30 to registry question 148).
///
/// Two lines that look alike and are not: the cows stand at the stock yard
/// and the horses stand in private yards, and BOTH are the kolkhoz's. The
/// horses are billeted, not given away — the farm feeds them and the farm
/// works them. What the families own is what the canon gives them: two goats
/// and eight hens each, a pig at every twentieth yard, and no cow at all
/// ("the cows are handed over, every last one"). The canonical ~1.2 t of
/// milk a yard is TWO GOATS, which is what made the coverage figure add up
/// all along.
void PlaceHerds(WorldState& world, const ITableSet& tables, UnitId stock_yard) {
  const ITable* livestock = tables.FindTable("livestock");
  if (livestock == nullptr) {
    return;
  }
  RngState& rng = world.rng;
  // Two bulls to 37 cows is the 4% the design keeps; without a sire the barn
  // could never grow, and growing it is the whole first-epoch arc.
  AddHerd(world, *livestock, rng, "cow", 39, 2, stock_yard, FamilyId{}, false);
  const auto yards = static_cast<std::uint32_t>(world.families.rows.size());
  const std::uint32_t horse_yards = yards < 16 ? yards : 16U;
  for (std::uint32_t yard = 0; yard < horse_yards; ++yard) {
    AddHerd(world, *livestock, rng, "horse", 1, 0, UnitId{}, world.families.row_ids[yard], false);
  }
  for (std::uint32_t yard = 0; yard < yards; ++yard) {
    const FamilyId home = world.families.row_ids[yard];
    AddHerd(world, *livestock, rng, "goat", 2, 0, UnitId{}, home, true);
    AddHerd(world, *livestock, rng, "chicken", 8, 0, UnitId{}, home, true);
  }
  // A pig at every twentieth yard: "only the well-off, and no more than a
  // fifth of the yards" — a sow and a boar, or it is not a herd.
  for (std::uint32_t yard = 0; yard + 1U < yards; yard += 20U) {
    AddHerd(world, *livestock, rng, "pig", 2, 1, UnitId{}, world.families.row_ids[yard], true);
  }
}

// ---------------------------------------------------------------------------
// The start layout, read rather than invented (task A2)
// ---------------------------------------------------------------------------
//
// The central zone is a hand-designed scene: every house, heap and field
// outline was placed by hand in the map editor and exported through
// db/design.db into tables/start_layout.csv and tables/start_stock.csv
// (start canon §2). Genesis reads them the way it reads crops and unit
// types. What used to stand here was a grid of invented coordinates,
// marked STUB; the two tables replace it entirely, and with them the
// scene and the simulation share ONE set of places.
//
// Genesis places EVERY row of the layout, including the ones the core has
// no rule for — the water mill, the manor ruins. A row that is in the
// scene and not in the state would leave the graphics layer holding a key
// with no id behind it, and one shared vocabulary of keys is worth more
// than the handful of idle rows it costs.

/// One row of start_layout.csv, in the columns this function reads.
struct LayoutRow {
  std::string_view key;
  std::string_view kind;
  std::string_view unit_type;
  float x_meters = 0.0F;
  float y_meters = 0.0F;
  float area_ga = 0.0F;
  std::array<std::string_view, 3> rotation;
  bool derelict = false;
};

/// @brief Reads one cell as a number; an empty, unreadable, non-finite or
/// absurd cell is 0 and says so. std::from_chars accepts "inf" and "nan",
/// and these tables are exported and then hand-edited, so the reader refuses
/// them here instead of passing them on to a cast (UB-002). Written
/// positively for the same reason as everywhere else: nan fails the test.
float LayoutNumber(const ITable& table, std::uint32_t row, std::uint32_t column) {
  if (column == kNoTableColumn) {
    return 0.0F;
  }
  const std::optional<float> cell = table.CellReal(row, column);
  if (!cell) {
    return 0.0F;
  }
  if (!(*cell >= -kLayoutNumberLimit && *cell <= kLayoutNumberLimit)) {
    LogWarning("genesis: a layout cell is not a usable number; it is read as zero");
    return 0.0F;
  }
  return *cell;
}

/// @brief Fills the units, fields and meadows of the start from the layout
/// table, and remembers which unit each row's key became so that the stock
/// table can find it.
/// @return false when the table is absent or has no usable columns — the
///         world then stays people-only, exactly as it does without the
///         other tables.
bool PlaceStartLayout(WorldState& world,
                      const ITable& layout,
                      const ITable* unit_types,
                      const ITable* crops,
                      Metric start_fertility,
                      float map_side_m,
                      std::vector<std::pair<std::string_view, UnitId>>& placed) {
  const std::uint32_t key_col = layout.FindColumn("key");
  const std::uint32_t kind_col = layout.FindColumn("kind");
  const std::uint32_t type_col = layout.FindColumn("unit_type");
  const std::uint32_t x_col = layout.FindColumn("x_m");
  const std::uint32_t y_col = layout.FindColumn("y_m");
  const std::uint32_t area_col = layout.FindColumn("area_ha");
  const std::uint32_t derelict_col = layout.FindColumn("is_derelict");
  const std::uint32_t meadow_kind_col = layout.FindColumn("meadow_kind");
  const std::array<std::uint32_t, 3> rotation_cols = {layout.FindColumn("rotation_year0"),
                                                      layout.FindColumn("rotation_year1"),
                                                      layout.FindColumn("rotation_year2")};
  if (key_col == kNoTableColumn || kind_col == kNoTableColumn) {
    return false;
  }

  for (std::uint32_t row = 0; row < layout.RowCount(); ++row) {
    const std::string_view kind = layout.CellText(row, kind_col);
    const Vec2 place{.x = LayoutNumber(layout, row, x_col), .y = LayoutNumber(layout, row, y_col)};
    // The map bounds ARE checked somewhere — on the positions the chairman
    // orders a building at (core_construction) — and were never checked on
    // the scene the core ships with. So when the layout moved to the twelve
    // kilometre map and the core's own copy of the side stayed at ten,
    // nothing said a word, and the graphics layer found it by building a
    // landscape too small for the farm. A check that looks only where the
    // danger is expected is how that happens. The row is still placed: the
    // scene is the scene, and dropping a cemetery because a number
    // disagrees would be worse. A side of zero means the table set declares
    // no map, and then there is no edge to be outside of.
    if (map_side_m > 0.0F &&
        !(place.x >= 0.0F && place.x <= map_side_m && place.y >= 0.0F && place.y <= map_side_m)) {
      LogWarning("genesis: layout row '" + std::string(layout.CellText(row, key_col)) +
                 "' lies outside the map declared by tables/map.csv");
    }
    if (kind == "unit") {
      const UnitTypeId type = TypeByKey(unit_types, layout.CellText(row, type_col));
      if (type.value == kInvalidDefIdValue) {
        continue;  // a type the core's tables do not carry: nothing to place
      }
      placed.emplace_back(layout.CellText(row, key_col), PlaceUnit(world, type, place.x, place.y));
      continue;
    }
    const float area = LayoutNumber(layout, row, area_col);
    if (kind == "meadow") {
      PlaceMeadow(world, area, place, layout.CellText(row, meadow_kind_col) == "floodplain");
      continue;
    }
    // Arable, and the reserve field held back for building on: both are
    // field rows, and the reserve is derelict like the rest of the ninety
    // hectares nobody has raised (start canon §2).
    const bool derelict = LayoutNumber(layout, row, derelict_col) > 0.5F;
    std::array<CropId, 3> rotation;
    for (std::size_t slot = 0; slot < rotation.size(); ++slot) {
      rotation[slot] = CropByKey(crops, layout.CellText(row, rotation_cols[slot]));
    }
    const FieldId field =
        PlaceField(world, area, place, start_fertility, rotation[0], rotation[1], rotation[2]);
    if (derelict) {
      world.fields.rows[FindRow(world.fields, field)].kind = LandKind::kDerelict;
    }
  }
  return true;
}

/// @brief Puts the start stock where the layout says it lies (start_stock.csv,
/// boss numbers of 2026-08-31). Amounts are in each resource's own measure
/// and the row carries the mass of one, so the conversion to grams needs no
/// second table — the same one rule the recipes use.
/// @brief Capacity of a unit type in grams, or -1 for an outline the player
/// draws (a heap, a stack: no number to be full against). Reads the level
/// ladder first and the type's own figure second — the same order and the
/// same tables core_production reads, so the two never disagree about what
/// a granary holds.
Grams TypeCapacityGrams(const ITable* unit_types,
                        const ITable* unit_levels,
                        UnitTypeId type,
                        std::uint8_t level) {
  if (unit_types == nullptr || type.value == kInvalidDefIdValue ||
      type.value >= unit_types->RowCount()) {
    return -1;
  }
  const std::uint32_t by_plot_col = unit_types->FindColumn("capacity_by_plot");
  if (by_plot_col != kNoTableColumn && LayoutNumber(*unit_types, type.value, by_plot_col) > 0.0F) {
    return -1;
  }
  const std::string_view key = unit_types->CellText(type.value, 0);
  if (unit_levels != nullptr && level >= 1) {
    const std::uint32_t unit_col = unit_levels->FindColumn("unit");
    const std::uint32_t level_col = unit_levels->FindColumn("level");
    const std::uint32_t tonnes_col = unit_levels->FindColumn("storage_capacity_t");
    if (unit_col != kNoTableColumn && level_col != kNoTableColumn && tonnes_col != kNoTableColumn) {
      for (std::uint32_t row = 0; row < unit_levels->RowCount(); ++row) {
        if (unit_levels->CellText(row, unit_col) != key) {
          continue;
        }
        // Compared in FLOAT, never cast: LayoutNumber refuses nan and inf
        // but clamps only to +/-1e9, so a hand-edited level of -1 or 1e6
        // would make the cast undefined ([conv.fpint]/1). This is the very
        // class the cast pass exists to remove, and it was sitting two lines
        // from the conversion the pass did fix.
        if (LayoutNumber(*unit_levels, row, level_col) != static_cast<float>(level)) {
          continue;
        }
        const float tonnes = LayoutNumber(*unit_levels, row, tonnes_col);
        if (tonnes > 0.0F) {
          return GramsFromTonnes(tonnes);
        }
      }
    }
  }
  const std::uint32_t tonnes_col = unit_types->FindColumn("storage_capacity_t");
  if (tonnes_col == kNoTableColumn) {
    return -1;
  }
  const float tonnes = LayoutNumber(*unit_types, type.value, tonnes_col);
  return tonnes > 0.0F ? GramsFromTonnes(tonnes) : -1;
}

void PlaceStartStock(WorldState& world,
                     const ITable& stock,
                     const ITable* resources,
                     const ITable* unit_types,
                     const ITable* unit_levels,
                     const std::vector<std::pair<std::string_view, UnitId>>& placed) {
  const std::uint32_t place_col = stock.FindColumn("place");
  const std::uint32_t resource_col = stock.FindColumn("resource");
  const std::uint32_t amount_col = stock.FindColumn("amount");
  const std::uint32_t mass_col = stock.FindColumn("kg_per_unit");
  if (place_col == kNoTableColumn || resource_col == kNoTableColumn) {
    return;
  }
  for (std::uint32_t row = 0; row < stock.RowCount(); ++row) {
    const std::string_view where = stock.CellText(row, place_col);
    UnitId unit;
    for (const std::pair<std::string_view, UnitId>& entry : placed) {
      if (entry.first == where) {
        unit = entry.second;
        break;
      }
    }
    const std::uint32_t unit_row = FindRow(world.units, unit);
    const ResourceId resource = GenesisResource(resources, stock.CellText(row, resource_col));
    if (unit_row == kNoRow || resource.value == kInvalidDefIdValue) {
      continue;
    }
    UnitRow& place = world.units.rows[unit_row];
    const float kilograms =
        LayoutNumber(stock, row, amount_col) * LayoutNumber(stock, row, mass_col);
    PutStock(place, resource, kilograms);
    // The start set must FIT where the canon puts it ("capacity — exactly
    // the start set, no more", start design §5). A row that overfills its
    // place is a table error, not a game state: genesis is setup code and
    // may say so out loud, and what did not fit is booked to the year's
    // no_room so the first report shows it rather than hiding it (task A3).
    const Grams capacity = TypeCapacityGrams(unit_types, unit_levels, place.type, place.level);
    if (capacity < 0) {
      continue;
    }
    Grams held = 0;
    for (const Grams amount : place.stock) {
      if (amount > 0) {
        held += amount;
      }
    }
    if (held <= capacity) {
      continue;
    }
    const Grams over = held - capacity;
    LogError("genesis: start stock overfills '" + std::string(where) +
             "'; the excess is dropped and booked as no_room");
    // MEM-001 fix. The read below was written defensively and the write two
    // lines under it was not: a row PutStock refused never grew the vector,
    // so this cell may not exist — and then there is nothing here to trim
    // anyway (`cut` would be zero), while the write would be a heap overrun
    // with nothing to show for it.
    if (resource.value >= place.stock.size()) {
      continue;
    }
    const Grams here = place.stock[resource.value];
    const Grams cut = over < here ? over : here;
    place.stock[resource.value] = here - cut;
    AddLedgerAmount(world.ledger.current.no_room, resource, cut);
  }
}

/// @brief The start economy of the canon (start.md §10-§11): the surviving
/// units with the stores in the church, 160 ha of arable land with the
/// suggested first-year plan of the reference run (70 ha sown: 62% grain,
/// 18% potatoes, 8% flax, 12% fodder), 200 ha of meadows and the animals of
/// PlaceHerds above. A table-less world (unit tests) gets none of this and
/// stays people-only.
/// Start fertility 65 = soil factor 1.3 of the reference runs; stock
/// amounts are ASSUMPTION sized to the first sowing plus a food margin.
void BuildStartEconomy(WorldState& world, const ITableSet& tables) {
  const ITable* unit_types = tables.FindTable("unit_types");
  const ITable* unit_levels = tables.FindTable("unit_levels");
  const ITable* resources = tables.FindTable("resources");
  const ITable* crops = tables.FindTable("crops");
  if (unit_types == nullptr || resources == nullptr || crops == nullptr) {
    return;  // people-only world until the tables exist
  }
  constexpr Metric kStartFertility = 65.0F;
  const UnitTypeId house_type = TypeByKey(unit_types, "old_house");

  // WHERE EVERYTHING STANDS COMES FROM THE TABLE, not from this function.
  // The layout is the hand-designed scene of the start canon, exported from
  // the design db; the areas and rotations in it are the core's own, carried
  // over unchanged from the reconciliation (69-reconciliation.md). Without
  // the table there is no scene to build, and the world stays people-only —
  // the same answer genesis gives without any other table.
  const ITable* const layout = tables.FindTable("start_layout");
  if (layout == nullptr) {
    LogError("genesis: no start_layout table — the world stays people-only");
    return;
  }
  std::vector<std::pair<std::string_view, UnitId>> placed;
  // The side of the map is data and lives in exactly one place — map.csv,
  // exported from db/map.db. Zero when the table set has none.
  float map_side_m = 0.0F;
  if (const ITable* const map = tables.FindTable("map")) {
    const std::uint32_t side_col = map->FindColumn("side_m");
    if (map->RowCount() > 0) {
      map_side_m = LayoutNumber(*map, 0, side_col);
    }
  }
  if (!PlaceStartLayout(world, *layout, unit_types, crops, kStartFertility, map_side_m, placed)) {
    LogError("genesis: start_layout has no key or kind column");
    return;
  }

  // The units the rest of this function needs by name. A layout without one
  // of them is not an error here: the herd simply has nowhere to stand, and
  // that shows up as a herd with no unit rather than as a crash.
  const auto unit_by_key = [&placed](std::string_view key) {
    for (const std::pair<std::string_view, UnitId>& entry : placed) {
      if (entry.first == key) {
        return entry.second;
      }
    }
    return UnitId{};
  };
  const UnitId stock_yard = unit_by_key("cattle_yard");
  const UnitId compost = unit_by_key("manure_pile");

  // One decrepit house per starting family, and the houses are the layout's
  // own yard_01..yard_21 in the order it lists them — the canon places every
  // one of them by hand (start canon §2). Families take them in row order:
  // the twenty-one yards and the twenty-one starting families are the same
  // twenty-one households seen from two sides.
  std::uint32_t family_row = 0;
  for (const std::pair<std::string_view, UnitId>& entry : placed) {
    if (family_row >= world.families.rows.size()) {
      break;
    }
    const std::uint32_t unit_row = FindRow(world.units, entry.second);
    if (unit_row == kNoRow || world.units.rows[unit_row].type.value != house_type.value) {
      continue;
    }
    world.units.rows[unit_row].household = world.families.row_ids[family_row];
    world.families.rows[family_row].house = entry.second;
    ++family_row;
  }

  // THE OLD HOUSES START PART WORN (start design §4), and not all at the
  // same number: a spread of fifteen points puts their collapses years
  // apart instead of dropping twenty-one roofs in one night (boss,
  // 2026-09-03; task A5). Drawn from the campaign seed like every other
  // start draw, so the same seed gives the same village — and the band
  // itself comes from the table, not from this file.
  float wear_min = kOldHouseWearMinDefault;
  float wear_max = kOldHouseWearMaxDefault;
  if (const ITable* const knobs = tables.FindTable("construction")) {
    const std::uint32_t column = knobs->FindColumn("value");
    const std::uint32_t min_row = knobs->FindRowByKey("old_house_wear_min");
    const std::uint32_t max_row = knobs->FindRowByKey("old_house_wear_max");
    if (column != kNoTableColumn && min_row != kNoTableRow) {
      wear_min = LayoutNumber(*knobs, min_row, column);
    }
    if (column != kNoTableColumn && max_row != kNoTableRow) {
      wear_max = LayoutNumber(*knobs, max_row, column);
    }
  }
  // A band that is not a band — reversed, negative, past the scale — is a
  // table error, and the canonical figures stand instead of a wrong world.
  if (!(wear_min >= 0.0F && wear_max <= kWearScale && wear_min <= wear_max)) {
    LogWarning("genesis: the old-house wear band is unusable; the canonical 45..60 is used");
    wear_min = kOldHouseWearMinDefault;
    wear_max = kOldHouseWearMaxDefault;
  }
  for (UnitRow& unit : world.units.rows) {
    if (unit.type.value != house_type.value || unit.level == 0) {
      continue;
    }
    unit.wear = wear_min + (NextRandomUnitFloat(world.rng) * (wear_max - wear_min));
  }

  // WHAT LIES WHERE comes from the table too (start_stock.csv, boss numbers
  // of 2026-08-31). It is written by places, not by one store, and that is
  // the point: 165 t of hay never fitted in a church that holds 60, and the
  // canon's "all the start resources are in the church" turned out to be
  // unimplementable as written. The fodder lies in a haystack, the building
  // materials on the build yard and in the manor ruins — where the stone is
  // the only stone there is until a quarry, so that "while the ruins stand,
  // stone building is closed" is finally true.
  //
  // The sizes themselves follow one rule worth keeping: a start stock that
  // gets consumed is measured from the nearest moment the farm replenishes
  // it itself — the cut for fodder, the harvest for food, the first felling
  // for firewood. That is why the hay is 165 t and not a round 100: the herd
  // eats 7.3 t a game day and the scythes go out on day 22.
  const ITable* const start_stock = tables.FindTable("start_stock");
  if (start_stock != nullptr) {
    PlaceStartStock(world, *start_stock, resources, unit_types, unit_levels, placed);
  }

  // What the households still have of their own. The village was living
  // before the kolkhoz was declared, and it is declared in January: yards
  // with bare shelves would spend the whole first spring on nothing at all,
  // because trudodni are earned by work and there is no field work until
  // April. That is not a hard start, it is an empty one.
  // ASSUMPTION on the amounts: potatoes as the peasant staple, a little
  // grain and what is left of the cellar — about two months to the first
  // issue (start canon §7 gives the stores, not the larders).
  for (FamilyRow& family : world.families.rows) {
    PutPantry(family, GenesisResource(resources, "potato"), 400);
    PutPantry(family, GenesisResource(resources, "oat"), 80);
    PutPantry(family, GenesisResource(resources, "vegetables"), 60);
    // And hay for the goats, by the same rule that measures the kolkhoz's
    // own fodder: from the start to the nearest moment the yard replenishes
    // it itself, which for a yard is its OWN cut in August (food.csv,
    // hay_harvest_month) — two months after the kolkhoz's.
    //
    // Two goats eat 2 fodder units a real day each: 1460 units over a real
    // year, of which January to August is a third at the full rate and the
    // four pasture months a fifth of it — about 584 units, and hay carries
    // 0.45 units to the kilogram. Hence 1.3 t.
    //
    // Without it the yards' goats starved from day one and died before the
    // first cut, and no path in phase 1 brings them back: private herds do
    // not breed by canon (69-reconciliation.md §3 D3). That made the first
    // year irreversible, which the design forbids outright.
    PutPantry(family, GenesisResource(resources, "hay"), 1300);
  }
  // Guarded like every other lookup on this path: unit_by_key answers with
  // an invalid id when the layout carries no such row, FindRow turns that
  // into kNoRow, and indexing the vector with kNoRow writes four gigabytes
  // past its end. The shipped layout has the heap; a doctored table set for
  // a test need not, and a start without manure is a table problem, not a
  // crash. MEM-001 fix.
  const std::uint32_t compost_row = FindRow(world.units, compost);
  if (compost_row != kNoRow) {
    PutStock(world.units.rows[compost_row], GenesisResource(resources, "manure"), 250000);
  } else {
    LogWarning("genesis: the layout has no manure_pile; the start begins without compost");
  }

  // The land is in the layout table too, and its numbers are the core's own
  // going the other way: the areas and the three-year rotation were
  // reconciled here (69-reconciliation.md) and exported into the registry,
  // so what comes back is what was agreed — 160.0 ha of arable to the
  // hectare, 200.0 of meadow, plus the 3 ha reserve held back for building
  // on, which the 160 deliberately does not count (start canon §2).
  //
  // Why the rotation had to be sound rather than one crop per field: the old
  // genesis sowed each field its single crop in all three slots, and the run
  // showed what that costs — the wheat field went from 65 fertility to 2 in
  // six years, and "the village does not go hungry under sound management"
  // cannot be tested on management that is not sound. The rings are
  // staggered so every year has potatoes, vegetables, grain, oats and grass;
  // grass stands two slots, because a stand sown in spring is not cut that
  // year and the winter rye that follows it must follow a stand that HAS
  // been cut.

  PlaceHerds(world, tables, stock_yard);
}

}  // namespace

WorldState CreateStartWorld(const ITableSet& tables, std::uint64_t world_seed) {
  WorldState world;
  world.world_seed = world_seed;
  world.rng = SeedRngState(world_seed, kWorldRngStream);
  world.calendar.day_zero_weekday = CampaignDayZeroWeekday(tables);
  RefreshCalendarCaches(world.calendar);

  // Value validation (UB-002): the float-to-uint cast of a negative or huge
  // table value is UB — validate before casting, fall back loudly.
  float population_value = CampaignValue(tables, "start_population", 80.0F);
  if (!(population_value >= 1.0F && population_value <= 100000.0F)) {
    LogError("campaign: start_population out of range; using 80");
    population_value = 80.0F;
  }
  float households_value = CampaignValue(tables, "start_households", 21.0F);
  if (!(households_value >= 1.0F && households_value <= 10000.0F)) {
    LogError("campaign: start_households out of range; using 21");
    households_value = 21.0F;
  }
  const auto population = static_cast<std::uint32_t>(population_value);
  const auto households = static_cast<std::uint32_t>(households_value);
  float life_speedup = LifeSpeedupFromTables(tables);
  // Positive test so that NaN falls back too, and a floor that keeps the
  // age-to-day division from producing values no int32 can hold.
  if (!(life_speedup >= 0.1F && life_speedup <= 1000.0F)) {
    LogError("life: life_speedup out of range; using 4");
    life_speedup = 4.0F;
  }
  RngState& rng = world.rng;

  // The pyramid in whole people.
  const auto old_count =
      static_cast<std::uint32_t>(static_cast<float>(population) * kShareOld + 0.5F);
  const auto under_seven =
      static_cast<std::uint32_t>(static_cast<float>(population) * kShareUnderSeven + 0.5F);
  const auto school_age =
      static_cast<std::uint32_t>(static_cast<float>(population) * kShareSchool + 0.5F);
  const std::uint32_t children = under_seven + school_age;
  const std::uint32_t adults = population - old_count - children;

  // Old-timers first: pairs in their own yards, then a single.
  std::uint32_t old_households = 0;
  for (std::uint32_t placed = 0; placed < old_count; placed += 2) {
    const FamilyId yard = AppendRow(world.families, RollFamily(rng));
    ++old_households;
    const float age = DrawInRange(rng, 62.0F, 74.0F);
    ResidentRow first = RollPerson(rng, Sex::kMale, age, life_speedup);
    first.family = yard;
    const ResidentId first_id = AppendRow(world.residents, first);
    if (placed + 1 < old_count) {
      ResidentRow second = RollPerson(rng, Sex::kFemale, age - 2.0F, life_speedup);
      second.family = yard;
      second.spouse = first_id;
      const ResidentId second_id = AppendRow(world.residents, second);
      world.residents.rows[FindRow(world.residents, first_id)].spouse = second_id;
    }
  }

  // Working couples fill the remaining yards.
  const std::uint32_t couple_households =
      households > old_households ? households - old_households : 1;
  const std::uint32_t couples = couple_households < adults / 2 ? couple_households : adults / 2;
  std::array<ResidentId, 64> family_mothers = {};
  std::array<ResidentId, 64> family_fathers = {};
  for (std::uint32_t couple = 0; couple < couples; ++couple) {
    const FamilyId yard = AppendRow(world.families, RollFamily(rng));
    // Spread over the whole working band, as the reference pyramid does —
    // an all-fertile start would overheat the early growth.
    const float age = DrawInRange(rng, 20.0F, 58.0F);
    ResidentRow husband = RollPerson(rng, Sex::kMale, age, life_speedup);
    husband.family = yard;
    const ResidentId husband_id = AppendRow(world.residents, husband);
    ResidentRow wife = RollPerson(rng, Sex::kFemale, age - 2.0F, life_speedup);
    wife.family = yard;
    wife.spouse = husband_id;
    const ResidentId wife_id = AppendRow(world.residents, wife);
    world.residents.rows[FindRow(world.residents, husband_id)].spouse = wife_id;
    if (couple < family_mothers.size()) {
      family_mothers[couple] = wife_id;
      family_fathers[couple] = husband_id;
    }
  }

  // Unmarried grown-ups live with a couple as grown children; children are
  // dealt to the couples round-robin, with kinship links.
  const std::uint32_t grown = adults - couples * 2;
  for (std::uint32_t index = 0; index < grown + children; ++index) {
    const std::uint32_t host = couples == 0 ? 0 : index % couples;
    const ResidentId mother_id = family_mothers[host % family_mothers.size()];
    const std::uint32_t mother_row = FindRow(world.residents, mother_id);
    if (mother_row == kNoRow) {
      break;  // degenerate start parameters; genesis stays valid, just small
    }
    float age = 0.0F;
    if (index < grown) {
      age = DrawInRange(rng, 16.0F, 21.0F);
    } else if (index - grown < under_seven) {
      age = DrawInRange(rng, 0.5F, 6.5F);
    } else {
      age = DrawInRange(rng, 7.0F, 15.5F);
    }
    const Sex sex = NextRandomUnitFloat(rng) < 0.5F ? Sex::kFemale : Sex::kMale;
    ResidentRow child = RollPerson(rng, sex, age, life_speedup);
    child.family = world.residents.rows[mother_row].family;
    child.mother = mother_id;
    child.father = family_fathers[host % family_fathers.size()];
    AppendRow(world.residents, child);
  }

  if (world.residents.rows.size() != population) {
    LogWarning("genesis: start parameters rounded population to " +
               std::to_string(world.residents.rows.size()));
  }
  BuildStartEconomy(world, tables);
  return world;
}

}  // namespace core
