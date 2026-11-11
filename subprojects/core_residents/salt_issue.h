/// @file
/// @brief «Соль — обязательная еда» (#1630 (B); health §4 «Солевой голод»; the
///        human's word of 1 October 2026: «Соль делаем обязательным продуктом
///        питания, без соли люди не живут. Соль может заменить соленая рыба или
///        солонина»; «Дебафф … не немедленный, он копится»): the month's salt
///        to the yards by eaters, the day's norm eaten, the family's count of
///        days without salt and the step it stands on, with the step's price.
/// @threading SINGLE_THREADED
/// The residents' decisions sub-step, at the day boundary, after the family
/// exchange: IssueSaltMonth and RunSaltDay; SaltMoodMonth after the sugar's
/// month, which composes the month's mood from mood_base.
///
/// THE RULE (econ's numbers, salt-need-2026-10-01 and salt-ending-b-predictions
/// 2026-11-11; every number STUB econ, a world_params row each):
///   * the norm: salt_norm_g_per_eater_day a family member (125 g: 6 kg a year);
///   * on a month's first day the stores' unreserved salt goes to the pantries,
///     a month of each family's norm less what its pantry holds, shared by
///     need when short - BEFORE every other food (the sugar is issued after);
///   * every day a family eats its eaters' norm from its pantry's salt; what
///     is short a substitute closes - salted fish, then salted meat - at
///     salt_substitute_kg_per_eater_day an eater-day, eaten from the pantry;
///   * a day short of the norm adds 1 to FamilyRow::salt_hunger_days; a day
///     met takes salt_count_recovery off it, never below 0;
///   * the step: 1 «пресно» from salt_step1_days, 2 «слабость» from
///     salt_step2_days, 3 «болеют» from salt_step3_days (4 «край» is #1630
///     (C)'s). Its price: the month's mood salt_mood_step1 / salt_mood_step2
///     (step 3 keeps step 2's), labour's output factor from step 2
///     (core_labor, efficiency_salt_weak_factor), health -salt_health_loss_
///     step3 a day a member on step 3.
/// OFF (salt_need_on 0, the default with no row): nothing is issued or eaten,
/// the count and the step stay 0.

#ifndef CORE_RESIDENTS_SALT_ISSUE_H_
#define CORE_RESIDENTS_SALT_ISSUE_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core_common/ids.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace core {

struct SaltIssueConfig {
  bool on = false;                           ///< `salt_need_on` (0/1)
  float norm_g_per_eater_day = 125.0F;       ///< `salt_norm_g_per_eater_day`
  float substitute_kg_per_eater_day = 1.0F;  ///< `salt_substitute_kg_per_eater_day`
  float step1_days = 4.0F;                   ///< `salt_step1_days`
  float step2_days = 8.0F;                   ///< `salt_step2_days`
  float step3_days = 12.0F;                  ///< `salt_step3_days`
  float mood_step1 = -4.0F;                  ///< `salt_mood_step1`
  float mood_step2 = -8.0F;                  ///< `salt_mood_step2` (step 3 keeps it)
  float health_loss_step3 = 1.0F;            ///< `salt_health_loss_step3`, a day
  float count_recovery = 2.0F;               ///< `salt_count_recovery`, days off a met day
  ResourceId salt;                           ///< resources.csv `salt`
  ResourceId salted_fish;                    ///< resources.csv `salted_fish`
  ResourceId salted_meat;                    ///< resources.csv `salted_meat`
};

/// @brief The world_params keys this file reads, for the assembly's union.
std::span<const std::string_view> SaltIssueWorldParamKeys();

/// @brief Reads the rows; a missing one keeps its documented number.
bool ParseSaltIssueConfig(const ITableSet& tables, SaltIssueConfig& config, std::string& error);

/// @brief The step a count of days without salt puts a family on (0..3).
std::uint8_t SaltStepOf(const SaltIssueConfig& config, float days_without);

/// @brief On a month's first day (day % kDaysPerMonth == 0): the stores'
///        unreserved salt into the pantries, a month of each family's norm
///        less its pantry's salt, shared by need when short. Any other day,
///        or with the need off, nothing.
/// Side effects: units' salt stock, families' pantry salt, ledger `issued`.
void IssueSaltMonth(const SaltIssueConfig& config, WorldState& current);

/// @brief The day's norm eaten by every family (salt, then salted fish, then
///        salted meat from its pantry), the count and the step, and the step
///        3 health loss. Call once a day. Off: nothing.
/// Side effects: pantries, FamilyRow::salt_hunger_days / salt_step, the
///        members' health.
void RunSaltDay(const SaltIssueConfig& config, WorldState& current);

/// @brief On a month's first day, after the sugar's month composed the mood:
///        each resident's mood takes its family's step term. Off: nothing.
void SaltMoodMonth(const SaltIssueConfig& config, WorldState& current);

/// @brief A resident's mood term at the family's salt `step`.
float SaltMoodTerm(const SaltIssueConfig& config, std::uint8_t step);

}  // namespace core

#endif  // CORE_RESIDENTS_SALT_ISSUE_H_
