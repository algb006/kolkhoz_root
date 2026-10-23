// The whole-trip arithmetic of the drawn trip's price (core_common/trip_terms.h; #1056 C3).

#include <cmath>
#include <cstdint>
#include <iostream>

#include "core_common/land_state.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/trip_terms.h"
#include "core_common/work_seam.h"
#include "core_common/world_state.h"

namespace core_test {
namespace {

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: trip terms: " << label << '\n';
  return 1;
}

}  // namespace

int TestTripTerms() {
  int failures = 0;
  // A 750 kg cart: a tonne is two trips, 750 kg one, a gram over it two.
  constexpr core::Grams kCart = 750'000;
  failures +=
      Expect(core::WholeTripsFor(1'000'000, kCart) == 2, "a tonne at 750 kg a cart is two trips");
  failures +=
      Expect(core::WholeTripsFor(kCart, kCart) == 1 && core::WholeTripsFor(kCart + 1, kCart) == 2,
             "a full load is one trip, a gram over it two");
  failures += Expect(core::WholeTripsFor(0, kCart) == 0 && core::WholeTripsFor(1'000, 0) == 0 &&
                         core::WholeTripsFor(-5, kCart) == 0,
                     "no grams or no load, no trip");

  // THE FALLBACK: whole trips at the round trip, over the norm day - trip_model 1's arithmetic.
  core::TaskTripTerms terms;
  terms.trip_hours = 1.5F;
  terms.trip_loaded_hours = 0.9F;
  terms.cart_load = kCart;
  failures += Expect(core::FallbackCartDays(terms, 1'000'000, 10.0F) == 0.3F,
                     "a tonne: two trips of 1.5 h over a 10 h day is 0.3 cart-days");
  failures +=
      Expect(core::FallbackCartDays(terms, 1, 10.0F) == 0.15F, "one gram still takes a whole trip");
  core::TaskTripTerms none;
  failures += Expect(core::FallbackCartDays(none, 1'000'000, 10.0F) == 0.0F &&
                         core::FallbackCartDays(terms, 1'000'000, 0.0F) == 0.0F,
                     "terms with no trip, or no norm day, price nothing");
  std::cout << "  trip terms: a tonne at 750 kg is " << core::WholeTripsFor(1'000'000, kCart)
            << " trips, " << core::FallbackCartDays(terms, 1'000'000, 10.0F) << " cart-days\n";

  // THE CARRIED LOAD DRAINS WITH ITS CARRIER (work_seam.h, CoDrainCarriedTasks; ruling 3): heap
  // B carried by A's task at half A's rate. A's carter drains 0.1 cart-days: B loses 0.05, and the
  // cart is noted as its drainer; a drain beyond B's seam floors it at nought; a task carried by
  // nobody, or carried by another, is not touched.
  core::WorldState world;
  const auto heap = [&world](float seam) {
    core::FieldRow field;
    field.kind = core::LandKind::kArable;
    field.reaped_grams = 400'000;
    field.reaped_resource = core::ResourceId{0};
    field.haul_days_remaining = seam;
    field.haul_days_written = seam;
    return core::AppendRow(world.fields, field);
  };
  const core::FieldId carrier_field = heap(0.5F);
  const core::FieldId carried_field = heap(0.2F);
  const core::FieldId other_field = heap(0.2F);
  core::LogisticsTaskRow carrier;
  carrier.load_kind = core::LogisticsLoadKind::kFieldHeap;
  carrier.field = carrier_field;
  const core::LogisticsTaskId carrier_id = core::AppendRow(world.logistics_tasks, carrier);
  core::LogisticsTaskRow carried = carrier;
  carried.field = carried_field;
  carried.carried_by = carrier_id;
  carried.carried_share = 0.5F;
  core::AppendRow(world.logistics_tasks, carried);
  core::LogisticsTaskRow other = carrier;
  other.field = other_field;
  core::AppendRow(world.logistics_tasks, other);
  const float taken = core::CoDrainCarriedTasks(world, carrier_id, 0.1F);
  const auto seam = [&world](core::FieldId field) {
    return world.fields.rows[core::FindRow(world.fields, field)].haul_days_remaining;
  };
  failures += Expect(std::fabs(taken - 0.05F) < 1.0e-6F &&
                         std::fabs(seam(carried_field) - 0.15F) < 1.0e-6F &&
                         seam(other_field) == 0.2F && seam(carrier_field) == 0.5F &&
                         world.logistics_tasks.rows[1].drained_by_cart > 0.0F,
                     "the carried heap drains with its carrier at its share, noted by cart");
  core::CoDrainCarriedTasks(world, carrier_id, 1.0F);
  failures += Expect(seam(carried_field) == 0.0F, "a drain beyond the carried seam floors it");
  return failures;
}

}  // namespace core_test
