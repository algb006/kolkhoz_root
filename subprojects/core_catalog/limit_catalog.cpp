// The district's limit catalogue (core_catalog/limit_catalog.h).

#include "core_catalog/limit_catalog.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The world_params.csv keys, in the order of the knob list in the parse.
constexpr std::array<std::string_view, 9> kLimitWorldParamKeys = {
    "limit_base_points_lagging",
    "limit_base_points_average",
    "limit_base_points_strong",
    "limit_base_points_leading",
    "limit_plan_met_points",
    "limit_overfulfil_points_per_percent",
    "limit_overfulfil_points_max",
    "limit_delivery_days",
    "limit_delivery_delay_days_max"};

/// Largest price, grant or day count a row may name. A thousand times the
/// dearest lot of the catalogue: past it the cell is a typo.
constexpr float kMostPoints = 100000.0F;

bool ParseKind(std::string_view text, LimitLotKind& kind) {
  static constexpr std::array<std::string_view, 5> kKinds = {
      "goods", "livestock", "vehicle", "person", "choice"};
  for (std::size_t index = 0; index < kKinds.size(); ++index) {
    if (text == kKinds[index]) {
      kind = static_cast<LimitLotKind>(index);
      return true;
    }
  }
  return false;
}

bool ReadLots(const ITable& table, LimitCatalog& catalog, std::string& error) {
  const std::uint32_t points_column = table.FindColumn("points");
  const std::uint32_t era_column = table.FindColumn("era");
  const std::uint32_t kind_column = table.FindColumn("kind");
  if (points_column == kNoTableColumn || era_column == kNoTableColumn ||
      kind_column == kNoTableColumn) {
    error = "limit_catalog: a column of points, era, kind is missing";
    return false;
  }
  catalog.lots.assign(table.RowCount(), LimitLotDef{});
  for (std::uint32_t row = 0; row < table.RowCount(); ++row) {
    LimitLotDef& lot = catalog.lots[row];
    const std::string prefix = "limit_catalog: row " + std::to_string(row) + ": ";
    if (!ParseKind(table.CellText(row, kind_column), lot.kind)) {
      error = prefix + "kind '" + std::string(table.CellText(row, kind_column)) +
              "' is none of goods, livestock, vehicle, person, choice";
      return false;
    }
    // An EMPTY price is a lot not yet priced (insulation, paint): it stays
    // in the list so ids keep their rows, and it is not ordered.
    if (!table.CellText(row, points_column).empty()) {
      const std::optional<std::int64_t> points = table.CellInteger(row, points_column);
      if (!points.has_value() || *points <= 0 || *points > static_cast<std::int64_t>(kMostPoints)) {
        error = prefix + "points is not a whole number of points above zero";
        return false;
      }
      lot.points = static_cast<std::int32_t>(*points);
    }
    const std::optional<std::int64_t> era = table.CellInteger(row, era_column);
    if (!era.has_value() || *era < 1 || *era > 3) {
      error = prefix + "era is not 1, 2 or 3";
      return false;
    }
    lot.era = static_cast<std::uint8_t>(*era);
  }
  return true;
}

bool ReadGoods(const ITableSet& tables,
               const ITable& goods,
               const ITable& lots,
               LimitCatalog& catalog,
               std::string& error) {
  const ITable* const resources = tables.FindTable("resources");
  const std::uint32_t lot_column = goods.FindColumn("lot");
  const std::uint32_t resource_column = goods.FindColumn("resource");
  const std::uint32_t amount_column = goods.FindColumn("amount");
  if (resources == nullptr || lot_column == kNoTableColumn || resource_column == kNoTableColumn ||
      amount_column == kNoTableColumn) {
    error = "limit_lot_goods: needs resources.csv and the columns lot, resource, amount";
    return false;
  }
  const std::uint32_t mass_column = resources->FindColumn("kg_per_unit");
  for (std::uint32_t row = 0; row < goods.RowCount(); ++row) {
    const std::string prefix = "limit_lot_goods: row " + std::to_string(row) + ": ";
    const std::uint32_t lot_row = lots.FindRowByKey(goods.CellText(row, lot_column));
    if (lot_row == kNoTableRow) {
      error = prefix + "lot '" + std::string(goods.CellText(row, lot_column)) +
              "' is not in limit_catalog";
      return false;
    }
    const std::uint32_t resource_row =
        resources->FindRowByKey(goods.CellText(row, resource_column));
    if (resource_row == kNoTableRow) {
      error = prefix + "resource '" + std::string(goods.CellText(row, resource_column)) +
              "' is not in resources";
      return false;
    }
    // AN EMPTY AMOUNT IS "NOT WRITTEN YET" (boss, parcel 211): that resource
    // does not come with the lot. Only a cell that is there and is not a
    // positive number refuses.
    if (goods.CellText(row, amount_column).empty()) {
      continue;
    }
    float amount = 0.0F;
    float kilograms = 0.0F;
    if (!RequiredCell(goods,
                      "limit_lot_goods",
                      "amount",
                      row,
                      amount_column,
                      Range{.low = 1.0e-6F, .high = Range::kUnbounded},
                      amount,
                      error) ||
        !RequiredCell(*resources,
                      "resources",
                      "kg_per_unit",
                      resource_row,
                      mass_column,
                      Range{.low = 0.001F, .high = 100000.0F},
                      kilograms,
                      error)) {
      return false;
    }
    const double grams = static_cast<double>(amount) * static_cast<double>(kilograms) * 1000.0;
    if (!(grams >= 1.0) || grams > 9.0e15) {
      error = prefix + "amount × kg_per_unit is not a mass the core can count";
      return false;
    }
    ResourceAmounts& lot_goods = catalog.lots[lot_row].goods;
    if (lot_goods.size() <= resource_row) {
      lot_goods.resize(static_cast<std::size_t>(resource_row) + 1U, 0);
    }
    lot_goods[resource_row] = static_cast<Grams>(std::llround(grams));
  }
  return true;
}

bool WholeKnob(float value, std::int32_t& out) {
  if (std::floor(value) != value) {
    return false;
  }
  out = static_cast<std::int32_t>(value);
  return true;
}

}  // namespace

std::span<const std::string_view> LimitWorldParamKeys() {
  return kLimitWorldParamKeys;
}

bool ParseLimitCatalog(const ITableSet& tables, LimitCatalog& catalog, std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    std::array<float, kLimitWorldParamKeys.size()> values = {
        static_cast<float>(catalog.base_points[0]),
        static_cast<float>(catalog.base_points[1]),
        static_cast<float>(catalog.base_points[2]),
        static_cast<float>(catalog.base_points[3]),
        static_cast<float>(catalog.plan_met_points),
        static_cast<float>(catalog.overfulfil_points_per_percent),
        static_cast<float>(catalog.overfulfil_points_max),
        static_cast<float>(catalog.delivery_days),
        static_cast<float>(catalog.delivery_delay_days_max)};
    std::array<ScalarKnob, kLimitWorldParamKeys.size()> knobs{};
    for (std::size_t index = 0; index < knobs.size(); ++index) {
      knobs[index] = ScalarKnob{.key = kLimitWorldParamKeys[index],
                                .value = &values[index],
                                .range = Range{.low = 0.0F, .high = kMostPoints}};
    }
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    std::array<std::int32_t, kLimitWorldParamKeys.size()> whole{};
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (!WholeKnob(values[index], whole[index])) {
        error =
            "world_params: " + std::string(kLimitWorldParamKeys[index]) + " is not a whole number";
        return false;
      }
    }
    for (std::size_t tier = 0; tier < catalog.base_points.size(); ++tier) {
      catalog.base_points[tier] = whole[tier];
    }
    catalog.plan_met_points = whole[4];
    catalog.overfulfil_points_per_percent = whole[5];
    catalog.overfulfil_points_max = whole[6];
    catalog.delivery_days = static_cast<std::uint32_t>(whole[7]);
    catalog.delivery_delay_days_max = static_cast<std::uint32_t>(whole[8]);
  }
  const ITable* const lots = tables.FindTable("limit_catalog");
  if (lots == nullptr) {
    return true;
  }
  if (!ReadLots(*lots, catalog, error)) {
    return false;
  }
  if (const ITable* const goods = tables.FindTable("limit_lot_goods")) {
    return ReadGoods(tables, *goods, *lots, catalog, error);
  }
  return true;
}

}  // namespace core
