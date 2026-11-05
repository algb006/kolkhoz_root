#include "food_theft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/table_value.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/state_table_ops.h"
#include "core_tables/tables.h"
#include "night_trade.h"

namespace core {
namespace {

/// The world_params keys link 2 reads, in ParseFoodTheftConfig's order.
constexpr std::array<std::string_view, 15> kFoodTheftWorldParamKeys = {
    "crime_food_theft_on",
    "food_theft_from_alcoholism",
    "food_theft_kg_per_thief_month",
    "food_theft_reach_m",
    "food_theft_store_cap_share_month",
    "collusion_share_month",
    "collusion_keeper_alcoholism",
    "audits_per_year",
    "audit_book_opens_month",
    "audit_remark_share",
    "audit_trial_share",
    "audit_remark_reputation",
    "write_off_reputation",
    "audit_window_months",
    "audit_min_receipts_t"};

/// The food a thief takes for his table, in this order (crime §7: grain and
/// potato, not only sugar).
constexpr std::array<std::string_view, 5> kFoodKeys = {"rye", "wheat", "barley", "oat", "potato"};

/// The thief's own age (crime §6: «мужчины 16+»).
constexpr float kThiefFromYears = 16.0F;

/// The counter-hash salt of an audit's day: its own stream.
constexpr std::uint64_t kAuditDayStream = 0x4155444954ULL;  // "AUDIT"

/// Per mille.
constexpr double kPerMille = 1000.0;

/// The raikom's reputation's scale (world_state.h, ChairmanState).
constexpr float kReputationMax = 100.0F;

/// Adds `grams` (may be negative) to one resource of a dense vector, growing it.
void AddAmount(ResourceAmounts& amounts, ResourceId resource, Grams grams) {
  if (amounts.size() <= resource.value) {
    amounts.resize(static_cast<std::size_t>(resource.value) + 1U, 0);
  }
  amounts[resource.value] += grams;
}

/// The campaign's month of `day` (year x 12 + month), the book window's index.
std::uint32_t MonthTag(SimDay day) {
  return static_cast<std::uint32_t>(day) / kDaysPerMonth;
}

/// Brings `unit`'s window up to `month`: sized to the config's window, every
/// month it skipped cleared (they lie a window back), and the month marked.
void AdvanceWindow(const FoodTheftConfig& config, UnitRow& unit, std::uint32_t month) {
  const std::uint32_t length = std::max<std::uint32_t>(config.window_months, 1U);
  if (unit.book_window.size() != length) {
    unit.book_window.assign(length, BookMonth{});
    unit.book_window_month = 0;
  }
  if (unit.book_window_month == 0) {
    unit.book_window_month = month + 1U;
    return;
  }
  const std::uint32_t last = unit.book_window_month - 1U;
  if (month <= last) {
    return;
  }
  const std::uint32_t skipped = std::min(month - last, length);
  for (std::uint32_t step = 1; step <= skipped; ++step) {
    unit.book_window[(last + step) % length] = BookMonth{};
  }
  unit.book_window_month = month + 1U;
}

/// The window's month `month` of `unit`, advanced to it first.
BookMonth& WindowMonth(const FoodTheftConfig& config, UnitRow& unit, std::uint32_t month) {
  AdvanceWindow(config, unit, month);
  return unit.book_window[month % unit.book_window.size()];
}

/// The recorded stock of `resource` in a unit: the real stock plus the book's gap.
Grams Recorded(const UnitRow& unit, ResourceId resource) {
  return AmountOf(unit.stock, resource) + AmountOf(unit.book_gap, resource);
}

/// The food (link 2's kinds) a unit holds by the book.
Grams RecordedFood(const FoodTheftConfig& config, const UnitRow& unit) {
  Grams total = 0;
  for (const ResourceId food : config.food) {
    total += Recorded(unit, food);
  }
  return total;
}

/// Takes up to `grams` of link 2's food off `unit`'s real stock, kind by kind,
/// into its book's gap and the year's `stolen`, one kFoodStolen a kind.
/// @return The grams taken.
Grams TakeFood(const FoodTheftConfig& config,
               std::uint32_t unit_row,
               ResidentId thief,
               bool collusion,
               Grams grams,
               WorldState& current) {
  UnitRow& unit = current.units.rows[unit_row];
  Grams taken = 0;
  for (const ResourceId food : config.food) {
    if (taken >= grams) {
      break;
    }
    const Grams here = AmountOf(unit.stock, food);
    const Grams take = std::min(here, grams - taken);
    if (take <= 0) {
      continue;
    }
    AddAmount(unit.stock, food, -take);
    AddLedgerAmount(current.ledger.current.stolen, food, take);
    // BEFORE THE ACT IT IS THE FORMER MANAGEMENT'S (#1568 (a)): «до приёмки»,
    // in its own line and never in the book.
    if (current.audit.books_accepted == 0) {
      AddLedgerAmount(current.ledger.current.stolen_before_acceptance, food, take);
    } else {
      AddAmount(unit.book_gap, food, take);
      AddAmount(WindowMonth(config, unit, MonthTag(current.calendar.day)).lost, food, take);
    }
    SimEvent& stolen = EmitEvent(current, EventKind::kFoodStolen, EventSeverity::kNotable);
    stolen.resident = thief;
    stolen.unit = current.units.row_ids[unit_row];
    stolen.resource = food;
    stolen.amount = take;
    stolen.family = FamilyId{collusion ? 1U : 0U};
    taken += take;
  }
  return taken;
}

/// Moves the raikom's reputation by `delta`, within its scale, and books a
/// loss as the district's own door does (district_reputation.cpp,
/// BookReputationLoss) - the rule's second writer, said here.
void MoveReputation(WorldState& current, float delta) {
  const float before = current.chairman.raikom_reputation;
  current.chairman.raikom_reputation = std::clamp(before + delta, 0.0F, kReputationMax);
  const float lost = before - current.chairman.raikom_reputation;
  if (lost > 0.0F) {
    current.ledger.current.reputation_losses += lost;
  }
}

}  // namespace

std::span<const std::string_view> FoodTheftWorldParamKeys() {
  return kFoodTheftWorldParamKeys;
}

bool ParseFoodTheftConfig(const ITableSet& tables, FoodTheftConfig& config, std::string& error) {
  if (const ITable* const world = tables.FindTable("world_params")) {
    auto on = config.on ? 1.0F : 0.0F;
    auto audits = static_cast<float>(config.audits_per_year);
    auto opens = static_cast<float>(config.book_opens_month) + 1.0F;
    auto window = static_cast<float>(config.window_months);
    const Range share{.low = 0.0F, .high = 1.0F};
    const Range alcoholism{.low = 0.0F, .high = 60.0F};
    const Range reputation{.low = -100.0F, .high = 0.0F};
    const std::array<ScalarKnob, kFoodTheftWorldParamKeys.size()> knobs = {{
        {.key = kFoodTheftWorldParamKeys[0], .value = &on, .range = share},
        {.key = kFoodTheftWorldParamKeys[1],
         .value = &config.thief_from_alcoholism,
         .range = alcoholism},
        {.key = kFoodTheftWorldParamKeys[2],
         .value = &config.kg_per_thief_month,
         .range = {.low = 0.0F, .high = 1000.0F}},
        {.key = kFoodTheftWorldParamKeys[3],
         .value = &config.reach_m,
         .range = {.low = 0.0F, .high = 20000.0F}},
        {.key = kFoodTheftWorldParamKeys[4],
         .value = &config.store_cap_share_month,
         .range = share},
        {.key = kFoodTheftWorldParamKeys[5],
         .value = &config.collusion_share_month,
         .range = share},
        {.key = kFoodTheftWorldParamKeys[6],
         .value = &config.collusion_keeper_alcoholism,
         .range = alcoholism},
        {.key = kFoodTheftWorldParamKeys[7],
         .value = &audits,
         .range = {.low = 0.0F, .high = 2.0F}},
        {.key = kFoodTheftWorldParamKeys[8],
         .value = &opens,
         .range = {.low = 1.0F, .high = 12.0F}},
        {.key = kFoodTheftWorldParamKeys[9], .value = &config.audit_remark_share, .range = share},
        {.key = kFoodTheftWorldParamKeys[10], .value = &config.audit_trial_share, .range = share},
        {.key = kFoodTheftWorldParamKeys[11],
         .value = &config.audit_remark_reputation,
         .range = reputation},
        {.key = kFoodTheftWorldParamKeys[12],
         .value = &config.write_off_reputation,
         .range = reputation},
        {.key = kFoodTheftWorldParamKeys[13],
         .value = &window,
         .range = {.low = 1.0F, .high = 120.0F}},
        {.key = kFoodTheftWorldParamKeys[14],
         .value = &config.min_receipts_t,
         .range = {.low = 0.0F, .high = 1000.0F}},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    config.on = on >= 0.5F;
    config.audits_per_year = static_cast<std::uint32_t>(audits);
    config.book_opens_month = static_cast<std::uint32_t>(opens) - 1U;
    config.window_months = static_cast<std::uint32_t>(window);
  }
  config.food.clear();
  if (const ITable* const resources = tables.FindTable("resources")) {
    for (const std::string_view key : kFoodKeys) {
      const std::uint32_t row = resources->FindRowByKey(key);
      if (row != kNoTableRow) {
        config.food.push_back(ResourceId{static_cast<std::uint16_t>(row)});
      }
    }
  }
  return true;
}

void RunFoodTheft(const FoodTheftConfig& config,
                  const NightTradeConfig& night,
                  float life_speedup,
                  WorldState& current) {
  if (!config.on || config.food.empty()) {
    return;
  }
  const std::uint32_t hour = HourFromTick(current.calendar.tick);
  if (hour != night.hour_out || !IsMoonlitNight(night, current.calendar.day)) {
    return;
  }
  // THE MONTH'S CAP A STORE, read before anybody takes: its recorded food
  // times the share, so the night's first thief does not shrink the cap of
  // the second.
  std::vector<Grams> cap(current.units.rows.size(), 0);
  std::vector<bool> open(current.units.rows.size(), false);
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    const UnitRow& unit = current.units.rows[row];
    if (unit.level == 0 || unit.dead != 0) {
      continue;
    }
    const Grams recorded = RecordedFood(config, unit);
    if (recorded <= 0) {
      continue;
    }
    cap[row] = static_cast<Grams>(static_cast<double>(recorded) *
                                  static_cast<double>(config.store_cap_share_month));
    open[row] = !StoreLeakClosed(night, current, row);
  }
  // 2a: EACH DRINKER OF 40+ TO THE NEAREST OPEN STORE IN REACH.
  const Grams per_thief = GramsFromKilograms(config.kg_per_thief_month);
  for (std::uint32_t person_row = 0; person_row < current.residents.rows.size(); ++person_row) {
    const ResidentRow& person = current.residents.rows[person_row];
    if (person.sex != Sex::kMale || person.alcoholism < config.thief_from_alcoholism ||
        BiologicalAgeYears(life_speedup, person.birth_day, current.calendar.day) <
            kThiefFromYears) {
      continue;
    }
    Vec2 yard;
    if (!YardOf(current, person, yard)) {
      continue;
    }
    std::uint32_t nearest = kNoRow;
    float best = config.reach_m;
    for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
      if (!open[row] || cap[row] <= 0) {
        continue;
      }
      const Vec2 at = current.units.rows[row].position;
      const float distance = std::hypot(at.x - yard.x, at.y - yard.y);
      // At or within the reach; the nearer wins, and a tie keeps the lower row.
      if (distance <= best && (nearest == kNoRow || distance < best)) {
        best = distance;
        nearest = row;
      }
    }
    if (nearest == kNoRow) {
      continue;
    }
    const Grams taken = TakeFood(config,
                                 nearest,
                                 current.residents.row_ids[person_row],
                                 false,
                                 std::min(per_thief, cap[nearest]),
                                 current);
    cap[nearest] -= taken;
  }
  // 2b: THE DRINKING STOREKEEPER LETS OUT HIS SHARE, guarded or not.
  for (std::uint32_t person_row = 0; person_row < current.residents.rows.size(); ++person_row) {
    const ResidentRow& person = current.residents.rows[person_row];
    if (person.post.profession.value != night.storekeeper_post.value ||
        night.storekeeper_post.value == kInvalidDefIdValue ||
        person.alcoholism <= config.collusion_keeper_alcoholism) {
      continue;
    }
    const std::uint32_t store = FindRow(current.units, person.post.unit);
    if (store == kNoRow) {
      continue;
    }
    const Grams share =
        static_cast<Grams>(static_cast<double>(RecordedFood(config, current.units.rows[store])) *
                           static_cast<double>(config.collusion_share_month));
    if (share > 0) {
      TakeFood(config, store, current.residents.row_ids[person_row], true, share, current);
    }
  }
}

void RunStoreAudit(const FoodTheftConfig& config, WorldState& current) {
  if (!config.on || config.audits_per_year == 0 || HourFromTick(current.calendar.tick) != 0) {
    return;
  }
  const std::uint32_t day = current.calendar.day;
  const std::uint32_t day_of_year = day % kDaysPerYear;
  AuditState& audit = current.audit;
  // THE ACT OF ACCEPTANCE (#1568 (a)): on its month's first day of year 1 (or
  // the first day after it the link is on), every book is set to the real
  // stock - the gap and the window cleared - and the shortage is his from here.
  if (audit.books_accepted == 0) {
    if (day < config.book_opens_month * kDaysPerMonth) {
      return;
    }
    Grams recorded = 0;
    for (UnitRow& unit : current.units.rows) {
      unit.book_gap.clear();
      unit.book_window.clear();
      unit.book_window_month = 0;
      for (const ResourceId food : config.food) {
        recorded += unit.level > 0 && unit.dead == 0 ? AmountOf(unit.stock, food) : 0;
      }
    }
    audit.books_accepted = 1;
    SimEvent& act = EmitEvent(current, EventKind::kBooksAccepted, EventSeverity::kNotable);
    act.amount = recorded;
  }
  const std::uint32_t halves = std::min<std::uint32_t>(config.audits_per_year, 2U);
  const std::uint32_t period = kDaysPerYear / halves;
  const std::uint32_t half = std::min(day_of_year / period, halves - 1U);
  // THE HALF'S DAY, DRAWN AT ITS START: «внезапно» - a counter hash of the
  // seed and the half's first day, so no world draw moves.
  if (day_of_year % period == 0) {
    const float draw = CounterHashUnitFloat(current.world_seed, day, half, kAuditDayStream);
    audit.next_day[half] =
        day + std::min(static_cast<std::uint32_t>(draw * static_cast<float>(period)), period - 1U);
  }
  if (audit.next_day[half] != day) {
    return;
  }
  // EACH POSITION ON ITS OWN, OVER THE WINDOW (#1568 (b)): a store-kind's
  // unbooked losses of the last window_months over its receipts of the same
  // months; positions under min_receipts_t of receipts are not read. The
  // village's verdict is its worst position - a leak running now, not a
  // residue an emptied store keeps.
  const Grams min_receipts = GramsFromKilograms(config.min_receipts_t * 1000.0F);
  const std::uint32_t month = MonthTag(day);
  double share = 0.0;
  UnitId worst_unit;
  ResourceId worst_food;
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    UnitRow& unit = current.units.rows[row];
    if (unit.level == 0 || unit.dead != 0 || unit.book_window.empty()) {
      continue;
    }
    AdvanceWindow(config, unit, month);
    for (const ResourceId food : config.food) {
      Grams lost = 0;
      Grams received = 0;
      for (const BookMonth& slot : unit.book_window) {
        lost += AmountOf(slot.lost, food);
        received += AmountOf(slot.received, food);
      }
      if (received < min_receipts || received <= 0) {
        continue;
      }
      const double here = static_cast<double>(lost) / static_cast<double>(received);
      if (here > share) {
        share = here;
        worst_unit = current.units.row_ids[row];
        worst_food = food;
      }
    }
  }
  YearLedger& book = current.ledger.current;
  ++book.audits;
  audit.shortage_permille = static_cast<std::uint16_t>(std::min(share * kPerMille, kPerMille));
  const auto amount = static_cast<std::int64_t>(audit.shortage_permille);
  // IN WHOLE PER MILLE, both sides: the table's 0.2 is a float a hair over 0.2,
  // and a shortage of exactly a fifth would never reach it.
  const auto remark_pm = static_cast<std::int64_t>(
      std::llround(static_cast<double>(config.audit_remark_share) * kPerMille));
  const auto trial_pm = static_cast<std::int64_t>(
      std::llround(static_cast<double>(config.audit_trial_share) * kPerMille));
  if (amount < remark_pm) {
    SimEvent& clean = EmitEvent(current, EventKind::kAuditClean, EventSeverity::kRoutine);
    clean.amount = amount;
    return;
  }
  // THE TRIGGER ONLY AFTER A REMARK (spec §6): a first audit at the trial
  // share gives the remark - the shortage must be readable before it hangs
  // the chairman.
  if (amount >= trial_pm && audit.remarked != 0) {
    if (audit.trial_due == 0) {
      audit.trial_due = 1;
      SimEvent& trial = EmitEvent(current, EventKind::kAuditTrialDue, EventSeverity::kInterrupting);
      trial.amount = amount;
      trial.unit = worst_unit;
      trial.resource = worst_food;
    }
    return;
  }
  audit.remarked = 1;
  ++book.audit_remarks;
  MoveReputation(current, config.audit_remark_reputation);
  SimEvent& remark = EmitEvent(current, EventKind::kAuditRemark, EventSeverity::kNotable);
  remark.amount = amount;
  remark.unit = worst_unit;
  remark.resource = worst_food;
}

void RunBookReceipts(const FoodTheftConfig& config,
                     const WorldState& previous,
                     WorldState& current) {
  if (!config.on || current.audit.books_accepted == 0) {
    return;
  }
  const std::uint32_t month = MonthTag(current.calendar.day);
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    UnitRow& unit = current.units.rows[row];
    if (unit.level == 0 || unit.dead != 0) {
      continue;
    }
    const std::uint32_t before = FindRow(previous.units, current.units.row_ids[row]);
    const UnitRow* const was = before == kNoRow ? nullptr : &previous.units.rows[before];
    for (const ResourceId food : config.food) {
      const Grams now = AmountOf(unit.stock, food);
      const Grams then = was == nullptr ? 0 : AmountOf(was->stock, food);
      if (now > then) {
        AddAmount(WindowMonth(config, unit, month).received, food, now - then);
      }
    }
  }
}

void TakeWriteOffOrders(const FoodTheftConfig& config, WorldState& current) {
  for (OrderRow& order : current.orders.rows) {
    if (order.status != OrderStatus::kPending || order.kind != OrderKind::kWriteOffShortage) {
      continue;
    }
    OrderRefusal refusal = OrderRefusal::kNone;
    const std::uint32_t row = FindRow(current.units, order.unit);
    if (row == kNoRow) {
      refusal = OrderRefusal::kNoSuchSubject;
    } else {
      UnitRow& unit = current.units.rows[row];
      Grams gap = 0;
      for (const Grams grams : unit.book_gap) {
        gap += grams;
      }
      if (!config.on || gap <= 0) {
        refusal = OrderRefusal::kRuleForbids;
      } else {
        for (std::size_t resource = 0; resource < unit.book_gap.size(); ++resource) {
          AddLedgerAmount(current.ledger.current.written_off,
                          ResourceId{static_cast<std::uint16_t>(resource)},
                          unit.book_gap[resource]);
        }
        unit.book_gap.clear();
        for (BookMonth& slot : unit.book_window) {
          slot.lost.clear();  // the window's losses cleared (#1568 (b))
        }
        current.audit.remarked = 0;
        MoveReputation(current, config.write_off_reputation);
      }
    }
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }
}

}  // namespace core
