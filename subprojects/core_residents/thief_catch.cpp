// #2191 «застал вора» and the chairman's seven choices (thief_catch.h).

#include "thief_catch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "core_common/authority.h"
#include "core_common/calendar.h"
#include "core_common/district_car_state.h"
#include "core_common/emit_event.h"
#include "core_common/state_table_ops.h"
#include "core_common/thief_catch_state.h"
#include "night_trade.h"

namespace core {
namespace {

constexpr float kScaleMax = 100.0F;
constexpr float kAttitudeMax = 100.0F;
/// A labour day in the trudodni account's hundredths.
constexpr float kTrudodniHundredths = 100.0F;
/// The hour of the day a man handed to the constable is home again (the morning).
constexpr std::uint8_t kBackHour = 8;

/// One of the chairman's scales by `step`, with the catch-up below its edge; never falls.
void RaiseScale(const ThiefCatchRules& rules, Metric& scale, float step) {
  if (step <= 0.0F) {
    return;
  }
  const float factor = scale < rules.catch_up_below ? rules.catch_up_factor : 1.0F;
  scale = std::min(scale + (step * factor), kScaleMax);
}

/// Every member of `family`'s attitude to the chairman by `delta`, within 0..100.
void MoveFamilyAttitude(WorldState& current, FamilyId family, float delta) {
  if (delta == 0.0F || family.value == kInvalidEntityIdValue) {
    return;
  }
  for (ResidentRow& person : current.residents.rows) {
    if (person.family.value == family.value) {
      person.attitude_to_chairman =
          std::clamp(person.attitude_to_chairman + delta, 0.0F, kAttitudeMax);
    }
  }
}

FamilyId FamilyOf(const WorldState& current, ResidentId resident) {
  const std::uint32_t row = FindRow(current.residents, resident);
  return row == kNoRow ? FamilyId{} : current.residents.rows[row].family;
}

/// Opens `sack` as the catch, raises kTheftUncovered and the Детектив.
void OpenCatch(WorldState& current, const NightSack& sack, bool by_chairman) {
  ThiefCatchState& state = current.thief_catch;
  state.open_catch = sack;
  state.caught_day = current.calendar.day;
  state.by_chairman = by_chairman ? 1U : 0U;
  RaiseScale(kThiefCatchRules, current.chairman.detective, kThiefCatchRules.detective_step);
  SimEvent& uncovered =
      EmitEvent(current, EventKind::kTheftUncovered, EventSeverity::kInterrupting);
  uncovered.resident = sack.thief;
  uncovered.unit = sack.unit;
  uncovered.resource = sack.resource;
  uncovered.amount = sack.grams;
  uncovered.family = FamilyId{by_chairman ? 1U : 0U};
}

void CloseCatch(ThiefCatchState& state) {
  state.open_catch = NightSack{};
  state.caught_day = 0;
  state.by_chairman = 0;
}

/// kReturnStolen: what is left of the sack in its still's raw goes back to its store.
void ReturnSack(const NightTradeConfig& night, WorldState& current, const NightSack& sack) {
  const std::uint32_t still = FindRow(current.residents, sack.still);
  const std::uint32_t store = FindRow(current.units, sack.unit);
  if (still == kNoRow || store == kNoRow || sack.resource.value == kInvalidDefIdValue) {
    return;
  }
  ResidentRow& distiller = current.residents.rows[still];
  std::uint32_t& raw = sack.resource.value == night.potato.value ? distiller.still_raw_potato_g
                                                                 : distiller.still_raw_grain_g;
  const auto back = static_cast<std::uint32_t>(
      std::min<Grams>(static_cast<Grams>(raw), std::max<Grams>(sack.grams, 0)));
  raw -= back;
  UnitRow& unit = current.units.rows[store];
  if (unit.stock.size() <= sack.resource.value) {
    unit.stock.resize(static_cast<std::size_t>(sack.resource.value) + 1U, 0);
  }
  unit.stock[sack.resource.value] += static_cast<Grams>(back);
}

/// kWorkOff: the trudodni written off his family's account by the sack's weight, not below 0.
void FineLabourDays(WorldState& current, const NightSack& sack) {
  const std::uint32_t family = FindRow(current.families, FamilyOf(current, sack.thief));
  if (family == kNoRow) {
    return;
  }
  const float kilograms = static_cast<float>(sack.grams) / static_cast<float>(kGramsPerKilogram);
  const auto fine = static_cast<TrudodniHundredths>(
      kilograms * kThiefCatchRules.work_off_days_per_kg * kTrudodniHundredths);
  TrudodniHundredths& account = current.families.rows[family].trudodni_account;
  account = account > fine ? account - fine : 0;
}

/// kHandToConstable: he goes to the district for the months, «под следствием».
void SendToConstable(WorldState& current, const NightSack& sack) {
  const std::uint32_t row = FindRow(current.residents, sack.thief);
  if (row == kNoRow) {
    return;
  }
  ResidentRow& thief = current.residents.rows[row];
  thief.away_reason = static_cast<std::uint8_t>(AwayReason::kInvestigation);
  thief.away_until_day = current.calendar.day + (kThiefCatchRules.constable_months * kDaysPerMonth);
  thief.away_until_hour = kBackHour;
  thief.away_walk_hours = 0;
  thief.night_trade = NightTrade::kNone;
}

/// kFindStill: the still his sack went to is taken, as kTakeNightTrader takes it.
void TakeStill(WorldState& current, const NightSack& sack) {
  const std::uint32_t row = FindRow(current.residents, sack.still);
  if (row == kNoRow || current.residents.rows[row].night_trade != NightTrade::kDistiller) {
    return;
  }
  current.residents.rows[row].night_trade = NightTrade::kNone;
}

/// The judgement's choice applied, and kThiefJudged raised (`order` invalid: lapsed).
void Judge(const NightTradeConfig& night, WorldState& current, ThiefChoice choice, OrderId order) {
  const NightSack sack = current.thief_catch.open_catch;
  const auto index = static_cast<std::size_t>(choice);
  const ThiefCatchRules& rules = kThiefCatchRules;
  const FamilyId moved = choice == ThiefChoice::kFindStill ? FamilyOf(current, sack.still)
                                                           : FamilyOf(current, sack.thief);
  MoveFamilyAttitude(current, moved, rules.attitude[index]);
  RaiseScale(rules, current.chairman.discipline, rules.discipline[index]);
  switch (choice) {
    case ThiefChoice::kCloseEyes:
      break;
    case ThiefChoice::kTalk:
      current.thief_catch.talked.push_back(
          TalkedThief{.thief = sack.thief, .until_day = current.calendar.day + rules.talk_days});
      break;
    case ThiefChoice::kReturnStolen:
      ReturnSack(night, current, sack);
      break;
    case ThiefChoice::kWorkOff:
      FineLabourDays(current, sack);
      break;
    case ThiefChoice::kHandToConstable:
      SendToConstable(current, sack);
      MoveAuthority(current, kAuthorityRules, AuthorityDeed::kThiefToConstable);
      current.chairman.raikom_reputation = std::clamp(
          current.chairman.raikom_reputation + rules.constable_district, 0.0F, kScaleMax);
      break;
    case ThiefChoice::kTakeUnderWing:
      ++current.thief_catch.under_wing_count;
      break;
    case ThiefChoice::kFindStill:
      TakeStill(current, sack);
      break;
    case ThiefChoice::kThiefChoiceCount:
      break;
  }
  SimEvent& judged = EmitEvent(current, EventKind::kThiefJudged, EventSeverity::kNotable);
  judged.resident = sack.thief;
  judged.amount = static_cast<std::int64_t>(choice);
  judged.family = moved;
  judged.order = order;
  CloseCatch(current.thief_catch);
}

bool CatchOpen(const ThiefCatchState& state) {
  return state.open_catch.thief.value != kInvalidEntityIdValue;
}

/// A catch open longer than the window lapses as «закрыть глаза».
void LapseOldCatch(const NightTradeConfig& night, WorldState& current) {
  const ThiefCatchState& state = current.thief_catch;
  if (CatchOpen(state) &&
      current.calendar.day > state.caught_day + kThiefCatchRules.judge_within_days) {
    Judge(night, current, ThiefChoice::kCloseEyes, OrderId{});
  }
}

/// A new catch: an open one lapses first («закрыть глаза»), then the new one opens.
void Catch(const NightTradeConfig& night,
           WorldState& current,
           const NightSack& sack,
           bool by_chairman) {
  if (CatchOpen(current.thief_catch)) {
    Judge(night, current, ThiefChoice::kCloseEyes, OrderId{});
  }
  OpenCatch(current, sack, by_chairman);
}

}  // namespace

void OpenTheftNight(WorldState& current) {
  current.thief_catch.last_night.clear();
}

void RecordNightSack(WorldState& current,
                     ResidentId thief,
                     UnitId unit,
                     ResourceId resource,
                     Grams grams,
                     ResidentId still) {
  current.thief_catch.last_night.push_back(NightSack{.thief = thief,
                                                     .unit = unit,
                                                     .resource = resource,
                                                     .grams = grams,
                                                     .still = still,
                                                     .day = current.calendar.day});
}

bool TalkedOutOfTheft(const WorldState& current, ResidentId thief, SimDay day) {
  return std::ranges::any_of(current.thief_catch.talked, [&](const TalkedThief& talked) {
    return talked.thief.value == thief.value && day <= talked.until_day;
  });
}

void CatchAtWatchedStores(const NightTradeConfig& night, WorldState& current) {
  const std::vector<NightSack> sacks = current.thief_catch.last_night;
  for (const NightSack& sack : sacks) {
    if (sack.day != current.calendar.day) {
      continue;
    }
    const std::uint32_t row = FindRow(current.units, sack.unit);
    if (row == kNoRow) {
      continue;
    }
    // A SOBER WATCHMAN AT A STORE LEFT OPEN BY A DRINKING STOREKEEPER (boss): the only store
    // a watchman can catch at - where he deters, nobody comes.
    const StoreWatch watch = StoreWatchAt(night, current, row);
    if (watch.sober_watch && watch.drinking_keeper) {
      Catch(night, current, sack, false);
      return;  // the first such sack of the night
    }
  }
}

void ConsumeThiefCatchOrders(const NightTradeConfig& night, WorldState& current) {
  LapseOldCatch(night, current);
  // The talked whose season is over are let go of.
  std::erase_if(current.thief_catch.talked, [&current](const TalkedThief& talked) {
    return current.calendar.day > talked.until_day;
  });
  for (std::uint32_t index = 0; index < current.orders.rows.size(); ++index) {
    OrderRow& order = current.orders.rows[index];
    if (order.status != OrderStatus::kPending ||
        (order.kind != OrderKind::kCatchThief && order.kind != OrderKind::kJudgeThief)) {
      continue;
    }
    OrderRefusal refusal = OrderRefusal::kNone;
    if (order.kind == OrderKind::kCatchThief) {
      const auto found = std::ranges::find_if(
          current.thief_catch.last_night,
          [&order](const NightSack& sack) { return sack.unit.value == order.unit.value; });
      if (found == current.thief_catch.last_night.end()) {
        refusal = OrderRefusal::kNoSuchSubject;
      } else {
        const NightSack sack = *found;
        Catch(night, current, sack, true);
      }
    } else {
      const ThiefCatchState& state = current.thief_catch;
      if (!CatchOpen(state) || state.open_catch.thief.value != order.resident.value) {
        refusal = OrderRefusal::kNoSuchSubject;
      } else if (order.amount < 0 ||
                 order.amount >= static_cast<Grams>(ThiefChoice::kThiefChoiceCount)) {
        refusal = OrderRefusal::kNotEligible;
      } else if (current.calendar.day > state.caught_day + kThiefCatchRules.judge_within_days) {
        refusal = OrderRefusal::kRuleForbids;
      } else {
        Judge(
            night, current, static_cast<ThiefChoice>(order.amount), current.orders.row_ids[index]);
      }
    }
    order.status = refusal == OrderRefusal::kNone ? OrderStatus::kDone : OrderStatus::kRefused;
    order.refusal = refusal;
  }
}

}  // namespace core
