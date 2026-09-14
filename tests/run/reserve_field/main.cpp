// Simulation run: the start quest's first gesture on the real start.
//
// The chairman lets the abandoned reserve field go on the first day
// (start canon §2; construction design §12; fact start_reserve_field_removed,
// host/manual/95 №6). The unit test proves the verb on a hand-made field; this
// run proves it on the field genesis actually lays out, through the whole
// step, and that the village runs its first year afterwards with the arable
// exactly three hectares smaller and nothing else missing.

#include <cstdint>
#include <iostream>
#include <span>

#include "../common/run_harness.h"
#include "core_common/event_state.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace {

constexpr std::uint64_t kSeed = 1929;

/// Hectares of arable land in the world, meadows excluded.
float ArableHectares(const core::WorldState& world) {
  float hectares = 0.0F;
  for (const core::FieldRow& field : world.fields.rows) {
    hectares += field.kind == core::LandKind::kArable ? field.area_ga : 0.0F;
  }
  return hectares;
}

}  // namespace

int main() {
  int failures = 0;
  run::Simulation world = run::Start(kSeed);
  if (!world) {
    return 1;
  }
  run::AdvanceDays(*world, 1);

  const core::WorldState& morning = world.State();
  core::FieldId reserve;
  float reserve_hectares = 0.0F;
  std::uint32_t marked = 0;
  for (std::uint32_t row = 0; row < morning.fields.rows.size(); ++row) {
    if (morning.fields.rows[row].start_reserve != 0) {
      reserve = morning.fields.row_ids[row];
      reserve_hectares = morning.fields.rows[row].area_ga;
      ++marked;
    }
  }
  failures += run::Expect(marked == 1, "the start carries exactly one reserve field");
  if (marked != 1) {
    return failures;
  }
  const float arable_before = ArableHectares(morning);
  const std::size_t fields_before = morning.fields.rows.size();

  core::OrderRow order;
  order.kind = core::OrderKind::kRemoveField;
  order.field = reserve;
  world->StageOrders(std::span<const core::OrderRow>(&order, 1), {});

  // The outbox is cleared every step, so the event is looked for tick by tick.
  std::uint32_t removed = 0;
  bool names_the_reserve = false;
  for (std::uint32_t tick = 0; tick < core::kTicksPerDay; ++tick) {
    world->AdvanceStep();
    for (const core::SimEvent& event : world.State().step_events) {
      if (event.kind == core::EventKind::kFieldRemoved) {
        ++removed;
        names_the_reserve = event.field.value == reserve.value && event.amount == 1;
      }
    }
  }
  const core::WorldState& after = world.State();
  failures += run::Expect(removed == 1 && names_the_reserve,
                          "one kFieldRemoved names the reserve and carries its mark — the fact "
                          "start_reserve_field_removed has its door");
  failures += run::Expect(after.fields.rows.size() + 1 == fields_before &&
                              core::FindRow(after.fields, reserve) == core::kNoRow,
                          "the reserve's row is gone and no other field went with it");
  failures += run::Expect(ArableHectares(after) == arable_before - reserve_hectares,
                          "the arable is smaller by the reserve's hectares and nothing else");

  run::AdvanceYear(*world);
  const core::WorldState& year_on = world.State();
  failures += run::Expect(ArableHectares(year_on) == arable_before - reserve_hectares,
                          "a year later the arable has not lost or found another hectare");
  failures += run::Expect(!year_on.residents.rows.empty() && year_on.ledger.closed.year >= 1,
                          "and the village ran its first year without the reserve");
  std::cout << "reserve_field: seed " << kSeed << ", reserve " << reserve_hectares << " ha of "
            << arable_before << " ha arable; population after a year "
            << year_on.residents.rows.size() << '\n';

  std::cout << (failures == 0 ? "reserve_field: all checks passed\n"
                              : "reserve_field: FAILURES ABOVE\n");
  return failures;
}
