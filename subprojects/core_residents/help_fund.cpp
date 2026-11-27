// #2199 «фонд помощи» (help_fund.h).

#include "help_fund.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/help_fund_state.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "family_exchange.h"
#include "food_config.h"

namespace core {
namespace {

constexpr double kPerMille = 1000.0;

void AddTo(ResourceAmounts& amounts, ResourceId resource, Grams grams) {
  if (amounts.size() <= resource.value) {
    amounts.resize(static_cast<std::size_t>(resource.value) + 1U, 0);
  }
  amounts[resource.value] += grams;
}

Grams HeldOf(const HelpFundState& fund, ResourceId resource) {
  return resource.value < fund.held.size() ? fund.held[resource.value] : 0;
}

/// The share of the winter store the meal will eat this month (family_meal.cpp,
/// EatTheWinterStore: the store over the days left to its window's end, and nothing outside
/// the window): the month's days over the days left, from the month's first day; 0 outside.
double WinterStoreMonthShare(const FoodConfig& config, const WorldState& current) {
  const PlotConfig& plot = config.plot;
  const auto month = static_cast<std::uint32_t>(current.calendar.date.month);
  const std::uint32_t from = plot.dried_eaten_from_month;
  const std::uint32_t to = plot.dried_eaten_to_month;
  const bool inside = from <= to ? (month >= from && month <= to) : (month >= from || month <= to);
  if (!inside) {
    return 0.0;
  }
  const std::uint32_t months_after = (to + kMonthsPerYear - month) % kMonthsPerYear;
  const std::uint32_t days_left = (months_after + 1U) * kDaysPerMonth;
  return std::min(1.0, static_cast<double>(kDaysPerMonth) / static_cast<double>(days_left));
}

/// The fund set to the target of its share, and «empty» may be said again.
void SetToTarget(const FoodConfig& config, WorldState& current) {
  current.help_fund.held = HelpFundTarget(config, current);
  current.help_fund.empty_said = 0;
}

}  // namespace

ResourceAmounts HelpFundTarget(const FoodConfig& config, const WorldState& world) {
  ResourceAmounts target;
  if (world.calendar.day < kDaysPerYear) {
    return target;  // year 1: nothing closed yet - its hunger is the issue raise's
  }
  const double share = static_cast<double>(world.help_fund.share_permille) / kPerMille;
  for (const ResourceId food : config.help_fund_foods) {
    const Grams harvest = food.value < world.ledger.closed.harvest.size()
                              ? world.ledger.closed.harvest[food.value]
                              : 0;
    if (harvest > 0) {
      AddTo(target, food, static_cast<Grams>(static_cast<double>(harvest) * share));
    }
  }
  return target;
}

void TopUpHelpFund(const FoodConfig& config, WorldState& current) {
  const SimDay day = current.calendar.day;
  if (day == 0 || day % kDaysPerYear != 0 ||
      HourFromTick(current.calendar.tick) != kHelpFundRules.turn_hour) {
    return;
  }
  // TOPPED UP, NOT ADDED TO (econ): the leftover counts toward the new year's share.
  SetToTarget(config, current);
}

void IssueFromHelpFund(const FoodConfig& config,
                       const std::vector<Grams>& reserve,
                       WorldState& current) {
  const SimDay day = current.calendar.day;
  if (day == 0 || day % kDaysPerMonth != 0 || HourFromTick(current.calendar.tick) != 0) {
    return;
  }
  HelpFundState& fund = current.help_fund;
  Grams given_total = 0;
  bool short_left = false;
  // IN ID ORDER, as the ration: the fund shrinks family by family.
  for (const std::uint32_t row : RowsById(current.families)) {
    FamilyRow& family = current.families.rows[row];
    if (!(family.month_need_kcal > 0.0F)) {
      continue;
    }
    const double line_kcal =
        static_cast<double>(kHelpFundRules.line) * static_cast<double>(family.month_need_kcal);
    if (!(static_cast<double>(family.month_eaten_kcal) < line_kcal)) {
      continue;  // the month closed at the line or above: no help
    }
    // THE PANTRY FORM (econ's yes after the month-after probe): the coming month's pantry
    // topped to 0.9 of a month's need, COUNTING WHAT IT HOLDS. The first form gave last
    // month's gap, and 40 % of the families were short again the month after - their own
    // intake swings with the issue's periods, and a gap read backwards undershot (hf2199c).
    double pantry_kcal = 0.0;
    for (std::size_t food = 0; food < family.pantry.size() && food < config.resources.size();
         ++food) {
      // THE WINTER STORE COUNTS ONLY WHAT THE MEAL WILL TAKE OF IT THIS MONTH (econ's W, after
      // #2199b's term split: 15 % of the term was the dried store, eaten Dec-May at its pace and
      // not at all outside it - a family holding it read «full» and was short on day 1).
      const double share =
          food == config.dried_forage_resource.value ? WinterStoreMonthShare(config, current) : 1.0;
      pantry_kcal += static_cast<double>(family.pantry[food]) *
                     static_cast<double>(config.resources[food].kcal_per_gram) * share;
    }
    double gap_kcal = line_kcal - pantry_kcal;
    if (!(gap_kcal > 0.0)) {
      continue;  // the pantry holds a month at the line already
    }
    for (const ResourceId food : config.help_fund_foods) {
      if (!(gap_kcal > 0.0) || food.value >= config.resources.size()) {
        break;
      }
      const double kcal_per_gram = static_cast<double>(config.resources[food.value].kcal_per_gram);
      const Grams held = HeldOf(fund, food);
      if (!(kcal_per_gram > 0.0) || held <= 0) {
        continue;
      }
      // What lies in the stores above the other funds: the fund cannot give what is not there.
      const Grams there = FreeFoodStock(config, current, reserve, food);
      // Rounded, not truncated: 13 200 kcal at 3.3F a gram is 3 999.99 g, and the floor lost a
      // gram a family a month (hu2199b).
      const auto wanted = static_cast<Grams>(std::llround(gap_kcal / kcal_per_gram));
      const Grams issue = std::min({wanted, held, there});
      if (issue <= 0) {
        continue;
      }
      const Grams given = TakeFoodFromStores(config, current, food, issue);
      AddFoodToPantry(family, food, given);
      AddTo(current.ledger.current.help_issued, food, given);
      fund.held[food.value] -= given;
      gap_kcal -= static_cast<double>(given) * kcal_per_gram;
      given_total += given;
    }
    short_left = short_left || gap_kcal > 1.0;
  }
  if (given_total > 0) {
    SimEvent& issued = EmitEvent(current, EventKind::kHelpFundIssued, EventSeverity::kNotable);
    issued.amount = given_total;
  }
  Grams left = 0;
  for (const ResourceId food : config.help_fund_foods) {
    left += HeldOf(fund, food);
  }
  if (short_left && left <= 0 && fund.empty_said == 0 && day >= kDaysPerYear) {
    fund.empty_said = 1;
    EmitEvent(current, EventKind::kHelpFundEmpty, EventSeverity::kNotable);
  }
}

void ConsumeHelpFundOrders(const FoodConfig& config, WorldState& current) {
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending || order.kind != OrderKind::kSetHelpFundShare) {
      continue;
    }
    if (order.amount < 0 || order.amount > static_cast<Grams>(kHelpFundRules.max_permille)) {
      order.status = OrderStatus::kRefused;
      order.refusal = OrderRefusal::kNotEligible;
      continue;
    }
    current.help_fund.share_permille = static_cast<std::uint16_t>(order.amount);
    SetToTarget(config, current);
    order.status = OrderStatus::kDone;
    order.refusal = OrderRefusal::kNone;
  }
}

}  // namespace core
