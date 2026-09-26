// Simulation run: a timber lot bought on the district's limit is fetched by
// the village's own carts (decision 279; district design §4, «Исключение —
// брёвна»; 0.36.17), and fetched when no field holds the horses (0.36.18).
//
// THE KNOWN ANSWER, two identical start canons (seed 1931), one ordering the
// catalogue's `timber_lot` on day 2:
//   - on the lot's arrival day its logs are NOT in the stores (until 0.36.17
//     they walked in by themselves on the third day, with no hauling day and
//     no horse — core's check of 26.09);
//   - the ordering village spends hauling man-days the other does not, its
//     carters ride out to the map's northern border end, and the lot's logs
//     come into the stores only as they are carted;
//   - NO CARTER GOES FOR THE LOT ON A DAY A PLOUGH OR A HARROW WAITS WITH NO
//     CREW, the rain not stopping it (0.36.18: the fetch is windowless, below
//     every field work with a window; with 0.36.17's haul window all sixteen
//     horses stood at the district while the rye's fallow waited), and none
//     goes on foot;
//   - the lot's row goes when it is empty, and the books count the trips
//     under the district (CartLoadSource::kDistrict). Summed over the years
//     the run crosses: the year's book is cleared at the year's end, and the
//     windowless fetch waits out the spring and the winter.

#include <cstdint>
#include <iostream>
#include <span>

#include "../common/run_harness.h"
#include "core_common/labor_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/rain_stops_work.h"
#include "core_common/world_state.h"

namespace {

constexpr std::uint64_t kSeed = 1931;
constexpr std::uint32_t kOrderDay = 2;

/// The horizon: the run stops on the first day the lot's row is seen gone,
/// or here.
/// Five years — the canon's worst wait was 94 days (seed 1932).
constexpr std::uint32_t kMaxDays = 240;

core::Grams Held(const core::WorldState& world, std::uint32_t resource) {
  core::Grams held = 0;
  for (const core::UnitRow& unit : world.units.rows) {
    held += resource < unit.stock.size() ? unit.stock[resource] : 0;
  }
  return held;
}

std::uint32_t CartingTheLot(const core::WorldState& world) {
  std::uint32_t carters = 0;
  for (const core::ResidentRow& person : world.residents.rows) {
    carters += person.work.limit_delivery.value != core::kInvalidEntityIdValue ? 1U : 0U;
  }
  return carters;
}

/// Carters on the lot with no horse under them: 25 km beyond the border is no
/// carry for a back (0.36.18; the on-foot rule for a carter with no horse
/// took them until then).
std::uint32_t CartingTheLotOnFoot(const core::WorldState& world) {
  std::uint32_t walkers = 0;
  for (const core::ResidentRow& person : world.residents.rows) {
    walkers += person.work.limit_delivery.value != core::kInvalidEntityIdValue &&
                       person.work.rides_horse == 0
                   ? 1U
                   : 0U;
  }
  return walkers;
}

/// Fields in a horse work (ploughing, harrowing) with work left and nobody
/// on them, on a day the rain does not stop that work. IN THAT WORK SINCE THE
/// DAY BEFORE (`morning`): a phase opens in production, after the morning's
/// placement, and a field that opened today had no plough to offer anyone
/// (the first draft of this check counted the two opening days of spring).
std::uint32_t HorseFieldsWaiting(const core::WorldState& morning, const core::WorldState& world) {
  std::uint32_t waiting = 0;
  for (std::uint32_t row = 0; row < world.fields.rows.size(); ++row) {
    const core::FieldRow& field = world.fields.rows[row];
    const bool plough = field.phase == core::FieldPhase::kPlowing;
    const bool harrow = field.phase == core::FieldPhase::kHarrowing;
    if ((!plough && !harrow) || !(field.work_days_remaining > 0.0F)) {
      continue;
    }
    const bool open_since_morning =
        row < morning.fields.rows.size() &&
        morning.fields.row_ids[row].value == world.fields.row_ids[row].value &&
        morning.fields.rows[row].phase == field.phase;
    if (!open_since_morning) {
      continue;
    }
    const core::WorkKind kind = plough ? core::WorkKind::kPlowing : core::WorkKind::kHarrowing;
    if (core::RainStopsWork(world.weather.precipitation, kind)) {
      continue;
    }
    // CREWED BY ITS OWN WORK: a field may be ploughed and carted at once, and
    // a carter on its load does not plough it (static review of 0.36.18 —
    // the first draft counted any worker on the field, the very case of the
    // rye's fallow with the oats still lying on it).
    bool crewed = false;
    for (const core::ResidentRow& person : world.residents.rows) {
      crewed = crewed || (person.work.field.value == world.fields.row_ids[row].value &&
                          person.work.kind == kind);
    }
    waiting += crewed ? 0U : 1U;
  }
  return waiting;
}

}  // namespace

int main() {
  int failures = 0;
  run::Simulation with = run::Start(kSeed);
  run::Simulation without = run::Start(kSeed);
  if (!with || !without) {
    return 1;
  }
  const std::uint32_t log = with.tables->FindTable("resources")->FindRowByKey("log");
  const std::uint32_t lot = with.tables->FindTable("limit_catalog")->FindRowByKey("timber_lot");
  if (run::Expect(log != core::kNoTableRow && lot != core::kNoTableRow,
                  "the tables name logs and the timber lot") != 0) {
    return 1;
  }
  const auto hauling = static_cast<std::size_t>(core::WorkKind::kHauling);
  const auto district = static_cast<std::size_t>(core::CartLoadSource::kDistrict);

  std::uint32_t arrive_day = 0;
  core::Grams logs_on_arrival_extra = -1;
  std::uint32_t carter_days = 0;
  std::uint32_t carter_days_over_a_plough = 0;
  std::uint32_t carters_on_foot = 0;  // man-days
  std::uint32_t emptied_day = 0;
  // The closed years' sums; the open year's book is added at the end.
  float hauling_extra_closed = 0.0F;
  float district_trips_closed = 0.0F;
  core::Grams district_grams_closed = 0;
  std::uint32_t closed_year = with.State().ledger.closed.year;
  for (std::uint32_t day = 0; day < kMaxDays && emptied_day == 0; ++day) {
    if (day == kOrderDay) {
      core::OrderRow order;
      order.kind = core::OrderKind::kOrderLimitLot;
      order.lot = core::LimitLotId{static_cast<std::uint16_t>(lot)};
      with->StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    }
    const core::WorldState morning = with.State();
    run::AdvanceDays(*with, 1);
    run::AdvanceDays(*without, 1);
    const core::WorldState& a = with.State();
    const core::WorldState& b = without.State();
    if (a.ledger.closed.year != closed_year) {
      closed_year = a.ledger.closed.year;
      hauling_extra_closed +=
          a.ledger.closed.work_days_by_kind[hauling] - b.ledger.closed.work_days_by_kind[hauling];
      district_trips_closed += a.ledger.closed.cart_trips[district];
      district_grams_closed += a.ledger.closed.cart_grams[district];
    }
    if (arrive_day == 0 && !a.limit_deliveries.rows.empty()) {
      arrive_day = a.limit_deliveries.rows[0].arrive_day;
    }
    if (arrive_day != 0 && a.calendar.day == arrive_day + 1U) {
      logs_on_arrival_extra = Held(a, log) - Held(b, log);
    }
    const std::uint32_t carters = CartingTheLot(a);
    carter_days += carters > 0 ? 1U : 0U;
    carter_days_over_a_plough += carters > 0 && HorseFieldsWaiting(morning, a) > 0 ? 1U : 0U;
    carters_on_foot += CartingTheLotOnFoot(a);
    if (arrive_day != 0 && emptied_day == 0 && a.limit_deliveries.rows.empty()) {
      emptied_day = a.calendar.day;
    }
  }
  const core::WorldState& end = with.State();
  const float hauling_extra = hauling_extra_closed + end.ledger.current.work_days_by_kind[hauling] -
                              without.State().ledger.current.work_days_by_kind[hauling];
  const float district_trips = district_trips_closed + end.ledger.current.cart_trips[district];
  const core::Grams district_grams =
      district_grams_closed + end.ledger.current.cart_grams[district];
  const core::Grams logs_extra = Held(end, log) - Held(without.State(), log);
  std::cout << "district_lot: seed " << kSeed << ", lot at the district on day " << arrive_day
            << ", logs in the stores beyond the other village on the day after: "
            << logs_on_arrival_extra / 1000 << " kg; days with a carter on the lot " << carter_days
            << " (of them with a plough or harrow waiting uncrewed: " << carter_days_over_a_plough
            << "; carter man-days on foot " << carters_on_foot << "), the lot's row gone on day "
            << emptied_day << (emptied_day == 0 ? " (NOT GONE in the horizon)" : "")
            << ", extra hauling man-days " << hauling_extra
            << ", logs beyond the other village at the end " << logs_extra / 1000
            << " kg; district trips booked " << district_trips << " (" << district_grams / 1000
            << " kg)\n";

  failures += run::Expect(arrive_day > 0, "the timber lot was bought and is on its way");
  failures += run::Expect(logs_on_arrival_extra == 0,
                          "on its arrival day the lot's logs are NOT in the stores: they wait at "
                          "the district centre for the village's carts");
  failures += run::Expect(carter_days > 0,
                          "the village's carters are sent for the lot (a work on the lot's row)");
  failures += run::Expect(carter_days_over_a_plough == 0,
                          "and never on a day a plough or a harrow waits with nobody on it: the "
                          "fetch is windowless, the field's horse work goes first");
  failures += run::Expect(carters_on_foot == 0,
                          "and every carter on the lot rides a horse: with none free, nobody "
                          "is sent");
  failures += run::Expect(hauling_extra > 0.0F,
                          "and the ordering village spends hauling man-days the other does not");
  failures += run::Expect(emptied_day > arrive_day && logs_extra > 0,
                          "the lot's logs come in as they are carted, and its row goes when empty");
  failures += run::Expect(district_trips > 0.0F && district_grams > 0,
                          "the books count the trips under the district");

  std::cout << (failures == 0 ? "district_lot: all checks passed\n"
                              : "district_lot: FAILURES ABOVE\n");
  return failures;
}
