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
constexpr std::array<std::string_view, 13> kFoodTheftWorldParamKeys = {
    "crime_food_theft_on",
    "food_theft_from_alcoholism",
    "food_theft_kg_per_thief_month",
    "food_theft_reach_m",
    "food_theft_store_cap_share_month",
    "collusion_share_month",
    "collusion_keeper_alcoholism",
    "audits_per_year",
    "audit_min_recorded_t",
    "audit_remark_share",
    "audit_trial_share",
    "audit_remark_reputation",
    "write_off_reputation"};

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
    AddAmount(unit.book_gap, food, take);
    AddLedgerAmount(current.ledger.current.stolen, food, take);
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
         .value = &config.audit_min_recorded_t,
         .range = {.low = 0.0F, .high = 1000.0F}},
        {.key = kFoodTheftWorldParamKeys[9], .value = &config.audit_remark_share, .range = share},
        {.key = kFoodTheftWorldParamKeys[10], .value = &config.audit_trial_share, .range = share},
        {.key = kFoodTheftWorldParamKeys[11],
         .value = &config.audit_remark_reputation,
         .range = reputation},
        {.key = kFoodTheftWorldParamKeys[12],
         .value = &config.write_off_reputation,
         .range = reputation},
    }};
    if (!ReadKnobs(*world, "world_params", knobs, error)) {
      return false;
    }
    config.on = on >= 0.5F;
    config.audits_per_year = static_cast<std::uint32_t>(audits);
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
  const std::uint32_t halves = std::min<std::uint32_t>(config.audits_per_year, 2U);
  const std::uint32_t period = kDaysPerYear / halves;
  const std::uint32_t half = std::min(day_of_year / period, halves - 1U);
  AuditState& audit = current.audit;
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
  // EVERY POSITION OF A TONNE OR MORE BY THE BOOK, summed: the shortage the
  // district reads is the gap over the recorded stock it counted.
  const Grams min_recorded = GramsFromKilograms(config.audit_min_recorded_t * 1000.0F);
  Grams recorded = 0;
  Grams gap = 0;
  for (const UnitRow& unit : current.units.rows) {
    if (unit.level == 0 || unit.dead != 0) {
      continue;
    }
    for (const ResourceId food : config.food) {
      const Grams by_book = Recorded(unit, food);
      if (by_book >= min_recorded) {
        recorded += by_book;
        gap += AmountOf(unit.book_gap, food);
      }
    }
  }
  YearLedger& book = current.ledger.current;
  ++book.audits;
  const double share =
      recorded > 0 ? static_cast<double>(gap) / static_cast<double>(recorded) : 0.0;
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
    }
    return;
  }
  audit.remarked = 1;
  ++book.audit_remarks;
  MoveReputation(current, config.audit_remark_reputation);
  SimEvent& remark = EmitEvent(current, EventKind::kAuditRemark, EventSeverity::kNotable);
  remark.amount = amount;
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
        current.audit.remarked = 0;
        MoveReputation(current, config.write_off_reputation);
      }
    }
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }
}

}  // namespace core
