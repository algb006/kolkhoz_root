// The checks of the trip's facts (trip_terms_writer_checks.h).

#include "trip_terms_writer_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>

#include "cart_trips.h"
#include "core_common/haul.h"
#include "core_common/ledger_state.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "haul_ways.h"
#include "production_config.h"
#include "trip_terms_writer.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: trip facts: " << label << '\n';
    return 1;
  }
  return 0;
}

bool Near(float a, float b) {
  return std::fabs(a - b) <= 1.0e-5F * (std::fabs(a) + std::fabs(b) + 1.0F);
}

}  // namespace

int CheckTheTripsFacts() {
  int failures = 0;
  // THE LOADED HALF BESIDE THE ROUND TRIP (haul.h, RateOverKm): 2 km at 0.2 h a km empty and
  // 0.3 loaded - the round trip 1.0 h of road and two pauses, the loaded half 0.6 h and both.
  const float pauses = 2.0F * core::kLoadingPauseHours;
  core::HaulRate rate = core::RateOverKm(2.0F, 0.2F, 0.3F, 750'000);
  failures +=
      Expect(Near(rate.round_trip_hours, 1.0F + pauses) && Near(rate.loaded_hours, 0.6F + pauses),
             "the round trip holds the loaded half and both pauses");

  // WRITTEN ONLY FOR A CART's WHOLE-TRIP RATE UNDER THE PLANNED MODEL.
  core::ProductionConfig config;
  rate.produce_cart = true;
  const core::Vec2 heap{.x = 120.0F, .y = -40.0F};
  config.cart_trip_model = static_cast<std::uint8_t>(core::CartTripModel::kPlanned);
  const core::TaskTripTerms planned = core::TripTermsOf(config, heap, rate, 1'000'000);
  failures += Expect(planned.load_point.x == heap.x && planned.load_point.y == heap.y &&
                         planned.trip_hours == rate.round_trip_hours &&
                         planned.trip_loaded_hours == rate.loaded_hours &&
                         planned.cart_load == 750'000 && planned.grams_left == 1'000'000,
                     "the planned model writes the rate's own facts");
  config.cart_trip_model = static_cast<std::uint8_t>(core::CartTripModel::kWholeRouted);
  failures += Expect(core::TripTermsOf(config, heap, rate, 1'000'000).trip_hours == 0.0F,
                     "any other model writes none");
  config.cart_trip_model = static_cast<std::uint8_t>(core::CartTripModel::kPlanned);
  core::HaulRate walked = rate;
  walked.produce_cart = false;
  failures += Expect(core::TripTermsOf(config, heap, walked, 1'000'000).trip_hours == 0.0F &&
                         core::TripTermsOf(config, heap, rate, 0).trip_hours == 0.0F,
                     "a load priced on foot, or no grams, writes none");

  // THE TASK BY ITS KIND AS WELL AS ITS LOAD: one unit named by a store's emptying and a works'
  // supply; the supply's facts land on the supply's task alone.
  core::WorldState world;
  const core::UnitId unit = core::AppendRow(world.units, core::UnitRow{});
  core::LogisticsTaskRow emptying;
  emptying.load_kind = core::LogisticsLoadKind::kStoreTransfer;
  emptying.unit = unit;
  core::AppendRow(world.logistics_tasks, emptying);
  core::LogisticsTaskRow supply = emptying;
  supply.load_kind = core::LogisticsLoadKind::kSiteSupply;
  core::AppendRow(world.logistics_tasks, supply);
  core::WorkAssignment hauled;
  hauled.kind = core::WorkKind::kHauling;
  hauled.unit = unit;
  core::WriteTripTermsFor(
      config, world, core::LogisticsLoadKind::kSiteSupply, hauled, heap, rate, 400'000);
  failures += Expect(world.logistics_tasks.rows[0].trip_terms.trip_hours == 0.0F &&
                         world.logistics_tasks.rows[1].trip_terms.grams_left == 400'000,
                     "the supply's facts land on the supply's task, not the emptying's");
  std::cout << "  trip facts: round trip " << rate.round_trip_hours << " h, loaded "
            << rate.loaded_hours << " h; planned " << planned.cart_load << " g a load\n";

  // U's NUMERATOR (haul_ways.h, BookPaidTrips): 0.3 cart-days drained of a 10 h day over a round
  // trip of 1.5 h are two whole trips paid, on the field's column; a walker's rate pays none.
  core::HaulRate paid = rate;
  paid.round_trip_hours = 1.5F;
  config.standard_day_hours = 10.0F;
  core::BookPaidTrips(config, world, core::CartLoadSource::kField, paid, 0.3F);
  const float on_field =
      world.ledger.current.cart_trips_paid[static_cast<std::size_t>(core::CartLoadSource::kField)];
  core::BookPaidTrips(config, world, core::CartLoadSource::kField, walked, 0.3F);
  failures +=
      Expect(Near(on_field, 2.0F) && world.ledger.current.cart_trips_paid[static_cast<std::size_t>(
                                         core::CartLoadSource::kField)] == on_field,
             "paid trips: the drained cart-days over the round trip, none on foot");
  return failures;
}
