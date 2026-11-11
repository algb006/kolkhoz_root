/// @file
/// @brief THE SALT DOOR (#1630 (A), the contract; boss's order of 11 Nov 2026,
/// core-boss-1630-salt-ending [2]): what the village holds of salt and its
/// substitutes, how many families stand on each step of the salt hunger, and
/// the run that is «Без соли»'s condition - one read for the lamp, the office
/// and the host's ending script.
/// @threading SINGLE_THREADED
/// Plain data, read between steps on the sim thread.
///
/// THE DESIGN (health §4 «Солевой голод»; epochs §8 «Отдельный исход — «Без
/// соли»»; the human, 1 Oct 2026): salt is a food a person cannot live
/// without; salt, salted fish and salted meat close the norm, any of the
/// three. A family keeps a count of days without it, which runs back twice as
/// fast as it grew. Steps by that count (days, STUB econ): 1 «пресно» from 4,
/// 2 «слабость» from 8, 3 «болеют» from 12, 4 «край» from 20. The ending's
/// CONDITION: at least half the families on step 4 eight days running
/// (kSaltEndingDue, kEndingConditionEventKinds).
///
/// CONTRACT, NO PRODUCER YET: until #1630 (B) lands (the eater's need, the
/// monthly issue, the family's count - FamilyRow::salt_hunger_days, a save
/// bump of its own), every reader gets `measured` false and zeros, and says
/// «not measured», never «no salt hunger».

#ifndef CORE_COMMON_SALT_VIEW_H_
#define CORE_COMMON_SALT_VIEW_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "core_common/quantities.h"

namespace core {

/// The salt hunger's steps, health §4: 0 none, 1 «пресно», 2 «слабость»,
/// 3 «болеют», 4 «край».
inline constexpr std::size_t kSaltHungerSteps = 5;

/// @brief The village's salt today.
struct SaltView {
  /// False until the core models the need (#1630 (B)): every number below is
  /// then 0 and means «not measured». STUB marker for the readers.
  bool measured = false;

  /// What lies in the kolkhoz's stores and the yards' larders, grams: salt;
  /// the substitutes (salted fish + salted meat, each kilogram one eater-day's
  /// norm, health §4 «Числа»).
  Grams salt_held = 0;
  Grams substitutes_held = 0;

  /// The village's table at the norm: days the salt and substitutes held
  /// cover every eater, the lamp's «Соли на столе на N суток». 0 when none.
  float table_days_left = 0.0F;

  /// Families on each step of the salt hunger, by step 0..4; their sum is the
  /// village's families.
  std::array<std::uint32_t, kSaltHungerSteps> families_by_step{};

  /// Days running that at least half the families stand on step 4 - the
  /// ending's count; its condition is 8 (STUB econ), raised once as
  /// kSaltEndingDue. 0 while fewer than half are on it.
  std::uint32_t half_on_edge_days = 0;
};

}  // namespace core

#endif  // CORE_COMMON_SALT_VIEW_H_
