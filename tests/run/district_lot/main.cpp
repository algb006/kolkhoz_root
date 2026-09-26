// Simulation run: a timber lot bought on the district's limit is fetched by
// the village's own carts (decision 279; district design §4, «Исключение —
// брёвна»; 0.36.17).
//
// THE KNOWN ANSWER, two identical start canons (seed 1931), one ordering the
// catalogue's `timber_lot` on day 2:
//   - on the lot's arrival day its logs are NOT in the stores (until 0.36.17
//     they walked in by themselves on the third day, with no hauling day and
//     no horse — core's check of 26.09);
//   - the ordering village spends hauling man-days the other does not, its
//     carters ride out to the map's northern border end, and the lot's logs
//     come into the stores only as they are carted;
//   - the lot's row goes when it is empty, and the year's book counts the
//     trips under the district (CartLoadSource::kDistrict).

#include <cstdint>
#include <iostream>
#include <span>

#include "../common/run_harness.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/world_state.h"

namespace {

constexpr std::uint64_t kSeed = 1931;
constexpr std::uint32_t kOrderDay = 2;
constexpr std::uint32_t kDays = 40;

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
  std::uint32_t emptied_day = 0;
  float hauling_extra = 0.0F;
  for (std::uint32_t day = 0; day < kDays; ++day) {
    if (day == kOrderDay) {
      core::OrderRow order;
      order.kind = core::OrderKind::kOrderLimitLot;
      order.lot = core::LimitLotId{static_cast<std::uint16_t>(lot)};
      with->StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    }
    run::AdvanceDays(*with, 1);
    run::AdvanceDays(*without, 1);
    const core::WorldState& a = with.State();
    const core::WorldState& b = without.State();
    if (arrive_day == 0 && !a.limit_deliveries.rows.empty()) {
      arrive_day = a.limit_deliveries.rows[0].arrive_day;
    }
    if (arrive_day != 0 && a.calendar.day == arrive_day + 1U) {
      logs_on_arrival_extra = Held(a, log) - Held(b, log);
    }
    carter_days += CartingTheLot(a) > 0 ? 1U : 0U;
    if (arrive_day != 0 && emptied_day == 0 && a.limit_deliveries.rows.empty()) {
      emptied_day = a.calendar.day;
    }
    hauling_extra =
        a.ledger.current.work_days_by_kind[hauling] - b.ledger.current.work_days_by_kind[hauling];
  }
  const core::WorldState& end = with.State();
  const core::Grams logs_extra = Held(end, log) - Held(without.State(), log);
  std::cout << "district_lot: seed " << kSeed << ", lot at the district on day " << arrive_day
            << ", logs in the stores beyond the other village on the day after: "
            << logs_on_arrival_extra / 1000 << " kg; days with a carter on the lot " << carter_days
            << ", the lot's row gone on day " << emptied_day << ", extra hauling man-days "
            << hauling_extra << ", logs beyond the other village at the end " << logs_extra / 1000
            << " kg; district trips booked " << end.ledger.current.cart_trips[district] << " ("
            << end.ledger.current.cart_grams[district] / 1000 << " kg)\n";

  failures += run::Expect(arrive_day > 0, "the timber lot was bought and is on its way");
  failures += run::Expect(logs_on_arrival_extra == 0,
                          "on its arrival day the lot's logs are NOT in the stores: they wait at "
                          "the district centre for the village's carts");
  failures += run::Expect(carter_days > 0,
                          "the village's carters are sent for the lot (a work on the lot's row)");
  failures += run::Expect(hauling_extra > 0.0F,
                          "and the ordering village spends hauling man-days the other does not");
  failures += run::Expect(emptied_day > arrive_day && logs_extra > 0,
                          "the lot's logs come in as they are carted, and its row goes when empty");
  failures += run::Expect(end.ledger.current.cart_trips[district] > 0.0F,
                          "the year's book counts the trips under the district");

  std::cout << (failures == 0 ? "district_lot: all checks passed\n"
                              : "district_lot: FAILURES ABOVE\n");
  return failures;
}
