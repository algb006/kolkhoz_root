// «Сахар — малая радость» (core_residents/sugar_issue.h; #1241).

#include "sugar_issue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "sport.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 8> kSugarIssueWorldParamKeys = {
    "sugar_kg_per_resident_year",
    "sugar_mood_with",
    "sugar_mood_with_child",
    "sugar_mood_without",
    "sugar_mood_without_child",
    "sugar_dry_share",
    "sugar_complaint_dry_months",
    "sugar_complaint_from_month"};

Grams PantryOf(const FamilyRow& family, ResourceId resource) {
  return resource.value < family.pantry.size() ? family.pantry[resource.value] : 0;
}

}  // namespace

std::span<const std::string_view> SugarIssueWorldParamKeys() {
  return kSugarIssueWorldParamKeys;
}

bool ParseSugarIssueConfig(const ITableSet& tables, SugarIssueConfig& config, std::string& error) {
  if (const ITable* const resources = tables.FindTable("resources")) {
    config.sugar = DefIdFromRow<ResourceIdTag>(resources->FindRowByKey("sugar"));
    config.honey = DefIdFromRow<ResourceIdTag>(resources->FindRowByKey("honey"));
  }
  if (const ITable* const world = tables.FindTable("world_params")) {
    if (!OptionalValue(*world,
                       "alcohol_adult_from_years",
                       Range{.low = 0.0F, .high = 100.0F},
                       config.child_under_years,
                       error)) {
      return false;
    }
    auto dry_months = static_cast<float>(config.complaint_dry_months);
    auto from_month = static_cast<float>(config.complaint_from_month);
    const Range mood{.low = -20.0F, .high = 20.0F};
    const std::array<ScalarKnob, kSugarIssueWorldParamKeys.size()> knobs = {{
        {.key = kSugarIssueWorldParamKeys[0],
         .value = &config.kg_per_resident_year,
         .range = {.low = 0.0F, .high = 100.0F}},
        {.key = kSugarIssueWorldParamKeys[1], .value = &config.mood_with, .range = mood},
        {.key = kSugarIssueWorldParamKeys[2], .value = &config.mood_with_child, .range = mood},
        {.key = kSugarIssueWorldParamKeys[3], .value = &config.mood_without, .range = mood},
        {.key = kSugarIssueWorldParamKeys[4], .value = &config.mood_without_child, .range = mood},
        {.key = kSugarIssueWorldParamKeys[5],
         .value = &config.dry_share,
         .range = {.low = 0.0F, .high = 1.0F}},
        {.key = kSugarIssueWorldParamKeys[6],
         .value = &dry_months,
         .range = {.low = 1.0F, .high = 24.0F}},
        {.key = kSugarIssueWorldParamKeys[7],
         .value = &from_month,
         .range = {.low = 1.0F, .high = 120.0F}},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    config.complaint_dry_months = static_cast<std::uint8_t>(dry_months);
    config.complaint_from_month = static_cast<std::uint8_t>(from_month);
  }
  return true;
}

float SugarMoodTerm(const SugarIssueConfig& config, float share, bool child) {
  const float linear = std::clamp((2.0F * share) - 1.0F, -1.0F, 1.0F);
  if (linear >= 0.0F) {
    return linear * (child ? config.mood_with_child : config.mood_with);
  }
  return -linear * (child ? config.mood_without_child : config.mood_without);
}

void IssueSugarMonth(const SugarIssueConfig& config, float life_speedup, WorldState& current) {
  const SimDay day = current.calendar.day;
  if (day % kDaysPerMonth != 0) {
    return;
  }
  // THE MONTH'S MOOD IS COMPOSED HERE EVERY MONTH, sugar or none: the
  // moonshine's +5 (#1253, alcoholism.cpp) lands on top of it after, and a
  // month that did not recompose would carry last month's +5 into this one.
  if (config.sugar.value == kInvalidDefIdValue) {
    for (ResidentRow& person : current.residents.rows) {
      person.mood = person.mood_base;
    }
    return;
  }
  // THE NEED, BY EATERS: a family's members this morning.
  const std::size_t families = current.families.rows.size();
  std::vector<std::uint32_t> members(families, 0);
  for (const ResidentRow& person : current.residents.rows) {
    const std::uint32_t row = FindRow(current.families, person.family);
    if (row != kNoRow) {
      ++members[row];
    }
  }
  const double per_resident_g = static_cast<double>(config.kg_per_resident_year) * 1000.0 /
                                static_cast<double>(kMonthsPerYear);
  std::vector<double> need(families, 0.0);
  std::vector<double> covered(families, 0.0);
  double short_total = 0.0;
  for (std::size_t row = 0; row < families; ++row) {
    need[row] = per_resident_g * members[row];
    // HONEY FIRST, off the family's own pantry: «Мёд считается за сахар».
    FamilyRow& family = current.families.rows[row];
    const Grams honey = PantryOf(family, config.honey);
    const auto from_honey = static_cast<Grams>(std::min(static_cast<double>(honey), need[row]));
    if (from_honey > 0) {
      family.pantry[config.honey.value] -= from_honey;
      covered[row] = static_cast<double>(from_honey);
    }
    short_total += need[row] - covered[row];
  }
  // THE STORES' SUGAR, unreserved, shared by what each family still lacks.
  double stores = 0.0;
  for (const UnitRow& unit : current.units.rows) {
    stores += static_cast<double>(UnreservedOf(unit, config.sugar));
  }
  const double share_of_short = short_total > 0.0 ? std::min(1.0, stores / short_total) : 0.0;
  auto to_take = static_cast<Grams>(std::floor(short_total * share_of_short));
  for (UnitRow& unit : current.units.rows) {
    if (to_take <= 0) {
      break;
    }
    const Grams free = UnreservedOf(unit, config.sugar);
    const Grams taken = std::min(free, to_take);
    if (taken > 0) {
      unit.stock[config.sugar.value] -= taken;
      to_take -= taken;
    }
  }
  // THE SHARE, THE MOOD AND THE DRY MONTHS.
  const std::uint32_t season = TalkSeasonOf(day) + 1U;
  const std::uint32_t game_month = (day / kDaysPerMonth) + 1U;
  std::vector<float> share(families, 0.0F);
  for (std::size_t row = 0; row < families; ++row) {
    FamilyRow& family = current.families.rows[row];
    const double got = covered[row] + ((need[row] - covered[row]) * share_of_short);
    share[row] = need[row] > 0.0 ? static_cast<float>(std::min(1.0, got / need[row])) : 1.0F;
    family.sugar_share = share[row];
    family.sugar_dry_months =
        share[row] < config.dry_share
            ? static_cast<std::uint8_t>(std::min<int>(family.sugar_dry_months + 1, 255))
            : 0U;
    if (family.sugar_dry_months >= config.complaint_dry_months &&
        game_month >= config.complaint_from_month &&
        current.chairman.sugar_complaint_season != season) {
      current.chairman.sugar_complaint_season = season;
      SimEvent& asked = EmitEvent(current, EventKind::kChildrenAskSweets, EventSeverity::kRoutine);
      asked.family = current.families.row_ids[row];
    }
  }
  for (ResidentRow& person : current.residents.rows) {
    const std::uint32_t row = FindRow(current.families, person.family);
    // A resident with no family has nobody's ration: a neutral month.
    const float family_share = row != kNoRow ? share[row] : 0.5F;
    const bool child =
        BiologicalAgeYears(life_speedup, person.birth_day, day) < config.child_under_years;
    person.mood = std::clamp(
        person.mood_base + SugarMoodTerm(config, family_share, child), kMetricMin, kMetricMax);
  }
}

}  // namespace core
