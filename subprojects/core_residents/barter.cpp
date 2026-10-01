// The yards' exchange at the barter counter (core_residents/barter.h).

#include "barter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/away_in_district.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/state_table_ops.h"
#include "core_tables/tables.h"
#include "family_exchange.h"
#include "family_meal.h"
#include "food_config.h"

namespace core {
namespace {

constexpr std::array<std::string_view, 9> kBarterWorldParamKeys = {
    "barter_surplus_keep_days",
    "barter_lack_share_of_need",
    "barter_take_days_ahead",
    "barter_hungry_days",
    "barter_fact_days_in_row",
    "barter_fact_yards_each_side",
    "barter_fact_share_of_village_need",
    "barter_hour",
    "barter_walk_limit_hours",
};

/// The last hour a settlement may stand in: 22 and 23 are the hours the
/// day's pantry flows are booked in (the header's note).
constexpr float kLastSettlementHour = 21.0F;

/// Past this a count of days is a typo: a hundred game years.
constexpr float kDaysMax = 100.0F * static_cast<float>(kDaysPerYear);

constexpr std::size_t kCategoryCount = static_cast<std::size_t>(FoodCategory::kCount);

/// One yard at the counter: what it would hand over and what it would take,
/// by resource, grams of the grain equivalent.
struct Yard {
  std::uint32_t family_row = 0;

  /// The yard's daily need, D.
  double need = 0.0;

  bool hungry = false;

  std::vector<double> offer;
  std::vector<double> claim;

  /// What the yard lacks, by category (the header's «takes»), before it is
  /// laid on the resources somebody offers.
  std::array<double, kCategoryCount> lack{};

  /// The category's stock in the pantry.
  std::array<double, kCategoryCount> held{};

  /// What of the offer goes bad before it can be eaten — rule (a) — and the
  /// longest shelf life among it: such a yard takes anything that keeps
  /// longer, to that value.
  double perishing = 0.0;
  double perishing_keeps = 0.0;
};

/// What the settlement moved, or would move.
struct Settlement {
  /// By yard and resource, grams of the grain equivalent.
  std::vector<std::vector<double>> gave;
  std::vector<std::vector<double>> took;

  /// Everything that changed hands, each gram once, on the giving side.
  double volume = 0.0;
};

/// Grams of the grain equivalent in a gram of the resource.
double EquivalentOf(const FoodConfig& food, std::uint32_t index) {
  const double reference = static_cast<double>(food.consumption.grain_reference_kcal_per_gram);
  return reference > 0.0 ? static_cast<double>(food.resources[index].kcal_per_gram) / reference
                         : 0.0;
}

/// The category's index, or kCategoryCount for what is not eaten.
std::size_t CategoryOf(const FoodConfig& food, std::uint32_t index) {
  const auto category = static_cast<std::size_t>(food.resources[index].category);
  return category < kCategoryCount && EquivalentOf(food, index) > 0.0 ? category : kCategoryCount;
}

/// The yards' daily needs by family row, grams of the grain equivalent.
std::vector<double> NeedsByFamily(const FoodConfig& food,
                                  float life_speedup,
                                  const WorldState& world) {
  std::vector<double> needs(world.families.rows.size(), 0.0);
  for (const ResidentRow& person : world.residents.rows) {
    if (AwayInDistrict(person, world.calendar.tick)) {
      continue;  // eats nothing from the yard's pantry (district_car.h)
    }
    const std::uint32_t row = FindRow(world.families, person.family);
    if (row == kNoRow) {
      continue;
    }
    const float age = BiologicalAgeYears(life_speedup, person.birth_day, world.calendar.day);
    needs[row] += static_cast<double>(DailyNeedKilograms(food.consumption, age, false)) *
                  static_cast<double>(kGramsPerKilogram);
  }
  return needs;
}

/// What a yard would give and what it lacks, read off its pantry (the
/// header's rule). The claims are laid on the resources afterwards, when the
/// village's offer is known.
Yard ReadYard(const BarterConfig& config,
              const FoodConfig& food,
              const FamilyRow& family,
              std::uint32_t family_row,
              double need) {
  const auto roster =
      static_cast<std::uint32_t>(std::min(family.pantry.size(), food.resources.size()));
  Yard yard;
  yard.family_row = family_row;
  yard.need = need;
  yard.offer.assign(food.resources.size(), 0.0);
  yard.claim.assign(food.resources.size(), 0.0);
  std::vector<double> stock(food.resources.size(), 0.0);
  double total = 0.0;
  for (std::uint32_t index = 0; index < roster; ++index) {
    const std::size_t category = CategoryOf(food, index);
    if (category == kCategoryCount || family.pantry[index] <= 0) {
      continue;
    }
    stock[index] = static_cast<double>(family.pantry[index]) * EquivalentOf(food, index);
    yard.held[category] += stock[index];
    total += stock[index];
  }
  yard.hungry = total < static_cast<double>(config.hungry_days) * need;
  // (a) What goes bad before the yard can eat it: above D x the shelf life.
  std::array<double, kCategoryCount> given{};
  for (std::uint32_t index = 0; index < roster; ++index) {
    const double keeps = static_cast<double>(KeepsDays(food, index));
    if (!(stock[index] > 0.0) || !std::isfinite(keeps)) {
      continue;
    }
    const double over = stock[index] - (need * keeps);
    if (over > 0.0) {
      yard.offer[index] = over;
      yard.perishing += over;
      yard.perishing_keeps = std::max(yard.perishing_keeps, keeps);
      given[CategoryOf(food, index)] += over;
    }
  }
  if (yard.hungry) {
    return yard;  // gives (a) only, and takes the richest on offer
  }
  // (b) The largest category, above the days the yard keeps of it.
  std::size_t largest = kCategoryCount;
  double largest_left = 0.0;
  for (std::size_t category = 0; category < kCategoryCount; ++category) {
    const double left = yard.held[category] - given[category];
    if (left > largest_left) {
      largest = category;
      largest_left = left;
    }
  }
  const double over = largest_left - (static_cast<double>(config.surplus_keep_days) * need);
  if (largest != kCategoryCount && over > 0.0) {
    for (std::uint32_t index = 0; index < roster; ++index) {
      if (CategoryOf(food, index) == largest) {
        yard.offer[index] += over * (stock[index] - yard.offer[index]) / largest_left;
      }
    }
    given[largest] += over;
  }
  // What it lacks: a category it holds less than a day's share of, and gives
  // nothing of.
  const double share = static_cast<double>(config.lack_share_of_need) * need;
  for (std::size_t category = 0; category < kCategoryCount; ++category) {
    if (!(given[category] > 0.0) && yard.held[category] < share) {
      yard.lack[category] =
          (static_cast<double>(config.take_days_ahead) * share) - yard.held[category];
    }
  }
  return yard;
}

/// Lays every yard's claims on the resources the OTHER yards offer.
void LayClaims(const BarterConfig& config,
               const FoodConfig& food,
               const std::vector<double>& supply,
               std::vector<Yard>& yards) {
  const auto roster = static_cast<std::uint32_t>(food.resources.size());
  for (Yard& yard : yards) {
    // What the others offer of a resource this yard may take: none of what
    // it offers itself.
    const auto others = [&](std::uint32_t index) {
      return yard.offer[index] > 0.0 ? 0.0 : supply[index];
    };
    double claimed = 0.0;
    if (yard.hungry) {
      // The richest in calories on offer, to the value of what it brings.
      std::uint32_t richest = roster;
      for (std::uint32_t index = 0; index < roster; ++index) {
        if (others(index) > 0.0 &&
            (richest == roster ||
             food.resources[index].kcal_per_gram > food.resources[richest].kcal_per_gram)) {
          richest = index;
        }
      }
      if (richest != roster) {
        yard.claim[richest] = yard.perishing;
      }
      continue;
    }
    const double share = static_cast<double>(config.lack_share_of_need) * yard.need;
    for (std::size_t category = 0; category < kCategoryCount; ++category) {
      if (!(yard.lack[category] > 0.0)) {
        continue;
      }
      double offered = 0.0;
      for (std::uint32_t index = 0; index < roster; ++index) {
        offered += CategoryOf(food, index) == category ? others(index) : 0.0;
      }
      if (!(offered > 0.0)) {
        continue;
      }
      for (std::uint32_t index = 0; index < roster; ++index) {
        if (CategoryOf(food, index) != category || !(others(index) > 0.0)) {
          continue;
        }
        // Of a perishable, no more than the yard eats before it goes bad.
        const double days = std::min(static_cast<double>(config.take_days_ahead),
                                     static_cast<double>(KeepsDays(food, index)));
        const double room = std::max(0.0, (days * share) - yard.held[category]);
        const double claim = std::min(yard.lack[category] * others(index) / offered, room);
        yard.claim[index] = claim;
        claimed += claim;
      }
    }
    // What it brings to save from going bad is worth anything that keeps
    // longer, beyond what it lacks.
    const double rest = yard.perishing - claimed;
    if (!(rest > 0.0)) {
      continue;
    }
    double keeping = 0.0;
    const auto keeps_longer = [&](std::uint32_t index) {
      return CategoryOf(food, index) != kCategoryCount && !(yard.claim[index] > 0.0) &&
             static_cast<double>(KeepsDays(food, index)) > yard.perishing_keeps &&
             others(index) > 0.0;
    };
    for (std::uint32_t index = 0; index < roster; ++index) {
      keeping += keeps_longer(index) ? others(index) : 0.0;
    }
    if (!(keeping > 0.0)) {
      continue;
    }
    for (std::uint32_t index = 0; index < roster; ++index) {
      if (keeps_longer(index)) {
        yard.claim[index] = rest * others(index) / keeping;
      }
    }
  }
}

/// THE SETTLEMENT, one calculation for all the yards given: every resource
/// offered is shared among those who claim it in proportion to their claims
/// (and every claim among the offers), and each PAIR of yards settles at the
/// smaller of what the two would pass each other — so a yard carries off
/// from every neighbour exactly what it handed him, and from the counter
/// exactly what it brought. No order of rows enters it.
///
/// STUB, named: the calculation is two-sided. A ring of three — the first
/// wants the second's, the second the third's, the third the first's — finds
/// no pair and does not exchange.
Settlement Settle(const BarterConfig& config, const FoodConfig& food, std::vector<Yard>& yards) {
  const auto roster = static_cast<std::uint32_t>(food.resources.size());
  std::vector<double> supply(roster, 0.0);
  for (const Yard& yard : yards) {
    for (std::uint32_t index = 0; index < roster; ++index) {
      supply[index] += yard.offer[index];
    }
  }
  LayClaims(config, food, supply, yards);
  std::vector<double> demand(roster, 0.0);
  for (const Yard& yard : yards) {
    for (std::uint32_t index = 0; index < roster; ++index) {
      demand[index] += yard.claim[index];
    }
  }
  Settlement settled;
  settled.gave.assign(yards.size(), std::vector<double>(roster, 0.0));
  settled.took.assign(yards.size(), std::vector<double>(roster, 0.0));
  // What `from` would pass `to` of a resource, before the pair is balanced.
  const auto pass = [&](const Yard& from, const Yard& to, std::uint32_t index) {
    const double larger = std::max(supply[index], demand[index]);
    return larger > 0.0 ? from.offer[index] * to.claim[index] / larger : 0.0;
  };
  for (std::size_t left = 0; left < yards.size(); ++left) {
    for (std::size_t right = left + 1; right < yards.size(); ++right) {
      double there = 0.0;
      double back = 0.0;
      for (std::uint32_t index = 0; index < roster; ++index) {
        there += pass(yards[left], yards[right], index);
        back += pass(yards[right], yards[left], index);
      }
      const double both = std::min(there, back);
      if (!(both > 0.0)) {
        continue;
      }
      for (std::uint32_t index = 0; index < roster; ++index) {
        const double out = pass(yards[left], yards[right], index) * both / there;
        const double in = pass(yards[right], yards[left], index) * both / back;
        settled.gave[left][index] += out;
        settled.took[right][index] += out;
        settled.gave[right][index] += in;
        settled.took[left][index] += in;
      }
      settled.volume += 2.0 * both;
    }
  }
  return settled;
}

std::uint16_t CountOf(std::size_t count) {
  return static_cast<std::uint16_t>(
      std::min<std::size_t>(count, std::numeric_limits<std::uint16_t>::max()));
}

}  // namespace

std::span<const std::string_view> BarterWorldParamKeys() {
  return kBarterWorldParamKeys;
}

bool ParseBarterConfig(const ITableSet& tables, BarterConfig& config, std::string& error) {
  if (const ITable* unit_types = tables.FindTable("unit_types")) {
    config.counter_type = DefIdFromRow<UnitTypeIdTag>(unit_types->FindRowByKey("barter_place"));
  }
  const ITable* const world = tables.FindTable("world_params");
  if (world == nullptr) {
    return true;
  }
  const Range days{.low = 0.0F, .high = kDaysMax};
  const Range share{.low = 0.0F, .high = 1.0F};
  auto fact_days = static_cast<float>(config.fact_days_in_row);
  auto fact_yards = static_cast<float>(config.fact_yards_each_side);
  auto hour = static_cast<float>(config.hour);
  const std::array<ScalarKnob, kBarterWorldParamKeys.size()> knobs = {{
      {.key = kBarterWorldParamKeys[0], .value = &config.surplus_keep_days, .range = days},
      {.key = kBarterWorldParamKeys[1], .value = &config.lack_share_of_need, .range = share},
      {.key = kBarterWorldParamKeys[2], .value = &config.take_days_ahead, .range = days},
      {.key = kBarterWorldParamKeys[3], .value = &config.hungry_days, .range = days},
      {.key = kBarterWorldParamKeys[4],
       .value = &fact_days,
       .range = {.low = 1.0F, .high = kDaysMax}},
      {.key = kBarterWorldParamKeys[5],
       .value = &fact_yards,
       .range = {.low = 0.0F,
                 .high = static_cast<float>(std::numeric_limits<std::uint16_t>::max())}},
      {.key = kBarterWorldParamKeys[6],
       .value = &config.fact_share_of_village_need,
       .range = share},
      {.key = kBarterWorldParamKeys[7],
       .value = &hour,
       .range = {.low = 0.0F, .high = kLastSettlementHour}},
      {.key = kBarterWorldParamKeys[8],
       .value = &config.walk_limit_hours,
       .range = {.low = 0.0F, .high = static_cast<float>(kTicksPerDay)}},
  }};
  if (!ReadKnobs(*world, "world_params", knobs, error)) {
    return false;
  }
  config.fact_days_in_row = static_cast<std::uint32_t>(fact_days);
  config.fact_yards_each_side = static_cast<std::uint32_t>(fact_yards);
  config.hour = static_cast<std::uint8_t>(hour);
  return true;
}

void RunBarterDryCount(const BarterConfig& config,
                       const FoodConfig& food,
                       float life_speedup,
                       WorldState& current) {
  BarterWatch& watch = current.barter;
  const std::vector<double> needs = NeedsByFamily(food, life_speedup, current);
  std::vector<Yard> yards;
  double village_need = 0.0;
  for (std::uint32_t row = 0; row < current.families.rows.size(); ++row) {
    if (!(needs[row] > 0.0)) {
      continue;  // nobody at the table: nothing to give for, nothing to take for
    }
    village_need += needs[row];
    yards.push_back(ReadYard(config, food, current.families.rows[row], row, needs[row]));
  }
  const Settlement settled = Settle(config, food, yards);
  const auto has_any = [](const std::vector<double>& amounts) {
    return std::ranges::any_of(amounts, [](double amount) { return amount > 0.0; });
  };
  const auto givers = static_cast<std::size_t>(
      std::ranges::count_if(yards, [&](const Yard& yard) { return has_any(yard.offer); }));
  const auto takers = static_cast<std::size_t>(
      std::ranges::count_if(yards, [&](const Yard& yard) { return has_any(yard.claim); }));
  watch.dry_givers = CountOf(givers);
  watch.dry_takers = CountOf(takers);
  watch.dry_equivalent = static_cast<Grams>(std::llround(settled.volume));
  const bool met =
      givers >= config.fact_yards_each_side && takers >= config.fact_yards_each_side &&
      village_need > 0.0 &&
      settled.volume >= static_cast<double>(config.fact_share_of_village_need) * village_need;
  watch.dry_days_in_row =
      met ? CountOf(static_cast<std::size_t>(watch.dry_days_in_row) + 1U) : std::uint16_t{0};
  if (watch.worth_starting_raised == 0 && watch.dry_days_in_row >= config.fact_days_in_row) {
    watch.worth_starting_raised = 1;
    EmitEvent(current, EventKind::kBarterWorthStarting, EventSeverity::kNotable).amount =
        watch.dry_equivalent;
  }
}

}  // namespace core
