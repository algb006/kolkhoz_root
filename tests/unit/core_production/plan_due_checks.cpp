// The checks of a plan position's due (plan_due_checks.h).
//
// THE KNOWN ANSWERS, by hand from econ's plan-by-sown-mix §2a: oat 1 000 kg/ha at 15 % of the
// worked ha, potato 9 000 kg/ha at 18 %, 100 worked ha, 28 % of the yield. Today's figures are
// 4 200 kg of oat and 45 360 kg of potato, 49 560 kg in all. The plan sows 3.5 ha of oat and 14
// of potato (a lean oat year): k = (1 000 x 15 + 9 000 x 18) / (1 000 x 3.5 + 9 000 x 14)
// = 177 000 / 129 500, so the oat is priced on 4.7838 ha (1 339.459 kg) and the potato on
// 19.1351 ha (48 220.541 kg) - the same 49 560 kg in all.
#include "plan_due_checks.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "district_plan.h"
#include "plan_alarms.h"
#include "plan_due.h"
#include "production_config.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: plan due: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::Grams kKilo = core::kGramsPerKilogram;

core::ProductionConfig PlanConfig() {
  core::ProductionConfig config;
  core::CropDef oat;
  oat.resource = core::ResourceId{0};
  oat.yield_kg_per_ha = 1000.0F;
  oat.sow_from_month = 4;
  oat.sow_to_month = 5;
  core::CropDef potato;
  potato.resource = core::ResourceId{1};
  potato.yield_kg_per_ha = 9000.0F;
  potato.sow_from_month = 5;
  potato.sow_to_month = 5;
  config.crops = {oat, potato};
  config.plan_grain_share = 0.28F;
  config.plan_positions = {{.crop = core::CropId{0}, .area_share = 0.15F},
                           {.crop = core::CropId{1}, .area_share = 0.18F}};
  return config;
}

/// A field whose chain grows `this_year` now and `next_year` after the turn.
void AddField(core::WorldState& world,
              float hectares,
              std::uint16_t this_year,
              std::uint16_t next_year) {
  core::FieldRow field;
  field.kind = core::LandKind::kArable;
  field.area_ga = hectares;
  field.rotation_assigned = 1;
  field.rotation_year0 = core::CropId{this_year};
  field.rotation_year1 = core::CropId{next_year};
  core::AppendRow(world.fields, field);
}

/// The second January: the letter is priced off the worked ha, not the stock.
core::WorldState PlanWorld() {
  core::WorldState world;
  world.calendar.day = core::kDaysPerYear;
  world.plan.worked_ha_last_year = 100.0F;
  world.plan.due.assign(2, 0);
  world.plan.delivered.assign(2, 0);
  world.chairman.raikom_reputation = 50.0F;
  return world;
}

/// Within a float's precision of the figure: one part in a million, and two
/// grams for the rounding (a 49-tonne total moves by a few grams in float).
bool Near(core::Grams value, core::Grams expected) {
  return std::llabs(value - expected) <= 2 + (std::llabs(expected) / 1000000);
}

}  // namespace

int CheckThePlanDue() {
  int failures = 0;
  core::ProductionConfig config = PlanConfig();
  core::WorldState world = PlanWorld();
  AddField(world, 3.5F, 0, 1);   // oat this year, potato next
  AddField(world, 14.0F, 1, 0);  // potato this year, oat next
  const core::SimDay today = world.calendar.day;
  const auto due = [&](std::size_t index, std::uint32_t year) {
    return core::PlanDueGrams(config, world, index, 100.0F, year, today);
  };

  // OFF, THE FIGURE IS TODAY'S TO THE GRAM, for the letter and the forecasts.
  const core::Grams oat_off = due(0, 0);
  const core::Grams potato_off = due(1, 0);
  failures +=
      Expect(oat_off == core::PlanPositionGrams(config, config.plan_positions[0], 100.0F) &&
                 potato_off == core::PlanPositionGrams(config, config.plan_positions[1], 100.0F) &&
                 due(0, 1) == oat_off && due(1, 2) == potato_off,
             "with the split and the band off every year's figure is today's fixed share");
  failures += Expect(Near(oat_off, 4200 * kKilo) && Near(potato_off, 45360 * kKilo),
                     "the fixed shares: 4 200 kg of oat and 45 360 kg of potato");

  // ON: THE LEAN OAT YEAR asks less oat, more potato, the same total.
  config.plan_split_by_sown_mix = true;
  const core::Grams oat_on = due(0, 0);
  const core::Grams potato_on = due(1, 0);
  std::cout << "plan due: off oat " << oat_off << " g, potato " << potato_off << " g; split oat "
            << oat_on << " g, potato " << potato_on << " g\n";
  failures += Expect(
      oat_on < oat_off && potato_on > potato_off && Near(oat_on + potato_on, oat_off + potato_off),
      "a lean oat year asks less oat and more potato, the total unchanged");
  failures +=
      Expect(Near(oat_on, 1339459) && Near(potato_on, 48220541),
             "the split by hand: 1 339.459 kg of oat on 4.7838 ha, 48 220.541 kg of potato");

  // THE FORECAST READS NEXT YEAR'S SLOTS: the fields swap, so next year is
  // the thick oat year, and next year's hold asks what the letter will.
  failures += Expect(due(0, 1) > oat_off && due(1, 1) < potato_off,
                     "next year's figure reads the chains' next slots: a thick oat year");
  const float next_area = core::NextPlanAreaHa(world, today);
  failures += Expect(core::NextPlanOwedGrams(config, world, core::ResourceId{0}, today) ==
                         core::PlanDueGrams(config, world, 0, next_area, 1, today),
                     "next year's hold prices the oat by the same home as the letter");

  // FIXED ON 1 JANUARY: the letter stores the figure; undersowing after it
  // changes no due.
  core::AnnouncePlan(config, world);
  const core::Grams announced = core::AmountOf(world.plan.due, core::ResourceId{0});
  world.fields.rows[0].rotation_year0 = core::CropId{};
  failures += Expect(
      Near(announced, oat_on) && core::AmountOf(world.plan.due, core::ResourceId{0}) == announced,
      "the letter names the split figure, and a chain withdrawn after it leaves "
      "the due as named");

  // A CROP THE PLAN DOES NOT SOW owes nothing, and the total moves onto the
  // rest (district.md, row 1199: the total stays on the worked ha).
  core::WorldState no_oat = PlanWorld();
  AddField(no_oat, 14.0F, 1, 1);
  failures += Expect(
      core::PlanDueGrams(config, no_oat, 0, 100.0F, 0, today) == 0 &&
          Near(core::PlanDueGrams(config, no_oat, 1, 100.0F, 0, today), oat_off + potato_off),
      "no oat planned: no oat owed, the whole total on the potato");

  // NOTHING PLANNED AT ALL falls back to the fixed shares.
  const core::WorldState bare = PlanWorld();
  failures += Expect(core::PlanDueGrams(config, bare, 0, 100.0F, 0, today) == oat_off &&
                         core::PlanDueGrams(config, bare, 1, 100.0F, 0, today) == potato_off,
                     "with nothing planned the split falls back to the fixed shares");

  // THE BAND multiplies the total once, by the rating today.
  config.plan_split_by_sown_mix = false;
  config.plan_band_factor = {0.9F, 1.0F, 1.0F, 1.1F, 1.2F};
  world.chairman.raikom_reputation = 70.0F;
  failures += Expect(Near(due(0, 0), 4620 * kKilo) && core::PlanBandFactor(config, world) == 1.1F,
                     "«на хорошем счету» (61-80): the oat's 4 200 kg asked x 1.1, once");
  world.chairman.raikom_reputation = 10.0F;
  failures += Expect(Near(due(1, 0), 40824 * kKilo),
                     "«на карандаше» (0-20): the potato's 45 360 kg eased x 0.9");
  world.chairman.raikom_reputation = 20.0F;
  failures += Expect(core::PlanBandFactor(config, world) == 0.9F,
                     "a rating of exactly 20 is still the bottom band");
  return failures;
}
