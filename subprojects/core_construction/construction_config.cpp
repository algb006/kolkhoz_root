// Parsing of the build data (construction_config.h): five exported tables
// plus one hand-written one.
//
// Five of the six are EXPORTS of the design db and must never be edited by
// hand (manual/61-balance-tables.md §4а). That is why this file is fussy
// about column names and cross-table agreement: when a column moves
// upstream, the parse has to fail loudly, never quietly return a plausible
// default. The one hand-written table here is construction.csv, and it
// taught its own lesson — a comment with quotation marks in it took the
// whole table set down, so it now carries none.
//
// Policy, shared with every subsystem factory: a MISSING table keeps the
// documented defaults (a unit test's world has no tables at all, and then
// nothing can be built — the honest answer), while a PRESENT table that
// cannot be read, or that contradicts another, refuses the subsystem.

#include "construction_config.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_construction/construction_system.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// Bounds every number read here. Generous on purpose: the point is to
/// catch a moved column or a garbled cell, not to police the balance.
constexpr float kMaxLaborDays = 100'000.0F;
constexpr float kMaxAmount = 10'000'000.0F;
constexpr float kMaxKgPerUnit = 100'000.0F;
/// A store bigger than this is a table error, not a plan: the largest thing
/// the design names is a 2500 t elevator.
constexpr float kMaxStorageTonnes = 1e6F;

/// An amortization term longer than this is a table error rather than a
/// plan: nothing in the design outlives a campaign by four orders.
constexpr float kMaxWearYears = 10'000.0F;

/// A pace multiplier outside this is a table error: the design's fastest
/// named exception is 1.6.
constexpr float kMaxWearFactor = 100.0F;

/// Grams in a tonne — the tables state stores in tonnes, the core counts
/// grams (state model §5).
constexpr Grams kGramsPerTonne = 1'000'000;

constexpr std::uint32_t kMaxEra = 3;
constexpr std::uint32_t kMaxLevel = 32;
constexpr std::uint32_t kMaxCrewCeiling = 255;

/// The build class that means "the player draws the outline": no work, no
/// materials (unit rules §9 — the second of the two legal cases for a type
/// that has a plot and no radius).
constexpr std::string_view kMarkingClass = "plot";

void Fail(std::string& error, std::string_view table, std::string_view what) {
  error = std::string(table);
  error += ": ";
  error += what;
}

/// @brief The gate a `gate` cell names; `known` reports success. An empty
/// cell is the era gate, which is why an unknown WORD cannot be told from a
/// default by the return value alone.
///
/// THIS BLOCK USED TO DESCRIBE SOMEBODY ELSE (UB-001, 2026-09-07). It read
/// "a cell as a real number, `fallback` for an empty one" — the contract of
/// CellOrDefault, which has since moved to core_catalog/table_value.h and
/// left its documentation standing over the next function down. Its two
/// neighbours below say `known` reports success and this one said nothing,
/// so a reader of the contract alone would have used the returned kEra
/// without asking.
UnitGate GateFromText(std::string_view text, bool& known) {
  known = true;
  if (text.empty() || text == "era") {
    return UnitGate::kEra;
  }
  if (text == "start") {
    return UnitGate::kStart;
  }
  if (text == "event") {
    return UnitGate::kEvent;
  }
  if (text == "quest") {
    return UnitGate::kQuest;
  }
  if (text == "unit") {
    return UnitGate::kUnit;
  }
  known = false;
  return UnitGate::kEra;
}

/// @brief Grams per one unit of a resource, from resources.csv. Zero means
/// "the tables carry no mass for it", which is a refusal wherever a recipe
/// names it: a material with no mass cannot be moved, stored or spent.
/// @param error Now carried, because the shared reader has a reason to
///        give and the old private one did not: a bare false told the
///        loader that something in resources.csv was wrong and nothing more.
bool ReadResourceMass(const ITable& resources,
                      std::vector<Grams>& grams_per_unit,
                      std::string& error) {
  const std::uint32_t column = resources.FindColumn("kg_per_unit");
  grams_per_unit.assign(resources.RowCount(), 0);
  if (column == kNoTableColumn) {
    return true;  // an older export: every recipe naming a resource refuses
  }
  for (std::uint32_t row = 0; row < resources.RowCount(); ++row) {
    float kg = 0.0F;
    if (!CellOrDefault(
            resources, row, column, Range{.low = 0.0F, .high = kMaxKgPerUnit}, 0.0F, kg, error)) {
      PrefixError("resources", resources.CellText(row, 0), error);
      return false;
    }
    grams_per_unit[row] = static_cast<Grams>(static_cast<double>(kg) * kGramsPerKilogram);
  }
  return true;
}

/// @brief The stink band a `stink` cell names; `known` reports success.
StinkStrength StinkFromText(std::string_view text, bool& known) {
  known = true;
  if (text == "none" || text.empty()) {
    return StinkStrength::kNone;
  }
  if (text == "weak") {
    return StinkStrength::kWeak;
  }
  if (text == "medium") {
    return StinkStrength::kMedium;
  }
  if (text == "strong") {
    return StinkStrength::kStrong;
  }
  known = false;
  return StinkStrength::kNone;
}

/// @brief The two words `stink_when` may hold; `known` reports success.
StinkWhen StinkWhenFromText(std::string_view text, bool& known) {
  known = true;
  if (text == "always") {
    return StinkWhen::kAlways;
  }
  if (text == "working") {
    return StinkWhen::kWorking;
  }
  known = false;
  return StinkWhen::kAlways;
}

bool ReadTypes(const ITable& unit_types, ConstructionConfig& config, std::string& error) {
  const std::uint32_t gate_col = unit_types.FindColumn("gate");
  const std::uint32_t built_col = unit_types.FindColumn("player_built");
  const std::uint32_t era_col = unit_types.FindColumn("era");
  const std::uint32_t by_plot_col = unit_types.FindColumn("capacity_by_plot");
  const std::uint32_t has_wear_col = unit_types.FindColumn("has_wear");
  const std::uint32_t wear_factor_col = unit_types.FindColumn("wear_factor");
  const std::uint32_t stink_col = unit_types.FindColumn("stink");
  const std::uint32_t stink_when_col = unit_types.FindColumn("stink_when");

  config.wear_column_present = has_wear_col != kNoTableColumn;
  config.types.assign(unit_types.RowCount(), BuildType{});
  for (std::uint32_t row = 0; row < unit_types.RowCount(); ++row) {
    BuildType& type = config.types[row];
    bool known = true;
    type.gate = gate_col == kNoTableColumn
                    ? UnitGate::kEra
                    : GateFromText(unit_types.CellText(row, gate_col), known);
    if (!known) {
      Fail(error, "unit_types", "unknown gate kind in row " + std::to_string(row));
      return false;
    }
    float number = 0.0F;
    if (!CellOrDefault(
            unit_types, row, built_col, Range{.low = 0.0F, .high = 1.0F}, 0.0F, number, error)) {
      Fail(error, "unit_types", "player_built is not 0 or 1 in row " + std::to_string(row));
      return false;
    }
    type.player_built = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(unit_types,
                       row,
                       era_col,
                       Range{.low = 1.0F, .high = static_cast<float>(kMaxEra)},
                       1.0F,
                       number,
                       error)) {
      Fail(error, "unit_types", "era is out of range in row " + std::to_string(row));
      return false;
    }
    type.era = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(
            unit_types, row, by_plot_col, Range{.low = 0.0F, .high = 1.0F}, 0.0F, number, error)) {
      Fail(error, "unit_types", "capacity_by_plot is not 0 or 1 in row " + std::to_string(row));
      return false;
    }
    type.capacity_by_plot = static_cast<std::uint8_t>(number);
    // Absent COLUMN = 0 for every type, and that means NOTHING WEARS. The
    // honest reading of "no data" (task A5, manual/73-wear-and-repair.md
    // §2): deriving it from the capacity flag or the recipe would be a
    // guess wearing the clothes of a rule.
    //
    // AN EMPTY CELL IN A PRESENT COLUMN IS A DIFFERENT ANSWER, and until
    // task A6 the config could not tell the two apart: both arrived as
    // has_wear = 0, so a hole in the export read as "this one does not wear
    // out" and a whole class of units would have quietly stopped ageing.
    // The column-level flag closed it at the level of the column; this
    // closes it at the level of the cell, which is where it was open.
    //
    // AND IT USED TO ASK THE TABLE A SECOND TIME to get here — the branch
    // re-tested `has_wear_col != kNoTableColumn` because one `kAbsent` stood
    // for all three kinds of nothing. That was a patch on the enumerator
    // rather than a fix of it, which is exactly why it never spread to the
    // ninety other columns with the same hole. Task A6 split the enumerator;
    // the second question is gone and the switch says what it means.
    //
    // The answer is a REFUSAL and not a default. A column that is there is
    // a column the export means to fill: a blank in it is a hole in the
    // data, not a value, and the one thing that must not happen is for it
    // to read as the answer that costs nothing to notice.
    switch (
        ReadCell(unit_types, row, has_wear_col, Range{.low = 0.0F, .high = 1.0F}, number, error)) {
      case CellState::kRead:
        type.has_wear = static_cast<std::uint8_t>(number);
        break;
      case CellState::kEmpty:
        Fail(error,
             "unit_types",
             "has_wear is empty in row " + std::to_string(row) +
                 " — a present column must answer for every type");
        return false;
      case CellState::kNoColumn:
      case CellState::kNoRow:
        type.has_wear = 0;
        break;
      case CellState::kBad:
        Fail(error, "unit_types", "has_wear is not 0 or 1 in row " + std::to_string(row));
        return false;
    }
    // An empty cell is 1.0 — the class's own pace — because the column names
    // only the exceptions the design lists by name. The floor is ZERO, not
    // one: a type that outlasts its class, a stone shed among timber ones,
    // is as legitimate as one that burns through it, and a floor of 1.0
    // refused the first kind outright. A cell of exactly 0 would mean "never
    // wears", which is what has_wear says, so it falls back to the class.
    if (!CellOrDefault(unit_types,
                       row,
                       wear_factor_col,
                       Range{.low = 0.0F, .high = kMaxWearFactor},
                       1.0F,
                       type.wear_factor,
                       error)) {
      Fail(error, "unit_types", "wear_factor is out of range in row " + std::to_string(row));
      return false;
    }
    if (!(type.wear_factor > 0.0F)) {
      type.wear_factor = 1.0F;
    }

    // THE STINK, AND THE TWO COLUMNS ARE ONE FACT WITH TWO HALVES. An
    // absent column is no data and every type is odourless, which is the
    // same honest reading has_wear takes above; a PRESENT column with a
    // word nobody knows is a refusal, because a misspelt "stong" reading as
    // "no smell" is exactly how a strong source disappears in silence.
    // AN ABSENT COLUMN AND AN EMPTY CELL ARE TWO ANSWERS, and this block
    // treated them as one for an hour. `has_wear` twelve lines above already
    // draws the line — "a present column must answer for every type" — and
    // the comment below argues the very same case for a misspelt word, so
    // reading a BLANK as "does not smell" was this file disagreeing with
    // itself. A hole in the export disappears a strong source exactly as
    // quietly as a typo does; found by the cycle, out of its own class.
    const std::string_view stink_text =
        stink_col == kNoTableColumn ? std::string_view{} : unit_types.CellText(row, stink_col);
    if (stink_col != kNoTableColumn && stink_text.empty()) {
      Fail(error,
           "unit_types",
           "stink is empty in row " + std::to_string(row) +
               " — a present column must answer for every type");
      return false;
    }
    type.stink = StinkFromText(stink_text, known);
    if (!known) {
      Fail(error,
           "unit_types",
           "stink is not none/weak/medium/strong in row " + std::to_string(row));
      return false;
    }
    const std::string_view when_text = stink_when_col == kNoTableColumn
                                           ? std::string_view{}
                                           : unit_types.CellText(row, stink_when_col);
    if (type.stink == StinkStrength::kNone) {
      // The table leaves stink_when empty exactly where there is nothing to
      // say, and a word here would be a second answer to a question nobody
      // asked. Refused rather than ignored: the pair is a contract, and the
      // export that breaks it on one row has broken it on more.
      if (!when_text.empty()) {
        Fail(error,
             "unit_types",
             "stink_when is set while stink is none in row " + std::to_string(row));
        return false;
      }
      type.stink_when = StinkWhen::kAlways;
      continue;
    }
    if (when_text.empty()) {
      Fail(error,
           "unit_types",
           "stink_when is empty while stink is not none in row " + std::to_string(row) +
               " — a source must say whether its contents or its work smells");
      return false;
    }
    type.stink_when = StinkWhenFromText(when_text, known);
    if (!known) {
      Fail(error, "unit_types", "stink_when is not always/working in row " + std::to_string(row));
      return false;
    }
  }
  return true;
}

/// @brief Refuses a table set whose store capacities cannot be read off the
///        level ladder, which is now the only place they live.
///
/// Two shapes, and both end in a SILENT ZERO — a store that holds nothing
/// looks exactly like a store nobody has filled, which is what host stumbled
/// over on 2026-09-05.
///
/// 1. The type row still names storage_capacity_t while no level row does.
///    That is the leftover column talking, and the load says so rather than
///    losing the number without a word.
/// 2. The ladder names a capacity at one step and leaves another blank. A
///    granary of 150 t at level 1 and nothing at level 2 used to borrow the
///    type's figure for level 2; it would now be a warehouse that empties
///    itself the day it is enlarged.
bool CheckCapacityLadder(const ITable& unit_types, ConstructionConfig& config, std::string& error) {
  const std::uint32_t tonnes_col = unit_types.FindColumn("storage_capacity_t");
  for (std::uint32_t row = 0; row < config.types.size() && row < unit_types.RowCount(); ++row) {
    const BuildType& type = config.types[row];
    const std::string key(unit_types.CellText(row, 0));
    bool named_anywhere = false;
    for (const BuildLevel& step : type.levels) {
      named_anywhere = named_anywhere || step.storage_capacity_grams > 0;
    }
    float in_type = 0.0F;
    if (!CellOrDefault(unit_types,
                       row,
                       tonnes_col,
                       Range{.low = 0.0F, .high = kMaxStorageTonnes},
                       0.0F,
                       in_type,
                       error)) {
      Fail(error, "unit_types", "storage_capacity_t is out of range in row " + std::to_string(row));
      return false;
    }
    if (in_type > 0.0F && !named_anywhere) {
      Fail(error,
           "unit_types",
           key +
               " names storage_capacity_t but no unit_levels.csv row gives it a capacity — "
               "the type column is not read any more, and the ladder is the only place a "
               "capacity lives");
      return false;
    }
    if (!named_anywhere || type.capacity_by_plot != 0) {
      continue;
    }
    for (std::size_t index = 0; index < type.levels.size(); ++index) {
      if (type.levels[index].storage_capacity_grams <= 0) {
        Fail(error,
             "unit_levels",
             key + " level " + std::to_string(index + 1) +
                 " names no storage_capacity_t while another level does — a blank step is a hole "
                 "in the ladder, not a capacity of zero");
        return false;
      }
    }
  }
  return true;
}

bool ReadLevels(const ITable& levels,
                const ITable& unit_types,
                ConstructionConfig& config,
                std::string& error) {
  const std::uint32_t unit_col = levels.FindColumn("unit");
  const std::uint32_t level_col = levels.FindColumn("level");
  const std::uint32_t era_col = levels.FindColumn("era");
  const std::uint32_t days_col = levels.FindColumn("labor_days");
  const std::uint32_t class_col = levels.FindColumn("build_class");
  const std::uint32_t crew_col = levels.FindColumn("max_crew");
  const std::uint32_t tonnes_col = levels.FindColumn("storage_capacity_t");
  const std::uint32_t idle_col = levels.FindColumn("wear_years_idle");
  const std::uint32_t in_use_col = levels.FindColumn("wear_years_in_use");
  const std::uint32_t pace_col = levels.FindColumn("wear_factor");
  if (unit_col == kNoTableColumn || level_col == kNoTableColumn) {
    Fail(error, "unit_levels", "no 'unit' or 'level' column");
    return false;
  }

  // WHICH RUNGS A ROW ACTUALLY FILLED. `ladder.resize(level)` below grows the
  // ladder to the highest level a row names, and the rungs it steps over are
  // default-constructed — a ladder whose only row is level 3 gains a level 1
  // and a level 2 that no line of any table ever wrote. Nothing in
  // BuildLevel can tell those apart afterwards, because a rung written as
  // zero and a rung never written look identical once the loop is over. So
  // the presence is recorded HERE, where the difference still exists.
  std::vector<std::vector<std::uint8_t>> filled(config.types.size());

  for (std::uint32_t row = 0; row < levels.RowCount(); ++row) {
    const std::uint32_t type_row = unit_types.FindRowByKey(levels.CellText(row, unit_col));
    if (type_row == kNoTableRow || type_row >= config.types.size()) {
      Fail(error, "unit_levels", "row " + std::to_string(row) + " names an unknown unit");
      return false;
    }
    float number = 0.0F;
    if (!CellOrDefault(levels,
                       row,
                       level_col,
                       Range{.low = 1.0F, .high = static_cast<float>(kMaxLevel)},
                       0.0F,
                       number,
                       error) ||
        number < 1.0F) {
      Fail(error, "unit_levels", "level is out of range in row " + std::to_string(row));
      return false;
    }
    const auto level = static_cast<std::uint32_t>(number);

    std::vector<BuildLevel>& ladder = config.types[type_row].levels;
    if (ladder.size() < level) {
      ladder.resize(level);
    }
    if (filled[type_row].size() < level) {
      filled[type_row].resize(level, 0);
    }
    filled[type_row][level - 1] = 1;
    BuildLevel& step = ladder[level - 1];

    // Real man-days in the table, game man-days in the core: every consumer
    // divides once at parse time (root rules §9, calendar.h).
    if (!CellOrDefault(levels,
                       row,
                       days_col,
                       Range{.low = 0.0F, .high = kMaxLaborDays},
                       0.0F,
                       number,
                       error)) {
      Fail(error, "unit_levels", "labor_days is out of range in row " + std::to_string(row));
      return false;
    }
    step.labor_days = number / kRealDaysPerGameDay;
    if (!CellOrDefault(levels,
                       row,
                       crew_col,
                       Range{.low = 0.0F, .high = static_cast<float>(kMaxCrewCeiling)},
                       0.0F,
                       number,
                       error)) {
      Fail(error, "unit_levels", "max_crew is out of range in row " + std::to_string(row));
      return false;
    }
    step.max_crew = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(levels,
                       row,
                       era_col,
                       Range{.low = 1.0F, .high = static_cast<float>(kMaxEra)},
                       1.0F,
                       number,
                       error)) {
      Fail(error, "unit_levels", "era is out of range in row " + std::to_string(row));
      return false;
    }
    step.era = static_cast<std::uint8_t>(number);
    if (!CellOrDefault(levels,
                       row,
                       tonnes_col,
                       Range{.low = 0.0F, .high = kMaxStorageTonnes},
                       0.0F,
                       number,
                       error)) {
      Fail(
          error, "unit_levels", "storage_capacity_t is out of range in row " + std::to_string(row));
      return false;
    }
    step.storage_capacity_grams = static_cast<Grams>(number) * kGramsPerTonne;
    // A TERM MAY BE UNNAMED, BUT IT MAY NOT BE ZERO, and telling those two
    // apart is what task A6's split bought (2026-09-07).
    //
    // A BLANK is legitimate and common: fifteen levels — an orchard, a road,
    // a well, a cemetery's marked plot — are a place rather than a building,
    // and a place names no amortization term. It reads as zero, and
    // WearDeadline's `!(years > 0)` turns that into "never wears", which is
    // the right answer.
    //
    // A WRITTEN ZERO would arrive at exactly the same stored value and be
    // INVERTED on the way: a term of zero years means the whole scale is
    // consumed in no time — ruined the day it is built — and it would come
    // out as "never wears at all", the opposite. Nothing in the shipped
    // tables writes one today, which is precisely why this is worth a guard
    // rather than a note: the day somebody does, nothing would say so.
    //
    // Only the columns where blank and zero MEAN DIFFERENT THINGS get this.
    // labor_days carries fourteen written zeros that are honest — a barter
    // place costs no labour — and max_crew's blank and zero both mean "no
    // crew", so a floor there would be strictness with no subject, which is
    // how a checker becomes noisy and then ignored.
    for (const auto& term : {std::pair{idle_col, &BuildLevel::wear_years_idle},
                             std::pair{in_use_col, &BuildLevel::wear_years_in_use}}) {
      float years = 0.0F;
      switch (ReadCell(
          levels, row, term.first, Range{.low = 0.0F, .high = kMaxWearYears}, years, error)) {
        case CellState::kRead:
          if (!(years > 0.0F)) {
            Fail(error,
                 "unit_levels",
                 "a wear term is written as zero in row " + std::to_string(row) +
                     " — a term of no years is not a term; leave the cell BLANK to say this "
                     "level names none");
            return false;
          }
          step.*term.second = years;
          break;
        case CellState::kEmpty:
        case CellState::kNoColumn:
        case CellState::kNoRow:
          step.*term.second = 0.0F;
          break;
        case CellState::kBad:
          Fail(error, "unit_levels", "a wear term is out of range in row " + std::to_string(row));
          return false;
      }
    }
    // An empty cell is 1.0 — this step adds nothing to its class's term —
    // and the floor is the same as the type column's, for the same reason:
    // a pace of zero would be a building that never wears at all, and that
    // is what has_wear says, not what a multiplier says.
    if (!CellOrDefault(levels,
                       row,
                       pace_col,
                       Range{.low = 0.0F, .high = kMaxWearFactor},
                       1.0F,
                       step.wear_factor,
                       error)) {
      Fail(error, "unit_levels", "wear_factor is out of range in row " + std::to_string(row));
      return false;
    }
    if (!(step.wear_factor > 0.0F)) {
      step.wear_factor = 1.0F;
    }
    step.is_marking = static_cast<std::uint8_t>(
        class_col != kNoTableColumn && levels.CellText(row, class_col) == kMarkingClass ? 1 : 0);
  }

  // A LADDER WITH A HOLE IS REFUSED, AND THE HOLE IS NOT A FREE BUILDING
  // (boss, 2026-09-07). A rung the ladder grew past but no row ever wrote is
  // not "a level that costs nothing" — it is A MISSING LINE READ AS A VALUE,
  // and zero is the commonest way to arrange that mistake: the language's
  // default and the subject's default coincide only by accident
  // (architecture 8бе). In the design database an empty cell means "not
  // written up yet", so a hole here is somebody's PLAN OF WORK, and letting
  // it through would ship that plan as a building the player raises for
  // free.
  //
  // A unit meant to start at its third rung would be a decision with a field
  // of its own and a name said out loud. There is none today, and a silent
  // door to it is not wanted.
  for (std::uint32_t type_row = 0; type_row < config.types.size(); ++type_row) {
    const std::vector<BuildLevel>& ladder = config.types[type_row].levels;
    for (std::uint32_t rung = 0; rung < ladder.size(); ++rung) {
      if (rung < filled[type_row].size() && filled[type_row][rung] != 0) {
        continue;
      }
      Fail(error,
           "unit_levels",
           std::string(unit_types.CellText(type_row, 0)) + " has a level " +
               std::to_string(ladder.size()) + " but no level " + std::to_string(rung + 1) +
               " — a rung no row writes is a missing line, not a step that costs nothing");
      return false;
    }
  }
  return CheckCapacityLadder(unit_types, config, error);
}

bool ReadRecipes(const ITable& costs,
                 const ITable& unit_types,
                 const ITable& resources,
                 const std::vector<Grams>& grams_per_unit,
                 ConstructionConfig& config,
                 std::string& error) {
  const std::uint32_t unit_col = costs.FindColumn("unit");
  const std::uint32_t level_col = costs.FindColumn("level");
  const std::uint32_t resource_col = costs.FindColumn("resource");
  const std::uint32_t amount_col = costs.FindColumn("amount");
  if (unit_col == kNoTableColumn || level_col == kNoTableColumn || resource_col == kNoTableColumn ||
      amount_col == kNoTableColumn) {
    Fail(error, "unit_level_cost", "a required column is missing");
    return false;
  }

  for (std::uint32_t row = 0; row < costs.RowCount(); ++row) {
    const std::uint32_t type_row = unit_types.FindRowByKey(costs.CellText(row, unit_col));
    if (type_row == kNoTableRow || type_row >= config.types.size()) {
      Fail(error, "unit_level_cost", "row " + std::to_string(row) + " names an unknown unit");
      return false;
    }
    float number = 0.0F;
    if (!CellOrDefault(costs,
                       row,
                       level_col,
                       Range{.low = 1.0F, .high = static_cast<float>(kMaxLevel)},
                       0.0F,
                       number,
                       error) ||
        number < 1.0F) {
      Fail(error, "unit_level_cost", "level is out of range in row " + std::to_string(row));
      return false;
    }
    const auto level = static_cast<std::uint32_t>(number);
    std::vector<BuildLevel>& ladder = config.types[type_row].levels;
    if (level > ladder.size()) {
      Fail(error, "unit_level_cost", "row " + std::to_string(row) + " costs a level with no row");
      return false;
    }

    const std::uint32_t resource_row = resources.FindRowByKey(costs.CellText(row, resource_col));
    if (resource_row == kNoTableRow || resource_row >= grams_per_unit.size()) {
      Fail(error, "unit_level_cost", "row " + std::to_string(row) + " names an unknown resource");
      return false;
    }
    if (grams_per_unit[resource_row] <= 0) {
      // Without a mass the material cannot be spent, stored or carried, and
      // a silent zero would build everything out of nothing.
      Fail(error,
           "unit_level_cost",
           "row " + std::to_string(row) + " names a resource with no kg_per_unit");
      return false;
    }
    if (!CellOrDefault(
            costs, row, amount_col, Range{.low = 0.0F, .high = kMaxAmount}, 0.0F, number, error)) {
      Fail(error, "unit_level_cost", "amount is out of range in row " + std::to_string(row));
      return false;
    }

    BuildMaterial material;
    material.resource = ResourceId{static_cast<std::uint16_t>(resource_row)};
    material.grams = static_cast<Grams>(static_cast<double>(number) *
                                        static_cast<double>(grams_per_unit[resource_row]));
    ladder[level - 1].recipe.push_back(material);
  }
  return true;
}

/// @brief The cross-table check unit rules §9 asks for by name: a type that
/// says it has a plot must name either a radius or the marking class, and
/// "there is no third legal case".
bool CheckPlots(const ITable& unit_types, const ConstructionConfig& config, std::string& error) {
  const std::uint32_t has_plot_col = unit_types.FindColumn("has_plot");
  if (has_plot_col == kNoTableColumn) {
    return true;
  }
  for (std::uint32_t row = 0; row < config.types.size(); ++row) {
    const BuildType& type = config.types[row];
    if (type.player_built == 0 || unit_types.CellText(row, has_plot_col) != "1") {
      continue;
    }
    // The PLOT alone, never the body: this asks whether a type that claims
    // a plot names one, and a footprint is a different answer to a different
    // question. A well has a body and no plot, and it does not claim one.
    //
    // Indexed by the type row without a bound of its own: the equality of the
    // two lengths is stated where they are filled, in ParseConstructionConfig,
    // and a set that breaks it never reaches this function.
    if (config.definitions.units.plot_radius_m[row] > 0.0F) {
      continue;
    }
    const bool marked_out = !type.levels.empty() && type.levels.front().is_marking != 0;
    if (!marked_out) {
      Fail(error,
           "unit_types",
           "row " + std::to_string(row) +
               " is built and claims a plot, but names neither a radius nor the marking class");
      return false;
    }
  }
  return true;
}

}  // namespace

bool ParseConstructionConfig(const ITableSet& tables,
                             ConstructionConfig& config,
                             std::string& error) {
  const ITable* const knobs = tables.FindTable("construction");
  if (knobs != nullptr) {
    const std::uint32_t column = knobs->FindColumn("value");

    // key, ceiling, destination — the knobs of this subsystem, each keeping
    // its canonical default when the table does not name it (task A5).
    struct Knob {
      const char* key;
      float ceiling;
      float* value;
    };

    const Knob knob_list[] = {
        {"demolition_labor_share", 1.0F, &config.demolition_labor_share},
        {"repair_labor_share", 1.0F, &config.repair_labor_share},
        {"repair_spare_parts_per_labor_day", 1e3F, &config.repair_spare_parts_per_labor_day},
        {"old_house_collapse_years", 1e4F, &config.old_house_collapse_years},
    };
    for (const Knob& knob : knob_list) {
      const std::uint32_t row = knobs->FindRowByKey(knob.key);
      if (row != kNoTableRow && !CellOrDefault(*knobs,
                                               row,
                                               column,
                                               Range{.low = 0.0F, .high = knob.ceiling},
                                               *knob.value,
                                               *knob.value,
                                               error)) {
        Fail(error, "construction", std::string(knob.key) + " is out of range");
        return false;
      }
    }
  }

  // The catalogue first: the plot radii and the map side belong to it, and
  // this module reads them from there rather than from its own pass over
  // unit_types.csv — those two columns have a second reader, and a column
  // with two readers has an owner (core_catalog/definitions.h).
  if (!LoadDefinitions(tables, config.definitions, error)) {
    return false;
  }

  const ITable* const unit_types = tables.FindTable("unit_types");
  const ITable* const resources = tables.FindTable("resources");
  if (unit_types == nullptr || resources == nullptr) {
    return true;  // a table-less world: nothing can be built, and that is all
  }
  if (!ReadTypes(*unit_types, config, error)) {
    return false;
  }

  // ONE DOOR, AND IT IS HERE, WHERE BOTH VECTORS HAVE JUST BEEN FILLED
  // (boss, 2026-09-07). `config.types` above and the catalogue's
  // `plot_radius_m` are two readings of the SAME table, one row each, so
  // their lengths agree — and until this line nothing said so. CheckPlots
  // walks `config.types` and indexes the catalogue with the same row; a guard
  // standing next to that indexing would be the fourth of its kind and would
  // still leave the next reader to invent its own. Refused by name rather
  // than asserted: a table set is data, and data that disagrees with itself
  // is a load failure, not a bug in this core.
  //
  // THE INHERITED FACT IS CONTAINMENT, NOT EQUALITY, AND THE DIFFERENCE IS
  // NOT PEDANTRY (MEM-001, 2026-09-07). The early success six lines above —
  // no `resources` table — returns with the catalogue filled and `types`
  // still empty, so the two lengths are NOT equal on every path out of this
  // function. What holds everywhere, and what a reader may build on, is the
  // one direction that gets indexed:
  //
  //     for every row < config.types.size(), plot_radius_m[row] exists.
  //
  // Vacuously true where the type list is empty, enforced by this line where
  // it is not. A comment promising the symmetric equality would have invited
  // the opposite subscript — walking `Count()` and indexing `types` — and
  // that one has no door at all.
  if (config.definitions.units.Count() != config.types.size()) {
    Fail(error,
         "unit_types",
         "plot_radius_m has " + std::to_string(config.definitions.units.Count()) +
             " rows but the type list has " + std::to_string(config.types.size()) +
             "; both are one row per unit_types row and must agree");
    return false;
  }
  std::vector<Grams> grams_per_unit;
  if (!ReadResourceMass(*resources, grams_per_unit, error)) {
    Fail(error, "resources", "kg_per_unit is out of range");
    return false;
  }

  // What a repair is made of, and the one type that collapses instead of
  // standing as a ruin. Both are keys, resolved here so that no rule of the
  // subsystem has to know a string (task A5).
  config.spare_part_resource = ResourceId{};
  config.spare_part_grams = 0;
  const std::uint32_t spare_row = resources->FindRowByKey("spare_part");
  if (spare_row != kNoTableRow && spare_row < grams_per_unit.size() &&
      grams_per_unit[spare_row] > 0) {
    config.spare_part_resource = ResourceId{static_cast<std::uint16_t>(spare_row)};
    config.spare_part_grams = grams_per_unit[spare_row];
  }
  config.old_house_type = UnitTypeId{};
  const std::uint32_t old_house_row = unit_types->FindRowByKey("old_house");
  if (old_house_row != kNoTableRow) {
    config.old_house_type = UnitTypeId{static_cast<std::uint16_t>(old_house_row)};
  }

  // The walking pace, from the same table every other consumer reads it
  // from (transport.csv) and never copied into a table of this module's
  // own. Real km/h in the file; the chronometer divides.
  if (const ITable* const transport = tables.FindTable("transport")) {
    float kmh = 5.0F;
    if (!CellOrDefault(*transport,
                       transport->FindRowByKey("pedestrian"),
                       transport->FindColumn("speed_kmh"),
                       Range{.low = 0.5F, .high = 20.0F},
                       5.0F,
                       kmh,
                       error)) {
      Fail(error, "transport", "pedestrian speed_kmh is out of range");
      return false;
    }
    if (kmh > 0.0F) {
      config.walk_hours_per_km = static_cast<float>(kClockScale) / kmh;
    }
  }

  const ITable* const levels = tables.FindTable("unit_levels");
  if (levels == nullptr) {
    // No ladder: nothing can be built, and nothing has a capacity either —
    // which is a refusal and not a shrug for any type that still names one.
    return CheckCapacityLadder(*unit_types, config, error);
  }
  if (!ReadLevels(*levels, *unit_types, config, error)) {
    return false;
  }
  const ITable* const costs = tables.FindTable("unit_level_cost");
  if (costs != nullptr &&
      !ReadRecipes(*costs, *unit_types, *resources, grams_per_unit, config, error)) {
    return false;
  }
  return CheckPlots(*unit_types, config, error);
}

}  // namespace core
