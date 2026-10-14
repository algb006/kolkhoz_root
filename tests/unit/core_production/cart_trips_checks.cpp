// The checks of whole trips and the routes (cart_trips_checks.h).

#include "cart_trips_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cart_trips.h"
#include "core_common/herd_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

bool Near(float a, float b) {
  return std::fabs(a - b) <= 1.0e-4F * (std::fabs(a) + std::fabs(b) + 1.0F);
}

std::uint16_t RowOf(const core::ITableSet& tables, const char* table, const char* key) {
  return static_cast<std::uint16_t>(tables.FindTable(table)->FindRowByKey(key));
}

/// A works at `position` awaiting `grams` of `resource` from the store at
/// `from` (its front leg).
void AddWorks(core::WorldState& world,
              core::Vec2 position,
              core::Vec2 from,
              std::uint16_t resource,
              core::Grams grams,
              std::size_t resources) {
  core::UnitRow unit;
  unit.level = 1;
  unit.position = position;
  unit.stock.assign(resources, 0);
  core::SupplyLeg leg;
  leg.from = from;
  leg.grams.assign(resources, 0);
  leg.grams[resource] = grams;
  unit.construction.awaiting = leg.grams;
  unit.construction.legs.push_back(leg);
  core::AppendRow(world.units, unit);
}

}  // namespace

int CheckWholeTripsAndTheRoutes() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "cart trips: the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  // THE PRICE DOOR: 200 kg on a cart of 1800 kg over a 3-hour round trip.
  core::HaulRate rate;
  rate.load = 1'800'000;
  rate.round_trip_hours = 3.0F;
  rate.produce_cart = true;
  const float day = config.standard_day_hours;
  config.cart_trip_model = 0;
  const float fluid = core::CartDaysFor(config, 200'000, rate);
  config.cart_trip_model = 1;
  const float whole = core::CartDaysFor(config, 200'000, rate);
  const float added = core::CartDaysAdded(config, 200'000, 100'000, rate);
  const float added_over = core::CartDaysAdded(config, 1'700'000, 200'000, rate);
  std::cout << "  cart trips: 200 kg - fluid " << fluid << " cart-days, whole " << whole
            << "; 100 kg more on it " << added << ", 200 kg over a full cart's edge " << added_over
            << '\n';
  failures += Expect(Near(fluid, (200.0F / 1800.0F) * 3.0F / day) && Near(whole, 3.0F / day),
                     "cart trips: the fluid pays a ninth of a trip, whole trips the whole trip");
  failures += Expect(Near(added, 0.0F) && Near(added_over, 3.0F / day),
                     "cart trips: what rides the last trip adds nothing; what spills over adds a "
                     "whole trip");
  core::HaulRate walked = rate;
  walked.produce_cart = false;
  failures += Expect(Near(core::CartDaysFor(config, 200'000, walked), fluid),
                     "cart trips: a carrier on foot is priced as before, whatever the model");

  // THE TOURS (WR): three works by one store - two awaiting bricks (packed),
  // one awaiting logs (long). The two brick loads ride one cart; the logs do
  // not mix with them and go alone.
  config.cart_trip_model = 2;
  core::WorldState world;
  const std::size_t resources = config.feed_values.size();
  core::HerdRow team;  // a kolkhoz horse: the settlement has carts
  team.kind = config.horse_kind;
  team.adult_count = 2;
  core::AppendRow(world.herds, team);
  const core::Vec2 store{.x = 0.0F, .y = 0.0F};
  const std::uint16_t brick = RowOf(*tables, "resources", "brick");
  const std::uint16_t log = RowOf(*tables, "resources", "log");
  AddWorks(world, {.x = 500.0F, .y = 0.0F}, store, brick, 300'000, resources);
  AddWorks(world, {.x = 900.0F, .y = 0.0F}, store, brick, 300'000, resources);
  AddWorks(world, {.x = 700.0F, .y = 0.0F}, store, log, 300'000, resources);
  const std::vector<core::CartTour> tours = core::PlanSupplyTours(config, world);
  std::cout << "  cart trips: tours " << tours.size();
  for (const core::CartTour& tour : tours) {
    std::cout << " [" << tour.stops.size() << " stops, " << tour.grams / 1000 << " kg, "
              << tour.trips << " trip(s)]";
  }
  std::cout << '\n';
  bool bricks_together = false;
  bool logs_alone = false;
  float shares = 0.0F;
  float tour_days = 0.0F;
  for (const core::CartTour& tour : tours) {
    bricks_together = bricks_together || (tour.stops.size() == 2 && tour.stops[0].unit_row == 0 &&
                                          tour.stops[1].unit_row == 1);
    logs_alone = logs_alone || (tour.stops.size() == 1 && tour.stops[0].unit_row == 2);
    for (std::size_t stop = 0; stop < tour.stops.size(); ++stop) {
      shares += core::StopShareOfTour(config, tour, stop);
    }
    tour_days += static_cast<float>(tour.trips) * tour.tour_hours / day;
  }
  failures += Expect(tours.size() == 2 && bricks_together && logs_alone,
                     "cart trips: the two brick loads by one store ride one cart, nearest first; "
                     "the logs do not mix with them and go alone");
  failures += Expect(Near(shares, tour_days),
                     "cart trips: the stops' shares add up to their tours' whole trips");
  // THE OTHER SIDE: under W no tour is planned - each works is its own leg.
  config.cart_trip_model = 1;
  failures += Expect(core::PlanSupplyTours(config, world).empty(),
                     "cart trips: whole trips without the routes plan no tour");
  return failures;
}
