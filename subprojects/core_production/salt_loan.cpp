// The salt's production half of «Без соли» (core_production/salt_loan.h; #1630 (C)).

#include "salt_loan.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "district_limit.h"

namespace core {
namespace {

/// health §4 «Числа»: a kilogram of salted fish or meat closes an eater-day.
constexpr double kSubstituteGramsAnEaterDay = 1000.0;

/// June-November, game months from 0: the second line's season (econ's
/// salt-ending-c §3, the 1 Oct autumn salting lamp folded in).
constexpr std::uint32_t kSaltingFirstMonth = 5;
constexpr std::uint32_t kSaltingLastMonth = 10;

std::uint32_t GameYearOf(const WorldState& world) {
  return (world.calendar.day / kDaysPerYear) + 1U;
}

/// The lamp's days to the first «пресно»: the table's days + step 1's, or 0
/// when a family stands on a step already.
std::int64_t DaysToComplaint(const ProductionConfig& config, const SaltView& view) {
  for (std::size_t step = 1; step < view.families_by_step.size(); ++step) {
    if (view.families_by_step[step] > 0) {
      return 0;
    }
  }
  return static_cast<std::int64_t>(std::floor(static_cast<double>(view.table_days_left))) +
         static_cast<std::int64_t>(config.farming.salt_step1_days);
}

bool LampLit(const ProductionConfig& config, const SaltView& view) {
  return view.measured && DaysToComplaint(config, view) <
                              static_cast<std::int64_t>(3.0F * config.farming.salt_step1_days);
}

}  // namespace

SaltView SaltViewOf(const ProductionConfig& config, const WorldState& world) {
  SaltView view;
  if (!config.farming.salt_need_on || config.salt_resource.value == kInvalidDefIdValue) {
    return view;
  }
  view.measured = true;
  const auto add = [](const ResourceAmounts& stock, ResourceId resource, Grams& into) {
    if (resource.value != kInvalidDefIdValue && resource.value < stock.size()) {
      into += stock[resource.value];
    }
  };
  for (const UnitRow& unit : world.units.rows) {
    add(unit.stock, config.salt_resource, view.salt_held);
    add(unit.stock, config.salted_fish_resource, view.substitutes_held);
    add(unit.stock, config.salted_meat_resource, view.substitutes_held);
  }
  for (const FamilyRow& family : world.families.rows) {
    add(family.pantry, config.salt_resource, view.salt_held);
    add(family.pantry, config.salted_fish_resource, view.substitutes_held);
    add(family.pantry, config.salted_meat_resource, view.substitutes_held);
    ++view.families_by_step[std::min<std::size_t>(family.salt_step, kSaltHungerSteps - 1U)];
  }
  const auto eaters = static_cast<double>(world.residents.rows.size());
  const auto norm = static_cast<double>(config.farming.salt_norm_g_per_eater_day);
  if (eaters > 0.0 && norm > 0.0) {
    const double eater_days =
        (static_cast<double>(view.salt_held) / norm) +
        (static_cast<double>(view.substitutes_held) / kSubstituteGramsAnEaterDay);
    view.table_days_left = static_cast<float>(eater_days / eaters);
  }
  view.half_on_edge_days = world.salt.half_on_edge_days;
  return view;
}

LimitLotId SaltLotOf(const ProductionConfig& config) {
  const std::size_t salt = config.salt_resource.value;
  if (config.salt_resource.value == kInvalidDefIdValue) {
    return LimitLotId{};
  }
  for (std::size_t lot = 0; lot < config.limit.lots.size(); ++lot) {
    const ResourceAmounts& goods = config.limit.lots[lot].goods;
    bool only_salt = salt < goods.size() && goods[salt] > 0;
    for (std::size_t resource = 0; resource < goods.size() && only_salt; ++resource) {
      only_salt = resource == salt || goods[resource] <= 0;
    }
    if (only_salt) {
      return LimitLotId{static_cast<std::uint16_t>(lot)};
    }
  }
  return LimitLotId{};
}

std::int32_t SaltLoanPoints(const ProductionConfig& config) {
  const LimitLotId lot = SaltLotOf(config);
  if (lot.value >= config.limit.lots.size() || config.limit.lots[lot.value].points <= 0) {
    return 0;
  }
  return static_cast<std::int32_t>(
      std::lround(static_cast<double>(config.limit.lots[lot.value].points) *
                  (1.0 + static_cast<double>(config.goods_loan_markup))));
}

std::uint32_t SaltLoanLots(const ProductionConfig& config, const WorldState& world) {
  const LimitLotId lot = SaltLotOf(config);
  if (lot.value >= config.limit.lots.size()) {
    return 0;
  }
  const ResourceAmounts& goods = config.limit.lots[lot.value].goods;
  const Grams lot_salt =
      config.salt_resource.value < goods.size() ? goods[config.salt_resource.value] : Grams{0};
  if (lot_salt <= 0) {
    return 0;
  }
  const double year_g = static_cast<double>(config.farming.salt_norm_g_per_eater_day) *
                        static_cast<double>(world.residents.rows.size()) * kDaysPerYear;
  const double lots = std::ceil(year_g / static_cast<double>(lot_salt));
  return static_cast<std::uint32_t>(std::max(1.0, lots));
}

void CollectSaltAlarms(const ProductionConfig& config,
                       const WorldState& world,
                       std::vector<Alarm>& alarms) {
  const SaltView view = SaltViewOf(config, world);
  if (!LampLit(config, view)) {
    return;
  }
  Alarm lamp;
  lamp.kind = AlarmKind::kSaltTableShort;
  lamp.resource = config.salt_resource;
  lamp.amount = view.salt_held;
  lamp.days_to_loss = DaysToLossOf(DaysToComplaint(config, view));
  // THE SECOND LINE, June-November: what the table's reserve holds back from
  // the barrels (the shops' «стол раньше бочки», processing_shops.cpp).
  const std::uint32_t month = static_cast<std::uint32_t>(world.calendar.date.month);
  if (month >= kSaltingFirstMonth && month <= kSaltingLastMonth) {
    const double reserve = static_cast<double>(config.farming.salt_norm_g_per_eater_day) *
                           static_cast<double>(world.residents.rows.size()) * kDaysPerMonth *
                           static_cast<double>(config.farming.salt_table_reserve_months);
    lamp.amount_more =
        static_cast<Grams>(std::llround(std::min(reserve, static_cast<double>(view.salt_held))));
  }
  alarms.push_back(lamp);
  // THE ELDER'S LINE: the lamp red, no loan this year, the lot not affordable.
  const LimitLotId lot = SaltLotOf(config);
  const bool red = DaysToComplaint(config, view) <=
                   static_cast<std::int64_t>(2.0F * config.farming.salt_step1_days);
  if (red && lot.value < config.limit.lots.size() &&
      world.limit.points < config.limit.lots[lot.value].points &&
      world.salt.loan_year != GameYearOf(world)) {
    Alarm line;
    line.kind = AlarmKind::kSaltLoanAdvice;
    line.resource = config.salt_resource;
    line.amount = static_cast<std::int64_t>(SaltLoanLots(config, world)) * SaltLoanPoints(config);
    line.lamp = 0;
    alarms.push_back(line);
  }
}

void RunSaltLetterDay(const ProductionConfig& config, WorldState& current) {
  const std::uint32_t year = GameYearOf(current);
  if (current.salt.letter_year == year || current.salt.loan_year == year) {
    return;
  }
  const LimitLotId lot = SaltLotOf(config);
  if (lot.value >= config.limit.lots.size() ||
      current.limit.points >= config.limit.lots[lot.value].points) {
    return;  // the lamp names the lot; the letter is for the one who cannot buy it
  }
  if (!LampLit(config, SaltViewOf(config, current))) {
    return;
  }
  current.salt.letter_year = static_cast<std::uint16_t>(year);
  SimEvent& letter = EmitEvent(current, EventKind::kSaltLoanOffered, EventSeverity::kNotable);
  letter.amount = SaltLoanLots(config, current);  // the lots the loan would bring
}

OrderRefusal TakeSaltLoan(const ProductionConfig& config,
                          WorldState& current,
                          const OrderRow& order) {
  const std::uint32_t year = GameYearOf(current);
  const LimitLotId lot = SaltLotOf(config);
  if (!config.farming.salt_need_on || lot.value >= config.limit.lots.size() ||
      current.salt.loan_year == year) {
    return OrderRefusal::kRuleForbids;
  }
  const LimitLotDef& def = config.limit.lots[lot.value];
  // THE DOOR AS FOR A BOUGHT LOT (room, store), the points not asked: the
  // loan is the lot without them.
  const OrderRefusal refusal = LimitLotRefusalToday(config, current, lot, def.points);
  if (refusal != OrderRefusal::kNone) {
    return refusal;
  }
  OrderRow carried = order;
  carried.lot = lot;
  // A YEAR OF THE TABLE (econ's (a)): the lots of every eater's year, one
  // cart each, on the district's day. NAMED: the door above asked the room
  // for one lot, not for all of them.
  const std::uint32_t lots = SaltLoanLots(config, current);
  const std::uint32_t arrive_day = LimitCartArriveDay(config, current, carried);
  for (std::uint32_t index = 0; index < lots; ++index) {
    LimitDeliveryRow cart;
    cart.lot = lot;
    cart.arrive_day = arrive_day;
    cart.goods = def.goods;
    AppendRow(current.limit_deliveries, cart);
  }
  current.salt.loan_year = static_cast<std::uint16_t>(year);
  current.salt.loan_points_owed += static_cast<std::int32_t>(lots) * SaltLoanPoints(config);
  ++current.ledger.current.salt_loans;
  return OrderRefusal::kNone;
}

void TakeSaltLoanPointsAtTurn(WorldState& current) {
  if (current.salt.loan_points_owed <= 0) {
    return;
  }
  current.limit.points = std::max(0, current.limit.points - current.salt.loan_points_owed);
  current.salt.loan_points_owed = 0;
}

}  // namespace core
