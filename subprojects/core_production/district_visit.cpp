// The district's visits in the simulation (core_production/district_visit.h).

#include "district_visit.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/ids.h"
#include "core_common/state_table_ops.h"

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

DistrictVisitOutcome InspectVisit(const WorldState& /*current*/, const DistrictVisitRow& visit) {
  // STUB, AND IT IS THE WHOLE SUBJECT THAT IS MISSING, not a computation that
  // was skipped. The core keeps no books for a discrepancy to be in, and the
  // faces have no personal reputations to answer a gift with.
  //
  // THIS IS THE DESIGN'S OWN DECISION AND NOT A GAP IN THE DELIVERY
  // (characters design §2, "Эпоха I числами", boss 2026-09-14, re-read and
  // unchanged on 2026-09-17): «Находка: STUB „ничего не найдено", пока нет
  // модели книг и учёта» and «Приём и подарок: не в сборке Эпохи I — приказа
  // нет, личные репутации лиц — STUB. Дверь — после».
  //
  // Three seam fields stand nil because of it — `found`, the reception and
  // gift kinds, and `gift` — and one written rule downstream cannot fire at
  // all (the junior's signal, in ArriveDistrictVisits below). ALL OF THEM
  // COME ALIVE WITH THE BOOKS AND NONE BEFORE, so they are one debt with one
  // door, not four.
  //
  // The outcome is the visit as it came, found nothing.
  return DistrictVisitOutcome{.face = visit.face, .kind = visit.kind};
}

void ArriveDistrictVisits(WorldState& current) {
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
    const bool extraordinary = visit.kind == DistrictVisitKind::kExtraordinary;
    SimEvent& event =
        EmitEvent(current,
                  EventKind::kDistrictVisit,
                  extraordinary ? EventSeverity::kInterrupting : EventSeverity::kNotable);
    event.amount = PackDistrictVisit(outcome);
    // "Младший заметил, доложил, приехал старший" (characters design §2).
    //
    // STUB, AND IT IS THE GATE THAT IS STUBBED, NOT THE RULE. The rule is the
    // design's and stands, written and working; what it reads is nil. `found`
    // comes from InspectVisit, which computes kNone and nothing else, so THIS
    // BRANCH IS NEVER TAKEN — no extraordinary visit on a junior's signal
    // happens in a campaign, not once, and neither does kJuniorMiss, which is
    // computed from the same finding.
    //
    // WHAT IS MISSING IS THE BOOKS. Not a field and not a condition: the core
    // keeps no accounts for a discrepancy to be found in, and the design says
    // so in as many words — «Находка: STUB „ничего не найдено", пока нет
    // модели книг и учёта» (characters design §2, "Эпоха I числами", boss's
    // decision of 2026-09-14, unchanged). The mark comes off WITH THE MODEL
    // OF THE BOOKS, in one move, and not a day earlier.
    //
    // Marked rather than mended because a rule that cannot fire is a stub,
    // and the next reader who finds it unmarked reports it as a live rule —
    // which happened three times in two days, once costing a named rejection
    // in the design of the only way out of a dead end.
    if (visit.kind == DistrictVisitKind::kRegular && outcome.found != DistrictVisitFinding::kNone) {
      CallExtraordinary(current, SeniorOfChannel(visit.face), DistrictVisitCause::kJuniorSignal);
    }
  }
}

}  // namespace core
