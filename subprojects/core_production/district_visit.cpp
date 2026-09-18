// The district's visits in the simulation (core_production/district_visit.h).

#include "district_visit.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/ids.h"
#include "core_common/ledger_state.h"
#include "core_common/state_table_ops.h"
#include "stock_ops.h"

namespace core {
namespace {

/// Whether a visit of `face` arriving on `day` already stands in the table —
/// the guard that keeps a call or an announcement from doubling.
bool VisitStands(const WorldState& current, DistrictFace face, std::uint32_t day) {
  return std::ranges::any_of(current.district_visits.rows,
                             [face, day](const DistrictVisitRow& row) {
                               return row.face == face && row.arrive_day == day;
                             });
}

/// Whether any visit of `face` is on its way, whatever its day.
bool FaceOnItsWay(const WorldState& current, DistrictFace face) {
  return std::ranges::any_of(current.district_visits.rows,
                             [face](const DistrictVisitRow& row) { return row.face == face; });
}

/// Calls an extraordinary visit of `face` for tomorrow, unannounced, unless
/// one of that face is already on its way.
void CallExtraordinary(WorldState& current, DistrictFace face, DistrictVisitCause cause) {
  if (FaceOnItsWay(current, face)) {
    return;
  }
  DistrictVisitRow visit;
  visit.arrive_day = current.calendar.day + 1U;
  visit.face = face;
  visit.kind = DistrictVisitKind::kExtraordinary;
  visit.cause = cause;
  AppendRow(current.district_visits, visit);
}

}  // namespace

DistrictFace SeniorOfChannel(DistrictFace junior) {
  switch (junior) {
    case DistrictFace::kKarasev:
      return DistrictFace::kStozharov;
    case DistrictFace::kPolushkina:
      return DistrictFace::kZhernova;
    default:
      return junior;
  }
}

void AnnounceRegularVisits(const ProductionConfig& config, WorldState& current) {
  struct Junior {
    DistrictFace face;
    std::uint8_t month;
  };

  const std::array<Junior, 2> juniors = {{
      {.face = DistrictFace::kKarasev, .month = config.district_visits.karasev_month},
      {.face = DistrictFace::kPolushkina, .month = config.district_visits.polushkina_month},
  }};
  const std::uint32_t arrive_day = current.calendar.day + config.district_visits.notice_days;
  const std::uint32_t day_of_year = arrive_day % kDaysPerYear;
  for (const Junior& junior : juniors) {
    if (junior.month < 1U || junior.month > kMonthsPerYear) {
      continue;  // the catalogue refuses such a month; a hand-made config idles
    }
    if (day_of_year != (static_cast<std::uint32_t>(junior.month) - 1U) * kDaysPerMonth ||
        VisitStands(current, junior.face, arrive_day)) {
      continue;
    }
    DistrictVisitRow visit;
    visit.arrive_day = arrive_day;
    visit.face = junior.face;
    visit.kind = DistrictVisitKind::kRegular;
    visit.cause = DistrictVisitCause::kSchedule;
    AppendRow(current.district_visits, visit);
    const DistrictVisitOutcome announced{.face = junior.face, .kind = DistrictVisitKind::kRegular};
    SimEvent& event =
        EmitEvent(current, EventKind::kDistrictVisitAnnounced, EventSeverity::kNotable);
    event.amount = PackDistrictVisit(announced);
  }
}

void CallPlanFailedVisit(WorldState& current) {
  // The raikom's business: a failed plan is Korenev's question (boss, parcel
  // 324), and he asks it himself.
  CallExtraordinary(current, DistrictFace::kKorenev, DistrictVisitCause::kPlanFailed);
}

namespace {

/// The finance channel's two faces — the ones who count the stores.
bool CountsTheStores(DistrictFace face) {
  return face == DistrictFace::kPolushkina || face == DistrictFace::kZhernova;
}

/// Grams of the produce at `index` standing in the stores above the
/// accumulation limit; 0 where there is no limit on it.
Grams SurplusAboveLimit(const WorldState& current, std::uint32_t index) {
  const Grams limit = index < current.plan.accumulation_limit.size()
                          ? current.plan.accumulation_limit[index]
                          : Grams{0};
  if (limit <= 0) {
    return 0;
  }
  const Grams held = HeldEverywhere(current, DefIdFromIndex<ResourceIdTag>(index));
  return held > limit ? held - limit : 0;
}

}  // namespace

DistrictVisitOutcome InspectVisit(const WorldState& current, const DistrictVisitRow& visit) {
  DistrictVisitOutcome outcome{.face = visit.face, .kind = visit.kind};
  // THE ONE FINDING WITHOUT BOOKS (district §9; boss seq 76 and 81,
  // 2026-09-18; characters §2 amended the same day): a finance auditor counts
  // the stores against the accumulation limit, and a surplus is a
  // discrepancy. A COUNT OF THE STORE, NOT A DISCREPANCY IN THE ACCOUNTS —
  // which is why it did not wait for the books.
  //
  // EVERY OTHER FINDING IS STILL A STUB, the design's own decision
  // (characters §2, "Эпоха I числами", boss 2026-09-14): «Находка: STUB
  // „ничего не найдено", пока нет модели книг и учёта», and «Приём и
  // подарок: не в сборке Эпохи I». The reception and gift kinds and `gift`
  // stay nil, and come alive with the books and not before.
  if (CountsTheStores(visit.face)) {
    for (std::uint32_t index = 0; index < current.plan.accumulation_limit.size(); ++index) {
      if (SurplusAboveLimit(current, index) > 0) {
        outcome.found = DistrictVisitFinding::kDiscrepancy;
        break;
      }
    }
  }
  return outcome;
}

Grams SeizeAboveLimit(const ProductionConfig& config, WorldState& current) {
  Grams seized = 0;
  for (std::uint32_t index = 0; index < current.plan.accumulation_limit.size(); ++index) {
    const Grams surplus = SurplusAboveLimit(current, index);
    if (surplus <= 0) {
      continue;
    }
    const ResourceId resource = DefIdFromIndex<ResourceIdTag>(index);
    const Grams taken = TakeFromStorage(current, config, resource, surplus);
    AddLedgerAmount(current.ledger.current.seized, resource, taken);
    seized += taken;
  }
  if (seized > 0) {
    // «Репутация вниз» (district §9), once a seizure, whatever it took.
    current.chairman.raikom_reputation =
        std::max(0.0F, current.chairman.raikom_reputation - config.limit.seizure_reputation_loss);
  }
  return seized;
}

void ArriveDistrictVisits(const ProductionConfig& config, WorldState& current) {
  // In the order they were announced or called, by id — not by row, which a
  // removal reshuffles (the wedding queue's lesson of 0.24.0).
  std::vector<DistrictVisitId> due;
  for (std::uint32_t row = 0; row < current.district_visits.rows.size(); ++row) {
    if (current.district_visits.rows[row].arrive_day <= current.calendar.day) {
      due.push_back(current.district_visits.row_ids[row]);
    }
  }
  std::ranges::sort(
      due, [](DistrictVisitId left, DistrictVisitId right) { return left.value < right.value; });
  for (const DistrictVisitId id : due) {
    const std::uint32_t row = FindRow(current.district_visits, id);
    if (row == kNoRow) {
      continue;
    }
    const DistrictVisitRow visit = current.district_visits.rows[row];
    RemoveRow(current.district_visits, id);
    const DistrictVisitOutcome outcome = InspectVisit(current, visit);
    // «Не сдал и попался — изымает целиком» (district §9): the surplus the
    // auditor found goes on the day she finds it.
    if (outcome.found == DistrictVisitFinding::kDiscrepancy && CountsTheStores(visit.face)) {
      SeizeAboveLimit(config, current);
    }
    const bool extraordinary = visit.kind == DistrictVisitKind::kExtraordinary;
    SimEvent& event =
        EmitEvent(current,
                  EventKind::kDistrictVisit,
                  extraordinary ? EventSeverity::kInterrupting : EventSeverity::kNotable);
    event.amount = PackDistrictVisit(outcome);
    // "Младший заметил, доложил, приехал старший" (characters design §2).
    //
    // IT FIRES SINCE 2026-09-18, on one finding only. Until then `found` was
    // kNone by construction and this branch was never taken — a rule that
    // could not fire, marked STUB so no reader would call it live. The
    // accumulation limit gave Polushkina her first real finding (InspectVisit),
    // and her regular visit that finds a surplus now calls Zhernova. The other
    // findings — the accounts', and kJuniorMiss with them — still wait for the
    // books (characters §2).
    if (visit.kind == DistrictVisitKind::kRegular && outcome.found != DistrictVisitFinding::kNone) {
      CallExtraordinary(current, SeniorOfChannel(visit.face), DistrictVisitCause::kJuniorSignal);
    }
  }
}

}  // namespace core
