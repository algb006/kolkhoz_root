// The checks of #1606's cure (heap_expected_checks.h).
//
// THE KNOWN ANSWERS, by hand from the shipped farming.csv (hay weathering 10 % a month in autumn,
// a month of 4 days: r = 0.025 a day): 100 t of hay on a field in September, hauled at 5 t a day
// (40 t since the cut began, 8 days), reaches the stores as (5 / 0.025) ln(1 + 0.025 x 100 / 5) =
// 200 ln 1.5 = 81.093 t. With no haul measured it counts whole (STUB); in July (a drying swath
// keeps) whole; with the switch off HerdFeedHeldKg counts the raw heap, with it on the 81.1 t.
#include "heap_expected_checks.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "herd_forecast.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: heaps at expected value: " << label << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int CheckTheHeapsCountAtExpectedValue() {
  int failures = 0;
  std::string error;
  const auto tables = core::LoadTableSet(KOLKHOZ_TABLES_DIR, &error);
  core::ProductionConfig config;
  if (Expect(tables != nullptr && core::ParseProductionConfig(*tables, config, error),
             "the shipped tables parse") != 0) {
    std::cout << error << '\n';
    return 1;
  }
  failures += Expect(!config.farming.hay_heaps_expected_value,
                     "the shipped tables leave the cure off (no world_params row)");
  constexpr core::Grams kHundred = 100'000'000;
  constexpr core::Grams kHauledSinceCut = 40'000'000;  // 8 days at 5 t
  const auto world_on = [&config](core::Month month, core::Grams hauled, std::uint32_t days_in) {
    core::WorldState world;
    const auto cut_from =
        static_cast<core::SimDay>(config.farming.meadow_cut_month) * core::kDaysPerMonth;
    world.calendar.day = cut_from + days_in;
    world.calendar.date.month = month;
    core::FieldRow field;
    field.kind = core::LandKind::kMeadow;
    field.reaped_grams = kHundred;
    field.reaped_resource = config.hay_resource;
    core::AppendRow(world.fields, field);
    world.ledger.current.hauled_to_stores.assign(config.hay_resource.value + 1U, 0);
    world.ledger.current.hauled_to_stores[config.hay_resource.value] = hauled;
    return world;
  };
  const core::Grams september =
      core::ExpectedHeapHayGrams(config, world_on(core::Month::kSeptember, kHauledSinceCut, 8));
  const double by_hand = 200.0 * std::log(1.5) * 1'000'000.0;  // grams
  std::cout << "  heaps at expected value, 100 t in September hauled 5 t a day: "
            << static_cast<double>(september) / 1e6 << " t (by hand " << by_hand / 1e6 << ")\n";
  failures += Expect(std::llabs(september - static_cast<core::Grams>(std::llround(by_hand))) < 1000,
                     "September: (R / r) ln(1 + r H / R) = 81.09 t of 100 t");
  failures += Expect(
      core::ExpectedHeapHayGrams(config, world_on(core::Month::kSeptember, 0, 8)) == kHundred,
      "no haul measured (none this year, none last): the whole heap, STUB");
  failures += Expect(core::ExpectedHeapHayGrams(
                         config, world_on(core::Month::kJuly, kHauledSinceCut, 8)) == kHundred,
                     "July, a drying swath keeps: the whole heap");
  const core::Grams faster =
      core::ExpectedHeapHayGrams(config, world_on(core::Month::kSeptember, 4 * kHauledSinceCut, 8));
  failures += Expect(faster > september && faster < kHundred,
                     "a faster haul saves more of the heap, never more than all of it");
  // THE SWITCH reaches the forecast's stock: off the raw heap, on the expected.
  const core::WorldState sept = world_on(core::Month::kSeptember, kHauledSinceCut, 8);
  const std::size_t hay = config.hay_resource.value;
  const float raw_kg = core::HerdFeedHeldKg(config, sept)[hay];
  core::ProductionConfig cured = config;
  cured.farming.hay_heaps_expected_value = true;
  const float cured_kg = core::HerdFeedHeldKg(cured, sept)[hay];
  failures += Expect(std::fabs(raw_kg - 100'000.0F) < 1.0F &&
                         std::fabs(cured_kg - static_cast<float>(september) / 1000.0F) < 1.0F,
                     "HerdFeedHeldKg: off counts 100 t, on counts the expected 81.1 t");
  return failures;
}
