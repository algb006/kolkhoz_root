// The chairmen New Game offers (core_catalog/avatar_catalog.h).

#include "core_catalog/avatar_catalog.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_tables/tables.h"

namespace core {
namespace {

constexpr std::string_view kTable = "avatars";

/// A count is whole: 2.5 bargains is a typo, not a rule.
bool ReadCount(const ITable& table,
               std::uint32_t row,
               std::string_view column_name,
               float high,
               std::uint8_t& value,
               std::string& error) {
  float read = static_cast<float>(value);
  if (!CellOrDefault(table,
                     row,
                     table.FindColumn(column_name),
                     Range{.low = 0.0F, .high = high},
                     read,
                     read,
                     error)) {
    error = std::string(kTable) + " row " + std::to_string(row) + " " + std::string(column_name) +
            ": " + error;
    return false;
  }
  if (std::floor(read) != read) {
    error = std::string(kTable) + " row " + std::to_string(row) + " " + std::string(column_name) +
            ": a count must be whole";
    return false;
  }
  value = static_cast<std::uint8_t>(read);
  return true;
}

bool ReadFactor(const ITable& table,
                std::uint32_t row,
                std::string_view column_name,
                Range range,
                float& value,
                std::string& error) {
  if (!CellOrDefault(table, row, table.FindColumn(column_name), range, value, value, error)) {
    error = std::string(kTable) + " row " + std::to_string(row) + " " + std::string(column_name) +
            ": " + error;
    return false;
  }
  return true;
}

}  // namespace

bool ReadAvatars(const ITableSet& tables, std::vector<AvatarDef>& avatars, std::string& error) {
  avatars.clear();
  const ITable* const table = tables.FindTable(kTable);
  if (table == nullptr) {
    return true;
  }
  const std::uint32_t key_col = table->FindColumn("key");
  const std::uint32_t reputation_col = table->FindColumn("start_reputation");
  if (key_col == kNoTableColumn || reputation_col == kNoTableColumn) {
    error = "avatars: the key and start_reputation columns are required";
    return false;
  }
  avatars.reserve(table->RowCount());
  for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
    AvatarDef avatar;
    avatar.key = std::string(table->CellText(row, key_col));
    if (avatar.key.empty()) {
      error = "avatars row " + std::to_string(row) + ": an empty key";
      return false;
    }
    float closed = avatar.plan_trade_closed ? 1.0F : 0.0F;
    if (!RequiredCell(*table,
                      kTable,
                      "start_reputation",
                      row,
                      reputation_col,
                      Range{.low = 0.0F, .high = 100.0F},
                      avatar.start_reputation,
                      error) ||
        !ReadFactor(*table,
                    row,
                    "crime_modifier",
                    Range{.low = -100.0F, .high = 100.0F},
                    avatar.crime_modifier,
                    error) ||
        !ReadFactor(*table,
                    row,
                    "electrification_threshold_factor",
                    Range{.low = 0.1F, .high = 10.0F},
                    avatar.electrification_threshold_factor,
                    error) ||
        !ReadCount(*table,
                   row,
                   "free_plan_trades_per_epoch",
                   100.0F,
                   avatar.free_plan_trades_per_epoch,
                   error) ||
        !ReadFactor(
            *table, row, "plan_trade_closed", Range{.low = 0.0F, .high = 1.0F}, closed, error) ||
        !ReadCount(*table,
                   row,
                   "crop_swap_closed_bargains",
                   100.0F,
                   avatar.crop_swap_closed_bargains,
                   error) ||
        !ReadCount(*table,
                   row,
                   "day_off_working_closed_years",
                   100.0F,
                   avatar.day_off_working_closed_years,
                   error) ||
        !ReadCount(*table,
                   row,
                   "free_cancelled_days_off_per_year",
                   48.0F,
                   avatar.free_cancelled_days_off_per_year,
                   error) ||
        !ReadFactor(*table,
                    row,
                    "workshops_l1_labor_factor",
                    Range{.low = 0.0F, .high = 10.0F},
                    avatar.workshops_l1_labor_factor,
                    error)) {
      return false;
    }
    if (closed != 0.0F && closed != 1.0F) {
      error = "avatars row " + std::to_string(row) + " plan_trade_closed: 0 or 1";
      return false;
    }
    avatar.plan_trade_closed = closed == 1.0F;
    avatars.push_back(std::move(avatar));
  }
  return true;
}

const AvatarDef& AvatarOf(std::span<const AvatarDef> avatars, AvatarId avatar) {
  static const AvatarDef kNeutral;
  return avatar.value < avatars.size() ? avatars[avatar.value] : kNeutral;
}

std::uint8_t FreePlanTradesLeft(const AvatarDef& avatar,
                                const ChairmanState& chairman,
                                Epoch epoch) {
  const std::uint8_t used = chairman.free_plan_trades_epoch == static_cast<std::uint8_t>(epoch)
                                ? chairman.free_plan_trades_used
                                : 0;
  return used < avatar.free_plan_trades_per_epoch
             ? static_cast<std::uint8_t>(avatar.free_plan_trades_per_epoch - used)
             : 0;
}

std::uint8_t FreeDaysOffLeft(const AvatarDef& avatar,
                             const ChairmanState& chairman,
                             std::uint16_t year) {
  const std::uint8_t used = chairman.free_days_off_year == year ? chairman.free_days_off_used : 0;
  return used < avatar.free_cancelled_days_off_per_year
             ? static_cast<std::uint8_t>(avatar.free_cancelled_days_off_per_year - used)
             : 0;
}

AvatarId FindAvatar(std::span<const AvatarDef> avatars, std::string_view key) {
  for (std::size_t row = 0; row < avatars.size(); ++row) {
    if (avatars[row].key == key) {
      return AvatarId{static_cast<std::uint16_t>(row)};
    }
  }
  return AvatarId{};
}

}  // namespace core
