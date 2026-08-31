// The run ledger as CSV (ledger_csv.h).
//
// ONE LIST OF COLUMNS, TWO MODES. EmitSheet below walks every column site
// once; ColumnWriter either prints the site's NAME (header mode) or its
// VALUE (row mode). That is why LedgerCsvHeader and LedgerCsvRow can never
// disagree on the column count or the order — there is one list, not two.

#include "core_report/ledger_csv.h"

#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {
namespace {

constexpr const char* kKeyColumn = "key";

/// Column-name suffix for a mass, so the unit is in the header where the
/// balance tables put it (manual/61-balance-tables.md §3).
constexpr const char* kKilogramSuffix = "_kg";

/// The work kinds that carry a column: everything but kNone, which is the
/// absence of an assignment and delivers nothing by definition.
constexpr std::array<const char*, kWorkKindCount> kWorkKindNames = {
    "none", "plowing", "harrowing", "sowing", "harvest", "herd_care"};

/// Grams as kilograms with three decimals — exact, and it reads back to the
/// same gram. Written by hand rather than through a float, which would lose
/// the last digits on a tonne.
std::string Kilograms(Grams grams) {
  const bool negative = grams < 0;
  const std::uint64_t magnitude =
      negative ? static_cast<std::uint64_t>(-(grams + 1)) + 1U : static_cast<std::uint64_t>(grams);
  const std::uint64_t whole = magnitude / static_cast<std::uint64_t>(kGramsPerKilogram);
  const std::uint64_t rest = magnitude % static_cast<std::uint64_t>(kGramsPerKilogram);
  std::string text = negative ? "-" : "";
  text += std::to_string(whole);
  text += '.';
  const std::string fraction = std::to_string(rest);
  text.append(3 - fraction.size(), '0');
  text += fraction;
  return text;
}

/// Shortest form that reads back as the same float.
std::string Real(float value) {
  std::array<char, 32> buffer = {};
  // std::to_chars takes a pointer range by definition; there is no
  // iterator overload to reach for.
  const std::to_chars_result result = std::to_chars(  // NOLINT(*-pro-bounds-pointer-arithmetic)
      buffer.data(),
      buffer.data() + buffer.size(),
      value);
  if (result.ec != std::errc()) {
    return "0";
  }
  return {buffer.data(), result.ptr};
}

/// One line of the sheet, built column by column. In header mode every
/// value is ignored and the name is printed instead.
class ColumnWriter {
 public:
  explicit ColumnWriter(bool header) : header_(header) {}

  void Integer(std::string_view name, std::uint64_t value) {
    Begin(name);
    if (!header_) {
      line_ += std::to_string(value);
    }
  }

  void Number(std::string_view name, float value) {
    Begin(name);
    if (!header_) {
      line_ += Real(value);
    }
  }

  /// A mass: the name gains the "_kg" suffix, the value is printed in
  /// kilograms. One call, so the unit and the conversion cannot part ways.
  void Mass(std::string_view name, Grams value) {
    Begin(name, kKilogramSuffix);
    if (!header_) {
      line_ += Kilograms(value);
    }
  }

  std::string Take() {
    line_ += '\n';
    return std::move(line_);
  }

 private:
  void Begin(std::string_view name, const char* suffix = nullptr) {
    if (!first_) {
      line_ += ',';
    }
    first_ = false;
    if (header_) {
      line_ += name;
      if (suffix != nullptr) {
        line_ += suffix;
      }
    }
  }

  bool header_;

  std::string line_;

  bool first_ = true;
};

/// The keys of one definition table, in DefId order; empty when the table
/// is absent — which drops its whole column group from header and row
/// alike, since both walk this same list.
std::vector<std::string> TableKeys(const ITableSet& tables, const char* name) {
  std::vector<std::string> keys;
  const ITable* table = tables.FindTable(name);
  if (table == nullptr) {
    return keys;
  }
  const std::uint32_t key_column = table->FindColumn(kKeyColumn);
  if (key_column == kNoTableColumn) {
    return keys;
  }
  keys.reserve(table->RowCount());
  for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
    keys.emplace_back(table->CellText(row, key_column));
  }
  return keys;
}

Grams AmountAt(const ResourceAmounts& amounts, std::size_t index) {
  return index < amounts.size() ? amounts[index] : 0;
}

/// What lies in every unit's store right now, dense by ResourceId.
ResourceAmounts VillageStores(const WorldState& state, std::size_t width) {
  ResourceAmounts total(width, 0);
  for (const UnitRow& unit : state.units.rows) {
    for (std::size_t index = 0; index < width; ++index) {
      total[index] += AmountAt(unit.stock, index);
    }
  }
  return total;
}

ResourceAmounts VillagePantries(const WorldState& state, std::size_t width) {
  ResourceAmounts total(width, 0);
  for (const FamilyRow& family : state.families.rows) {
    for (std::size_t index = 0; index < width; ++index) {
      total[index] += AmountAt(family.pantry, index);
    }
  }
  return total;
}

/// Area-weighted mean fertility over every field; 0 with no land.
float MeanFertility(const WorldState& state) {
  float area = 0.0F;
  float weighted = 0.0F;
  for (const FieldRow& field : state.fields.rows) {
    if (field.kind != LandKind::kArable) {
      continue;  // a meadow has no fertility to average in (land_state.h)
    }
    area += field.area_ga;
    weighted += field.fertility * field.area_ga;
  }
  return area > 0.0F ? weighted / area : 0.0F;
}

/// Heads by rung for one livestock kind, kolkhoz herds and yard herds apart.
struct KindHeads {
  std::uint32_t adult = 0;
  std::uint32_t male = 0;
  std::uint32_t juvenile = 0;
  std::uint32_t newborn = 0;
  std::uint32_t billeted = 0;
  std::uint32_t yard = 0;
};

std::vector<KindHeads> CountHeads(const WorldState& state, std::size_t kinds) {
  std::vector<KindHeads> counts(kinds);
  for (const HerdRow& herd : state.herds.rows) {
    if (herd.kind.value >= kinds) {
      continue;
    }
    KindHeads& kind = counts[herd.kind.value];
    if (herd.household_owned != 0) {
      kind.yard +=
          static_cast<std::uint32_t>(herd.newborn_count) + herd.juvenile_count + herd.adult_count;
      continue;
    }
    kind.adult += herd.adult_count;
    kind.male += herd.adult_male_count;
    kind.juvenile += herd.juvenile_count;
    kind.newborn += herd.newborn_count;
    kind.billeted += herd.billeted_count;
  }
  return counts;
}

/// Emits one block of per-resource masses: `<prefix>_<key>_kg` for every
/// row of resources.csv, in table order.
void EmitResourceBlock(ColumnWriter& out,
                       const std::vector<std::string>& keys,
                       std::string_view prefix,
                       const ResourceAmounts& amounts) {
  for (std::size_t index = 0; index < keys.size(); ++index) {
    out.Mass(std::string(prefix) + "_" + keys[index], AmountAt(amounts, index));
  }
}

/// THE COLUMN LIST. Everything the sheet has, in order, once.
void EmitSheet(ColumnWriter& out, const WorldState& state, const ITableSet& tables) {
  const YearLedger& book = state.ledger.closed;
  const std::vector<std::string> resources = TableKeys(tables, "resources");
  const std::vector<std::string> livestock = TableKeys(tables, "livestock");

  out.Integer("year", book.year);

  // -- the state at the turn ----------------------------------------------
  out.Integer("population", state.residents.rows.size());
  out.Integer("families", state.families.rows.size());
  out.Integer("epoch", static_cast<std::uint64_t>(state.epoch));
  out.Number("life_expectancy_years", state.vitals.life_expectancy_years);
  out.Number("fertility_mean", MeanFertility(state));

  // -- people --------------------------------------------------------------
  out.Integer("births", book.births);
  out.Integer("deaths", book.deaths);
  out.Integer("arrivals", book.arrivals);
  out.Integer("departures", book.departures);
  out.Integer("weddings", book.weddings);

  // -- satiety -------------------------------------------------------------
  const float satiety_mean = book.satiety_days > 0
                                 ? book.satiety_day_mean_sum / static_cast<float>(book.satiety_days)
                                 : 0.0F;
  out.Number("satiety_mean", satiety_mean);
  // The leanest day, and 0 rather than the sentinel for a year nobody lived.
  out.Number("satiety_day_min", book.satiety_days > 0 ? book.satiety_day_mean_min : 0.0F);
  out.Integer("hungry_at_once_max", book.hungry_at_once_max);

  // -- land ----------------------------------------------------------------
  out.Number("area_sown_ha", book.area_sown_ha);
  out.Number("area_harvested_ha", book.area_harvested_ha);
  out.Number("area_lost_ha", book.area_lost_ha);
  out.Number("area_manured_ha", book.area_manured_ha);
  out.Mass("manure_plowed_in", book.manure_plowed_in);

  // -- herds, totals -------------------------------------------------------
  out.Integer("herd_births", book.herd_births);
  out.Integer("herd_deaths_age", book.herd_deaths_age);
  out.Integer("herd_deaths_hunger", book.herd_deaths_hunger);
  out.Integer("herd_culled", book.herd_culled);
  out.Number("herd_hungry_head_days", book.herd_hungry_head_days);

  // -- labor ---------------------------------------------------------------
  for (std::size_t kind = 1; kind < kWorkKindNames.size(); ++kind) {
    out.Number(std::string("work_") + kWorkKindNames[kind] + "_days", book.work_days_by_kind[kind]);
  }
  // Trudodni are stored in hundredths and reported whole: the sheet speaks
  // the design's unit, not the state's storage.
  out.Number("trudodni_accrued",
             static_cast<float>(book.trudodni_accrued) / static_cast<float>(kTrudodniScale));
  out.Number("trudodni_burned",
             static_cast<float>(book.trudodni_burned) / static_cast<float>(kTrudodniScale));
  out.Integer("walk_offs", book.walk_offs);

  // -- per resource --------------------------------------------------------
  EmitResourceBlock(out, resources, "harvest", book.harvest);
  EmitResourceBlock(out, resources, "seed", book.seed);
  EmitResourceBlock(out, resources, "delivered", book.delivered);
  EmitResourceBlock(out, resources, "herd_produce", book.herd_produce);
  EmitResourceBlock(out, resources, "feed", book.feed);
  EmitResourceBlock(out, resources, "issued", book.issued);
  EmitResourceBlock(out, resources, "ration", book.ration);
  EmitResourceBlock(out, resources, "nets", book.nets);
  EmitResourceBlock(out, resources, "yard_produce", book.yard_produce);
  EmitResourceBlock(out, resources, "plot_harvest", book.plot_harvest);
  EmitResourceBlock(out, resources, "eaten", book.eaten);
  EmitResourceBlock(out, resources, "store", VillageStores(state, resources.size()));
  EmitResourceBlock(out, resources, "pantry", VillagePantries(state, resources.size()));
  EmitResourceBlock(out, resources, "plan_due", state.plan.due);

  // -- per livestock kind --------------------------------------------------
  const std::vector<KindHeads> heads = CountHeads(state, livestock.size());
  for (std::size_t kind = 0; kind < livestock.size(); ++kind) {
    const std::string prefix = "herd_" + livestock[kind];
    out.Integer(prefix + "_adult", heads[kind].adult);
    out.Integer(prefix + "_male", heads[kind].male);
    out.Integer(prefix + "_juvenile", heads[kind].juvenile);
    out.Integer(prefix + "_newborn", heads[kind].newborn);
    out.Integer(prefix + "_billeted", heads[kind].billeted);
    out.Integer("yard_" + livestock[kind] + "_head", heads[kind].yard);
  }
}

}  // namespace

std::string LedgerCsvHeader(const ITableSet& tables) {
  ColumnWriter writer(true);
  // The state is never read in header mode; a default world is the cheapest
  // way to walk the one column list without duplicating it.
  const WorldState unused;
  EmitSheet(writer, unused, tables);
  return writer.Take();
}

std::string LedgerCsvRow(const WorldState& state, const ITableSet& tables) {
  // A row taken before any book has closed would be a year of zeros posing
  // as a year: the driver's job is to wait for ledger.closed.year to move.
  assert(state.ledger.closed.year > 0);
  ColumnWriter writer(false);
  EmitSheet(writer, state, tables);
  return writer.Take();
}

}  // namespace core
