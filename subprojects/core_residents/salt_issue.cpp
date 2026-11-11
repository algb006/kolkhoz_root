// «Соль — обязательная еда» (core_residents/salt_issue.h; #1630 (B)).

#include "salt_issue.h"

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
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 10> kSaltIssueWorldParamKeys = {
    "salt_need_on",
    "salt_norm_g_per_eater_day",
    "salt_substitute_kg_per_eater_day",
    "salt_step1_days",
    "salt_step2_days",
    "salt_step3_days",
    "salt_mood_step1",
    "salt_mood_step2",
    "salt_health_loss_step3",
    "salt_count_recovery"};

Grams& PantrySlot(FamilyRow& family, ResourceId resource) {
  if (family.pantry.size() <= resource.value) {
    family.pantry.resize(static_cast<std::size_t>(resource.value) + 1U, 0);
  }
  return family.pantry[resource.value];
}

Grams PantryOf(const FamilyRow& family, ResourceId resource) {
  return resource.value != kInvalidDefIdValue && resource.value < family.pantry.size()
             ? family.pantry[resource.value]
             : 0;
}

/// Each family's members this morning, by family row.
std::vector<std::uint32_t> MembersByFamily(const WorldState& current) {
  std::vector<std::uint32_t> members(current.families.rows.size(), 0);
  for (const ResidentRow& person : current.residents.rows) {
    const std::uint32_t row = FindRow(current.families, person.family);
    if (row != kNoRow) {
      ++members[row];
    }
  }
  return members;
}

/// Eats up to `eater_days` of the norm off the pantry's `resource` at
/// `grams_a_day` an eater-day; returns the eater-days it closed.
double EatSubstitute(FamilyRow& family,
                     ResourceId resource,
                     double grams_a_day,
                     double eater_days) {
  const Grams held = PantryOf(family, resource);
  if (held <= 0 || eater_days <= 0.0 || !(grams_a_day > 0.0)) {
    return 0.0;
  }
  const double want = eater_days * grams_a_day;
  const auto eaten = static_cast<Grams>(std::min(static_cast<double>(held), std::ceil(want)));
  PantrySlot(family, resource) -= eaten;
  return std::min(eater_days, static_cast<double>(eaten) / grams_a_day);
}

}  // namespace

std::span<const std::string_view> SaltIssueWorldParamKeys() {
  return kSaltIssueWorldParamKeys;
}

bool ParseSaltIssueConfig(const ITableSet& tables, SaltIssueConfig& config, std::string& error) {
  if (const ITable* const resources = tables.FindTable("resources")) {
    config.salt = DefIdFromRow<ResourceIdTag>(resources->FindRowByKey("salt"));
    config.salted_fish = DefIdFromRow<ResourceIdTag>(resources->FindRowByKey("salted_fish"));
    config.salted_meat = DefIdFromRow<ResourceIdTag>(resources->FindRowByKey("salted_meat"));
  }
  if (const ITable* const world = tables.FindTable("world_params")) {
    float on = config.on ? 1.0F : 0.0F;
    const Range days{.low = 0.0F, .high = 1000.0F};
    const Range mood{.low = -50.0F, .high = 0.0F};
    const std::array<ScalarKnob, kSaltIssueWorldParamKeys.size()> knobs = {{
        {.key = kSaltIssueWorldParamKeys[0], .value = &on, .range = {.low = 0.0F, .high = 1.0F}},
        {.key = kSaltIssueWorldParamKeys[1],
         .value = &config.norm_g_per_eater_day,
         .range = {.low = 0.0F, .high = 1000.0F}},
        {.key = kSaltIssueWorldParamKeys[2],
         .value = &config.substitute_kg_per_eater_day,
         .range = {.low = 0.01F, .high = 100.0F}},
        {.key = kSaltIssueWorldParamKeys[3], .value = &config.step1_days, .range = days},
        {.key = kSaltIssueWorldParamKeys[4], .value = &config.step2_days, .range = days},
        {.key = kSaltIssueWorldParamKeys[5], .value = &config.step3_days, .range = days},
        {.key = kSaltIssueWorldParamKeys[6], .value = &config.mood_step1, .range = mood},
        {.key = kSaltIssueWorldParamKeys[7], .value = &config.mood_step2, .range = mood},
        {.key = kSaltIssueWorldParamKeys[8],
         .value = &config.health_loss_step3,
         .range = {.low = 0.0F, .high = 100.0F}},
        {.key = kSaltIssueWorldParamKeys[9],
         .value = &config.count_recovery,
         .range = {.low = 0.0F, .high = 100.0F}},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    config.on = on >= 0.5F;
    if (!(config.step1_days <= config.step2_days && config.step2_days <= config.step3_days)) {
      error = "world_params salt_step1_days <= salt_step2_days <= salt_step3_days does not hold";
      return false;
    }
  }
  return true;
}

std::uint8_t SaltStepOf(const SaltIssueConfig& config, float days_without) {
  if (days_without >= config.step3_days) {
    return 3U;
  }
  if (days_without >= config.step2_days) {
    return 2U;
  }
  return days_without >= config.step1_days ? 1U : 0U;
}

float SaltMoodTerm(const SaltIssueConfig& config, std::uint8_t step) {
  if (step >= 2U) {
    return config.mood_step2;
  }
  return step == 1U ? config.mood_step1 : 0.0F;
}

void IssueSaltMonth(const SaltIssueConfig& config, WorldState& current) {
  if (!config.on || config.salt.value == kInvalidDefIdValue ||
      current.calendar.day % kDaysPerMonth != 0) {
    return;
  }
  // THE NEED, BY EATERS: a month of each family's norm less what it holds.
  const std::vector<std::uint32_t> members = MembersByFamily(current);
  const double month_g = static_cast<double>(config.norm_g_per_eater_day) * kDaysPerMonth;
  std::vector<double> need(current.families.rows.size(), 0.0);
  double need_total = 0.0;
  for (std::size_t row = 0; row < need.size(); ++row) {
    const double held = static_cast<double>(PantryOf(current.families.rows[row], config.salt));
    need[row] = std::max(0.0, (month_g * members[row]) - held);
    need_total += need[row];
  }
  double stores = 0.0;
  for (const UnitRow& unit : current.units.rows) {
    stores += static_cast<double>(UnreservedOf(unit, config.salt));
  }
  if (need_total <= 0.0 || stores <= 0.0) {
    return;
  }
  // SHORT STORES SHARE BY NEED, as the sugar's do.
  const double share = std::min(1.0, stores / need_total);
  auto to_take = static_cast<Grams>(std::floor(need_total * share));
  Grams taken_total = 0;
  for (UnitRow& unit : current.units.rows) {
    if (to_take <= 0) {
      break;
    }
    const Grams taken = std::min(UnreservedOf(unit, config.salt), to_take);
    if (taken > 0) {
      unit.stock[config.salt.value] -= taken;
      to_take -= taken;
      taken_total += taken;
    }
  }
  Grams given_total = 0;
  for (std::size_t row = 0; row < need.size(); ++row) {
    const auto given = static_cast<Grams>(std::floor(need[row] * share));
    if (given > 0 && given_total + given <= taken_total) {
      PantrySlot(current.families.rows[row], config.salt) += given;
      given_total += given;
    }
  }
  // The rounding's crumbs go back to the first store that gave: nothing lost.
  if (taken_total > given_total) {
    for (UnitRow& unit : current.units.rows) {
      if (config.salt.value < unit.stock.size()) {
        unit.stock[config.salt.value] += taken_total - given_total;
        break;
      }
    }
  }
  AddLedgerAmount(current.ledger.current.issued, config.salt, given_total);
}

void RunSaltDay(const SaltIssueConfig& config, WorldState& current) {
  if (!config.on || config.salt.value == kInvalidDefIdValue) {
    return;
  }
  const std::vector<std::uint32_t> members = MembersByFamily(current);
  const auto norm = static_cast<double>(config.norm_g_per_eater_day);
  const double substitute_g = static_cast<double>(config.substitute_kg_per_eater_day) * 1000.0;
  for (std::size_t row = 0; row < current.families.rows.size(); ++row) {
    FamilyRow& family = current.families.rows[row];
    if (members[row] == 0) {
      continue;
    }
    const double need = norm * members[row];
    const auto from_salt = static_cast<Grams>(
        std::min(static_cast<double>(PantryOf(family, config.salt)), std::ceil(need)));
    if (from_salt > 0) {
      PantrySlot(family, config.salt) -= from_salt;
    }
    // WHAT THE SALT LEFT SHORT, in eater-days, the substitutes close.
    double short_days = norm > 0.0 ? (need - static_cast<double>(from_salt)) / norm : 0.0;
    short_days -= EatSubstitute(family, config.salted_fish, substitute_g, short_days);
    short_days -= EatSubstitute(family, config.salted_meat, substitute_g, short_days);
    // A DAY IS MET WHEN EVERY EATER HAD THE NORM (a crumb under one gram of
    // the whole family's need does not count against it).
    const bool met = short_days * norm < 1.0;
    family.salt_hunger_days = met ? std::max(0.0F, family.salt_hunger_days - config.count_recovery)
                                  : family.salt_hunger_days + 1.0F;
    family.salt_step = SaltStepOf(config, family.salt_hunger_days);
  }
  // STEP 3 «болеют»: the health falls a day, every member (health §4: the
  // first delivery has no illness mechanic; the step rests on health only).
  if (!(config.health_loss_step3 > 0.0F)) {
    return;
  }
  for (ResidentRow& person : current.residents.rows) {
    const std::uint32_t row = FindRow(current.families, person.family);
    if (row != kNoRow && current.families.rows[row].salt_step >= 3U) {
      person.health = std::clamp(person.health - config.health_loss_step3, kMetricMin, kMetricMax);
    }
  }
}

void SaltMoodMonth(const SaltIssueConfig& config, WorldState& current) {
  if (!config.on || current.calendar.day % kDaysPerMonth != 0) {
    return;
  }
  for (ResidentRow& person : current.residents.rows) {
    const std::uint32_t row = FindRow(current.families, person.family);
    if (row == kNoRow) {
      continue;
    }
    const float term = SaltMoodTerm(config, current.families.rows[row].salt_step);
    if (term != 0.0F) {
      person.mood = std::clamp(person.mood + term, kMetricMin, kMetricMax);
    }
  }
}

}  // namespace core
