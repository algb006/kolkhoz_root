// The chairman New Game chose, set on the start world (core_world/world.h,
// ApplyStartAvatar; #634).
//
// @threading SINGLE_THREADED
// Called once from CreateStandardSimulation, before the first step.

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace core {
namespace {

constexpr std::string_view kAvatarsTable = "avatars";

void Refuse(std::string* error, std::string sentence) {
  if (error != nullptr) {
    *error = std::move(sentence);
  }
}

/// A numeric cell in [low, high]; nullopt when blank, not a number or out of
/// range — a start value read past its range is a table error, not a clamp.
std::optional<float> CellIn(
    const ITable& table, std::uint32_t row, std::uint32_t column, float low, float high) {
  const std::optional<float> value = table.CellReal(row, column);
  if (!value.has_value() || !std::isfinite(*value) || *value < low || *value > high) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

bool ApplyStartAvatar(WorldState& world,
                      const ITableSet& tables,
                      std::string_view avatar_key,
                      std::string* error) {
  if (avatar_key.empty()) {
    return true;  // no avatar: the world every run and test starts in
  }
  const ITable* const table = tables.FindTable(kAvatarsTable);
  if (table == nullptr) {
    Refuse(error,
           "avatar '" + std::string(avatar_key) +
               "' was asked for and the table set carries no avatars.csv");
    return false;
  }
  const std::uint32_t key_col = table->FindColumn("key");
  const std::uint32_t reputation_col = table->FindColumn("start_reputation");
  const std::uint32_t crime_col = table->FindColumn("crime_modifier");
  const std::uint32_t electrification_col = table->FindColumn("electrification_threshold_factor");
  if (key_col == kNoTableColumn || reputation_col == kNoTableColumn ||
      crime_col == kNoTableColumn || electrification_col == kNoTableColumn) {
    Refuse(error,
           "avatars: a column of key, start_reputation, crime_modifier, "
           "electrification_threshold_factor is missing");
    return false;
  }
  for (std::uint32_t row = 0; row < table->RowCount(); ++row) {
    if (table->CellText(row, key_col) != avatar_key) {
      continue;
    }
    const std::optional<float> reputation = CellIn(*table, row, reputation_col, 0.0F, 100.0F);
    const std::optional<float> crime = CellIn(*table, row, crime_col, -100.0F, 100.0F);
    const std::optional<float> electrification =
        CellIn(*table, row, electrification_col, 0.1F, 10.0F);
    if (!reputation || !crime || !electrification) {
      Refuse(error,
             "avatars row '" + std::string(avatar_key) +
                 "': start_reputation 0..100, crime_modifier -100..100 and "
                 "electrification_threshold_factor 0.1..10 are required");
      return false;
    }
    world.chairman.avatar = AvatarId{static_cast<std::uint16_t>(row)};
    world.chairman.raikom_reputation = *reputation;
    world.chairman.crime_modifier = *crime;
    world.chairman.electrification_threshold_factor = *electrification;
    return true;
  }
  // A MISSPELT SEAM WORD IS REFUSED, NOT SERVED A DEFAULT: the design has no
  // default chairman (boss, core-boss-634-avatar-door [2] (c)).
  Refuse(error, "avatar '" + std::string(avatar_key) + "' is not a key of avatars.csv");
  return false;
}

}  // namespace core
