// The district's limit catalogue (core_catalog/limit_catalog.h).

#include "core_catalog/limit_catalog.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {
namespace {

/// The world_params.csv keys, in the order of the knob list in the parse.
/// The first kPointKnobCount are whole points and days; the rest are the MTS
/// column's (ReadMtsColumnKnobs).
constexpr std::array<std::string_view, 19> kLimitWorldParamKeys = {
    "limit_base_points_lagging",
    "limit_base_points_average",
    "limit_base_points_strong",
    "limit_base_points_leading",
    "limit_plan_met_points",
    "limit_overfulfil_points_per_percent",
    "limit_overfulfil_points_max",
    "limit_delivery_days",
    "limit_delivery_delay_days_max",
    "mts_column_ha_limit",
    "mts_column_ha_per_work_day",
    "mts_column_spring_from_month",
    "mts_column_spring_to_month",
    "mts_column_autumn_from_month",
    "mts_column_autumn_to_month",
    // WHAT THE DISTRICT PAYS FOR A HEAD HANDED BACK, as a share of what the
    // same lot COSTS to buy. A share and not a price of its own so that the
    // two numbers cannot drift apart: the design's rule is «сдают дешевле,
    // чем берут — иначе это была бы не сдача излишка, а способ печатать
    // баллы на обороте», and a share below one is that rule made
    // unbreakable by arithmetic rather than watched by a guard.
    "livestock_handover_newborn",
    "livestock_handover_young",
    "livestock_handover_adult",
    "livestock_handover_old"};

constexpr std::size_t kPointKnobCount = 9;

/// Where the four handover shares begin in the list above.
constexpr std::size_t kHandoverKnobFirst = 15;

/// Largest price, grant or day count a row may name. A thousand times the
/// dearest lot of the catalogue: past it the cell is a typo.
constexpr float kMostPoints = 100000.0F;

/// Largest head count a lot may name. The district sells a head at a time and
/// batches of chicks by the crate; a cell past this is a typo, and the ceiling
/// is here rather than in the balance because it is not a knob — nothing tunes
/// it, it only refuses nonsense.
constexpr std::int64_t kMostHeadInALot = 1000;

bool ParseKind(std::string_view text, LimitLotKind& kind) {
  // Index order is the enum's (limit_catalog.h, LimitLotKind).
  static constexpr std::array<std::string_view, 6> kKinds = {
      "goods", "livestock", "vehicle", "person", "choice", "service"};
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
              "' is none of goods, livestock, vehicle, person, choice, service";
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

/// What a livestock lot brings (tables/limit_lot_livestock.csv).
///
/// THE EMPTY `head_count` IS THE GOODS RULE AGAIN, and it is written out
/// rather than borrowed silently: a cell left blank is "not written yet", the
/// lot simply cannot be ordered, and the parse does NOT refuse — a catalogue
/// that failed to load over one unpriced batch of piglets would take the
/// whole district down with it. Only a cell that is there and is not a
/// positive whole number refuses.
bool ReadLivestockLots(const ITableSet& tables,
                       const ITable& stock,
                       const ITable& lots,
                       LimitCatalog& catalog,
                       std::string& error) {
  const ITable* const kinds = tables.FindTable("livestock");
  const std::uint32_t lot_column = stock.FindColumn("lot");
  const std::uint32_t kind_column = stock.FindColumn("livestock");
  const std::uint32_t head_column = stock.FindColumn("head_count");
  const std::uint32_t stage_column = stock.FindColumn("arrives_stage");
  const std::uint32_t sex_column = stock.FindColumn("sex_choice");
  if (kinds == nullptr || lot_column == kNoTableColumn || kind_column == kNoTableColumn ||
      head_column == kNoTableColumn || stage_column == kNoTableColumn ||
      sex_column == kNoTableColumn) {
    error =
        "limit_lot_livestock: needs livestock.csv and the columns lot, livestock, head_count, "
        "arrives_stage, sex_choice";
    return false;
  }
  for (std::uint32_t row = 0; row < stock.RowCount(); ++row) {
    const std::string prefix = "limit_lot_livestock: row " + std::to_string(row) + ": ";
    const std::uint32_t lot_row = lots.FindRowByKey(stock.CellText(row, lot_column));
    if (lot_row == kNoTableRow) {
      error = prefix + "lot '" + std::string(stock.CellText(row, lot_column)) +
              "' is not in limit_catalog";
      return false;
    }
    const std::uint32_t kind_row = kinds->FindRowByKey(stock.CellText(row, kind_column));
    if (kind_row == kNoTableRow) {
      error = prefix + "livestock '" + std::string(stock.CellText(row, kind_column)) +
              "' is not in livestock";
      return false;
    }
    LimitLotDef& lot = catalog.lots[lot_row];
    lot.livestock = DefIdFromRow<LivestockKindIdTag>(kind_row);
    const std::string_view stage = stock.CellText(row, stage_column);
    if (stage == "adult_start") {
      lot.arrives_stage = LivestockArrivalStage::kAdultStart;
    } else if (stage == "young") {
      lot.arrives_stage = LivestockArrivalStage::kYoung;
    } else {
      error = prefix + "arrives_stage '" + std::string(stage) +
              "' is neither 'adult_start' nor 'young'";
      return false;
    }
    const std::optional<std::int64_t> sex = stock.CellInteger(row, sex_column);
    if (!sex.has_value() || *sex < 0 || *sex > 1) {
      error = prefix + "sex_choice is not 0 or 1";
      return false;
    }
    lot.sex_choice = *sex == 1;
    if (stock.CellText(row, head_column).empty()) {
      continue;  // not written yet: the lot cannot be ordered, and that is all
    }
    const std::optional<std::int64_t> head = stock.CellInteger(row, head_column);
    if (!head.has_value() || *head < 1 || *head > kMostHeadInALot) {
      error = prefix + "head_count is not a whole number of head";
      return false;
    }
    lot.head_count = static_cast<std::uint16_t>(*head);
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

/// The column's hectares and its two windows. The table's months are human,
/// 1..12; the catalogue keeps them 0-based as Month counts them.
bool ReadMtsColumnKnobs(const ITable& world, LimitCatalog& catalog, std::string& error) {
  constexpr float kMostHectares = 14400.0F;  // the whole map (CLAUDE.md §9)
  const Range hectares{.low = 0.0F, .high = kMostHectares};
  const Range months{.low = 1.0F, .high = 12.0F};
  std::array<float, 4> month_values = {static_cast<float>(catalog.mts_spring_from_month) + 1.0F,
                                       static_cast<float>(catalog.mts_spring_to_month) + 1.0F,
                                       static_cast<float>(catalog.mts_autumn_from_month) + 1.0F,
                                       static_cast<float>(catalog.mts_autumn_to_month) + 1.0F};
  const std::array<ScalarKnob, 6> knobs = {
      ScalarKnob{.key = kLimitWorldParamKeys[kPointKnobCount],
                 .value = &catalog.mts_column_ha_limit,
                 .range = hectares},
      ScalarKnob{.key = kLimitWorldParamKeys[kPointKnobCount + 1],
                 .value = &catalog.mts_column_ha_per_work_day,
                 .range = hectares},
      ScalarKnob{.key = kLimitWorldParamKeys[kPointKnobCount + 2],
                 .value = month_values.data(),
                 .range = months},
      ScalarKnob{.key = kLimitWorldParamKeys[kPointKnobCount + 3],
                 .value = &month_values[1],
                 .range = months},
      ScalarKnob{.key = kLimitWorldParamKeys[kPointKnobCount + 4],
                 .value = &month_values[2],
                 .range = months},
      ScalarKnob{.key = kLimitWorldParamKeys[kPointKnobCount + 5],
                 .value = &month_values[3],
                 .range = months}};
  if (!ReadKnobs(world, "world_params", knobs, error)) {
    return false;
  }
  std::array<std::uint8_t, 4> zero_based{};
  for (std::size_t index = 0; index < month_values.size(); ++index) {
    std::int32_t whole = 0;
    if (!WholeKnob(month_values[index], whole)) {
      error = "world_params: " + std::string(kLimitWorldParamKeys[kPointKnobCount + 2 + index]) +
              " is not a whole month";
      return false;
    }
    zero_based[index] = static_cast<std::uint8_t>(whole - 1);
  }
  // A window that ends before it begins would never open, and a column bought
  // for it would take its points to a season that does not come.
  if (zero_based[0] > zero_based[1] || zero_based[2] > zero_based[3]) {
    error = "world_params: an MTS column's window ends before it begins";
    return false;
  }
  catalog.mts_spring_from_month = zero_based[0];
  catalog.mts_spring_to_month = zero_based[1];
  catalog.mts_autumn_from_month = zero_based[2];
  catalog.mts_autumn_to_month = zero_based[3];
  return true;
}

/// The four shares of the buying price the district pays for a head handed
/// back. THE RANGE IS THE RULE: the high end stops BELOW one, so a table
/// that tried to pay as much as it charges is refused at the parse and not
/// caught later by somebody noticing that points were being minted on the
/// turnaround (livestock design, «сдают дешевле, чем берут»). The low end is
/// zero, because a district that pays nothing for a kind is a balance
/// decision and not a broken table.
bool ReadHandoverKnobs(const ITable& world, LimitCatalog& catalog, std::string& error) {
  const Range share{.low = 0.0F, .high = 0.99F};
  const std::array<ScalarKnob, 4> knobs = {
      ScalarKnob{.key = kLimitWorldParamKeys[kHandoverKnobFirst],
                 .value = &catalog.handover_share_newborn,
                 .range = share},
      ScalarKnob{.key = kLimitWorldParamKeys[kHandoverKnobFirst + 1],
                 .value = &catalog.handover_share_young,
                 .range = share},
      ScalarKnob{.key = kLimitWorldParamKeys[kHandoverKnobFirst + 2],
                 .value = &catalog.handover_share_adult,
                 .range = share},
      ScalarKnob{.key = kLimitWorldParamKeys[kHandoverKnobFirst + 3],
                 .value = &catalog.handover_share_old,
                 .range = share}};
  return ReadKnobs(world, "world_params", knobs, error);
}

}  // namespace

std::span<const std::string_view> LimitWorldParamKeys() {
  return kLimitWorldParamKeys;
}

bool ParseLimitCatalog(const ITableSet& tables, LimitCatalog& catalog, std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    std::array<float, kPointKnobCount> values = {
        static_cast<float>(catalog.base_points[0]),
        static_cast<float>(catalog.base_points[1]),
        static_cast<float>(catalog.base_points[2]),
        static_cast<float>(catalog.base_points[3]),
        static_cast<float>(catalog.plan_met_points),
        static_cast<float>(catalog.overfulfil_points_per_percent),
        static_cast<float>(catalog.overfulfil_points_max),
        static_cast<float>(catalog.delivery_days),
        static_cast<float>(catalog.delivery_delay_days_max)};
    std::array<ScalarKnob, kPointKnobCount> knobs{};
    for (std::size_t index = 0; index < knobs.size(); ++index) {
      knobs[index] = ScalarKnob{.key = kLimitWorldParamKeys[index],
                                .value = &values[index],
                                .range = Range{.low = 0.0F, .high = kMostPoints}};
    }
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    std::array<std::int32_t, kPointKnobCount> whole{};
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
    if (!ReadMtsColumnKnobs(*world, catalog, error)) {
      return false;
    }
    if (!ReadHandoverKnobs(*world, catalog, error)) {
      return false;
    }
  }
  const ITable* const lots = tables.FindTable("limit_catalog");
  if (lots == nullptr) {
    return true;
  }
  if (!ReadLots(*lots, catalog, error)) {
    return false;
  }
  // The column's two lots by key, as the catalogue names them. A row that is
  // not a service would buy goods under the column's name: refused.
  catalog.mts_spring_lot = LimitLotId{};
  catalog.mts_autumn_lot = LimitLotId{};
  const std::array<std::pair<std::string_view, LimitLotId*>, 2> column_lots = {
      std::pair<std::string_view, LimitLotId*>{"mts_column_spring", &catalog.mts_spring_lot},
      std::pair<std::string_view, LimitLotId*>{"mts_column_autumn", &catalog.mts_autumn_lot}};
  for (const auto& [key, id] : column_lots) {
    const std::uint32_t row = lots->FindRowByKey(key);
    if (row == kNoTableRow) {
      continue;
    }
    if (catalog.lots[row].kind != LimitLotKind::kService) {
      error = "limit_catalog: " + std::string(key) + " is not a service";
      return false;
    }
    *id = DefIdFromRow<LimitLotIdTag>(row);
  }
  // THE STOCK BEFORE THE GOODS, so that a lot which is both — none is today,
  // and none should be — would end up refused by the goods rule rather than
  // silently half-read.
  if (const ITable* const stock = tables.FindTable("limit_lot_livestock")) {
    if (!ReadLivestockLots(tables, *stock, *lots, catalog, error)) {
      return false;
    }
  }
  if (const ITable* const goods = tables.FindTable("limit_lot_goods")) {
    return ReadGoods(tables, *goods, *lots, catalog, error);
  }
  return true;
}

}  // namespace core
