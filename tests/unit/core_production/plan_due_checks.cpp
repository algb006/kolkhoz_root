// The checks of a plan position's due (plan_due_checks.h).
//
// THE KNOWN ANSWERS, by hand from econ's plan-by-sown-mix §2a and its grain-equivalent total
// (0.37.278; boss, the #1036 thread [9]): oat 1 000 kg/ha at 15 % of the worked ha, potato
// 9 000 kg/ha at 18 %, 100 worked ha, 28 % of the yield; a gram of oat is a gram of grain, a gram
// of potato 0.8 / 3.0 of one. Today's figures are 4 200 kg of oat and 45 360 kg of potato -
// 16 296 kg of grain. The plan sows 3.5 ha of oat and 14 of potato (a lean oat year):
// k = (1 000 x 15 + 2 400 x 18) / (1 000 x 3.5 + 2 400 x 14) = 58 200 / 37 100, so the oat is
// priced on 5.4906 ha (1 537.358 kg) and the potato on 21.9623 ha (55 344.906 kg) - the same
// 16 296 kg of grain.
#include "plan_due_checks.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
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
  config.limit.overfulfil_grain_kcal_per_gram = 3.0F;
  config.food_kcal_per_gram = {3.0F, 0.8F, 3.0F};
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

/// Grams of grain the dues are worth, by the config's own weights.
core::Grams GrainOf(const core::ProductionConfig& config,
                    std::initializer_list<std::pair<std::uint16_t, core::Grams>> dues) {
  double grain = 0.0;
  for (const auto& [resource, grams] : dues) {
    grain += static_cast<double>(grams) * core::GrainPerGram(config, core::ResourceId{resource});
  }
  return static_cast<core::Grams>(std::llround(grain));
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
  failures += Expect(oat_on < oat_off && potato_on > potato_off &&
                         Near(GrainOf(config, {{0, oat_on}, {1, potato_on}}),
                              GrainOf(config, {{0, oat_off}, {1, potato_off}})),
                     "a lean oat year asks less oat and more potato, the GRAIN owed unchanged");
  failures +=
      Expect(Near(oat_on, 1537358) && Near(potato_on, 55344906),
             "the split by hand: 1 537.358 kg of oat on 5.4906 ha, 55 344.906 kg of potato");

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
          Near(GrainOf(config, {{1, core::PlanDueGrams(config, no_oat, 1, 100.0F, 0, today)}}),
               GrainOf(config, {{0, oat_off}, {1, potato_off}})),
      "no oat planned: no oat owed, the whole grain on the potato");

  // NOTHING PLANNED AT ALL falls back to the fixed shares.
  const core::WorldState bare = PlanWorld();
  failures += Expect(core::PlanDueGrams(config, bare, 0, 100.0F, 0, today) == oat_off &&
                         core::PlanDueGrams(config, bare, 1, 100.0F, 0, today) == potato_off,
                     "with nothing planned the split falls back to the fixed shares");

  // 1933 y4's SHAPE: a winter rye position with no rye standing at the letter.
  // The split moves the rye's grain onto the crops that stand, the grain owed
  // stays the fixed shares', and the letter says it (kPlanWinterMoved) for
  // the rye's whole fixed figure.
  core::ProductionConfig rye_config = PlanConfig();
  core::CropDef rye;
  rye.resource = core::ResourceId{2};
  rye.yield_kg_per_ha = 1500.0F;
  rye.is_winter = true;
  rye.sow_from_month = 7;
  rye.sow_to_month = 8;
  rye_config.crops.push_back(rye);
  rye_config.plan_positions.push_back({.crop = core::CropId{2}, .area_share = 0.067F});
  rye_config.plan_split_by_sown_mix = true;
  core::WorldState no_rye = PlanWorld();
  no_rye.plan.due.assign(3, 0);
  no_rye.plan.delivered.assign(3, 0);
  AddField(no_rye, 3.5F, 0, 1);
  AddField(no_rye, 14.0F, 1, 0);
  const core::Grams rye_fixed =
      core::PlanPositionGrams(rye_config, rye_config.plan_positions[2], 100.0F);
  const core::Grams grain_fixed =
      GrainOf(rye_config,
              {{0, core::PlanPositionGrams(rye_config, rye_config.plan_positions[0], 100.0F)},
               {1, core::PlanPositionGrams(rye_config, rye_config.plan_positions[1], 100.0F)},
               {2, rye_fixed}});
  no_rye.step_events.clear();
  core::AnnouncePlan(rye_config, no_rye);
  const core::Grams grain_split =
      GrainOf(rye_config,
              {{0, core::AmountOf(no_rye.plan.due, core::ResourceId{0})},
               {1, core::AmountOf(no_rye.plan.due, core::ResourceId{1})},
               {2, core::AmountOf(no_rye.plan.due, core::ResourceId{2})}});
  bool said = false;
  for (const core::SimEvent& event : no_rye.step_events) {
    said = said || (event.kind == core::EventKind::kPlanWinterMoved && event.resource.value == 2 &&
                    Near(event.amount, rye_fixed));
  }
  failures += Expect(core::AmountOf(no_rye.plan.due, core::ResourceId{2}) == 0 &&
                         Near(grain_split, grain_fixed) && said,
                     "no rye in the ground: no rye owed, the grain owed the fixed shares' grain, "
                     "and the letter says the rye moved");

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
