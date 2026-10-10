// The checks of a sown grass stand's age (grass_stand_checks.h).

#include "grass_stand_checks.h"

#include <cstdint>
#include <iostream>

#include "core_common/calendar.h"
#include "core_common/land_state.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "field_work.h"
#include "production_config.h"
#include "production_orders.h"

namespace {

int Expect(bool condition, const char* label) {
  if (!condition) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  return 0;
}

constexpr core::CropId kClover{0};
constexpr core::CropId kOat{1};
constexpr core::CropId kOldGrass{2};

/// Clover that lives by the stand's age (2 200 kg/ha, +4 a cut), oat, and a
/// perennial WITHOUT the mark — the pair of every rule. Econ's life: 0.7 /
/// 1.0 / 1.0 / 0.7 and 0.4 from the fifth summer, banking in summers 1–4.
core::ProductionConfig MakeConfig() {
  core::ProductionConfig config;
  config.farming.fertility_neutral = 50.0F;
  core::CropDef clover;
  clover.resource = core::ResourceId{0};
  clover.is_perennial = true;
  clover.stand_ages = true;
  clover.yield_kg_per_ha = 2200.0F;
  clover.fertility_delta = 4.0F;
  core::CropDef oat;
  oat.resource = core::ResourceId{1};
  oat.yield_kg_per_ha = 850.0F;
  core::CropDef old_grass = clover;
  old_grass.stand_ages = false;
  config.crops = {clover, oat, old_grass};
  config.farming.grass_stand = {{.yield_factor = 0.7F, .banks_fertility = true},
                                {.yield_factor = 1.0F, .banks_fertility = true},
                                {.yield_factor = 1.0F, .banks_fertility = true},
                                {.yield_factor = 0.7F, .banks_fertility = true},
                                {.yield_factor = 0.4F, .banks_fertility = false}};
  return config;
}

/// Ten hectares under `crop`, sown whole, at `fertility`, in the stand's `summer`.
core::FieldRow Stand(core::CropId crop, float fertility, std::uint8_t summer) {
  core::FieldRow field;
  field.area_ga = 10.0F;
  field.crop = crop;
  field.rotation_year0 = crop;
  field.rotation_year1 = crop;
  field.rotation_year2 = crop;
  field.rotation_assigned = 1;
  field.fertility = fertility;
  field.sown_share = 1.0F;
  field.stand_summers = summer;
  field.phase = core::FieldPhase::kGrowing;
  return field;
}

/// To the nearest kilogram: 0.7 of 22 t is a float's 15 399.999 or 15 400.0004.
long long Kilograms(core::Grams grams) {
  return (static_cast<long long>(grams) + 500) / 1000;
}

int CheckTheYield() {
  int failures = 0;
  const core::ProductionConfig config = MakeConfig();
  const auto yield = [&](core::CropId crop, float fertility, std::uint8_t summer) {
    const core::FieldRow field = Stand(crop, fertility, summer);
    return Kilograms(core::FieldYieldGrams(config, field, config.crops[crop.value]));
  };
  // The table's 22 000 kg of ten hectares, by the summer, on NEUTRAL ground.
  std::cout << "  grass stand, ten hectares of clover on neutral ground, kg by summer 1..6: "
            << yield(kClover, 50.0F, 1) << ' ' << yield(kClover, 50.0F, 2) << ' '
            << yield(kClover, 50.0F, 3) << ' ' << yield(kClover, 50.0F, 4) << ' '
            << yield(kClover, 50.0F, 5) << ' ' << yield(kClover, 50.0F, 6) << '\n';
  failures += Expect(yield(kClover, 50.0F, 1) == 15400 && yield(kClover, 50.0F, 2) == 22000 &&
                         yield(kClover, 50.0F, 3) == 22000 && yield(kClover, 50.0F, 4) == 15400,
                     "grass stand: the table's yield by the summer — 0.7, 1.0, 1.0, 0.7 of 22 t");
  failures += Expect(yield(kClover, 50.0F, 5) == 8800 && yield(kClover, 50.0F, 9) == 8800,
                     "grass stand: the fifth summer and every later one give 0.4 — the last row "
                     "stands for all after it");
  failures += Expect(yield(kClover, 50.0F, 0) == 15400,
                     "grass stand: a stand that carries no summer is read as a young one");
  // THE CEILING: rich ground does not lift a hay crop above the table; poor
  // ground lowers it. The pair: the same grass without the mark is lifted.
  failures += Expect(yield(kClover, 100.0F, 2) == 22000,
                     "grass stand: fertility 100 against a neutral 50 gives the table's 22 t and "
                     "not 44 — the table's yield is the hay crop's ceiling");
  failures += Expect(yield(kClover, 25.0F, 2) == 11000,
                     "grass stand: poor ground lowers it — half the fertility, half the hay");
  failures += Expect(yield(kOldGrass, 100.0F, 2) == 44000 && yield(kOldGrass, 50.0F, 5) == 22000,
                     "grass stand, the pair: a perennial without the mark is lifted by rich "
                     "ground and has no age");
  // No table: no age, and the ceiling still stands.
  core::ProductionConfig no_life = config;
  no_life.farming.grass_stand.clear();
  const core::FieldRow old = Stand(kClover, 100.0F, 7);
  failures += Expect(Kilograms(core::FieldYieldGrams(no_life, old, no_life.crops[0])) == 22000 &&
                         core::GrassStandOfSummer(no_life, old).banks_fertility,
                     "grass stand: with no grass_stand table every summer gives the table's "
                     "yield and banks");
  return failures;
}

int CheckTheCut() {
  int failures = 0;
  const core::ProductionConfig config = MakeConfig();
  const auto cut = [&](core::CropId crop, std::uint8_t summer) {
    core::WorldState world;
    core::FieldRow field = Stand(crop, 50.0F, summer);
    field.phase = core::FieldPhase::kHarvest;
    core::AppendRow(world.fields, field);
    core::FinishHarvest(config, world, world.fields.rows[0]);
    return world.fields.rows[0];
  };
  const core::FieldRow fourth = cut(kClover, 4);
  failures +=
      Expect(fourth.fertility == 54.0F && fourth.stand_summers == 5 &&
                 fourth.phase == core::FieldPhase::kGrowing && fourth.crop.value == kClover.value,
             "grass stand: the fourth summer's cut banks its +4 and the stand enters its "
             "fifth, still standing");
  const core::FieldRow fifth = cut(kClover, 5);
  failures += Expect(fifth.fertility == 50.0F && fifth.stand_summers == 6,
                     "grass stand: the fifth summer's cut banks nothing — a stand past its life "
                     "does not keep paying — and the count goes on");
  const core::FieldRow unmarked = cut(kOldGrass, 5);
  failures += Expect(unmarked.fertility == 54.0F && unmarked.stand_summers == 5,
                     "grass stand, the pair: a perennial without the mark banks at any age and "
                     "counts no summers");
  core::FieldRow old = cut(kClover, 255);
  failures += Expect(old.stand_summers == 255, "grass stand: the count saturates at 255");
  return failures;
}

int CheckTheRenewal() {
  int failures = 0;
  const core::ProductionConfig config = MakeConfig();
  const auto order = [&](core::CropId standing, core::FieldPhase phase) {
    core::WorldState world;
    core::FieldRow field = Stand(standing, 50.0F, 3);
    field.phase = phase;
    const core::FieldId id = core::AppendRow(world.fields, field);
    core::OrderRow rotation;
    rotation.kind = core::OrderKind::kSetRotation;
    rotation.field = id;
    rotation.rotation_year0 = kClover;
    rotation.rotation_year1 = kClover;
    rotation.rotation_year2 = kClover;
    core::AppendRow(world.orders, rotation);
    core::ConsumeProductionOrders(config, world);
    return world;
  };
  const core::WorldState renewed = order(kClover, core::FieldPhase::kGrowing);
  const core::FieldRow& ended = renewed.fields.rows[0];
  failures += Expect(renewed.orders.rows[0].status == core::OrderStatus::kDone &&
                         ended.crop.value == core::kInvalidDefIdValue &&
                         ended.last_crop.value == kClover.value &&
                         ended.phase == core::FieldPhase::kIdle && ended.stand_summers == 0 &&
                         core::HasRotation(ended) && ended.rotation_skips_turn == 1,
                     "grass stand: the rotation's order ends a growing stand at once — the field "
                     "idle, the grass its last crop, the chain fresh to plough and sow anew");
  const core::WorldState busy = order(kClover, core::FieldPhase::kHarvest);
  failures += Expect(busy.orders.rows[0].refusal == core::OrderRefusal::kConflictsWithActive &&
                         busy.fields.rows[0].crop.value == kClover.value &&
                         busy.fields.rows[0].stand_summers == 3,
                     "grass stand: while the stand is being cut the order is refused, not "
                     "queued, and nothing moves");
  const core::WorldState untouched = order(kOldGrass, core::FieldPhase::kGrowing);
  failures += Expect(untouched.orders.rows[0].status == core::OrderStatus::kDone &&
                         untouched.fields.rows[0].crop.value == kOldGrass.value &&
                         untouched.fields.rows[0].phase == core::FieldPhase::kGrowing,
                     "grass stand, the pair: a standing perennial without the mark is not ended "
                     "by the order — work already opened runs to its end");
  return failures;
}

int CheckTheView() {
  int failures = 0;
  const core::ProductionConfig config = MakeConfig();
  core::WorldState world;
  world.calendar.tick = (3U * core::kDaysPerYear + 10U) * core::kTicksPerDay;
  core::RefreshCalendarCaches(world.calendar);
  const core::FieldId clover = core::AppendRow(world.fields, Stand(kClover, 50.0F, 4));
  const core::FieldId oat = core::AppendRow(world.fields, Stand(kOat, 50.0F, 0));
  const core::GrassStandView standing = core::GrassStandOn(config, world, clover);
  failures += Expect(standing.stands && standing.summer == 4 && standing.summers_described == 5 &&
                         standing.yield_factor == 0.7F && standing.banks_fertility &&
                         !standing.being_cut && Kilograms(standing.hay_standing) == 15400,
                     "grass stand's view: the fourth summer of five described, factor 0.7, "
                     "banking, and 15.4 t standing to be lost by a renewal now");
  failures +=
      Expect(!core::GrassStandOn(config, world, oat).stands &&
                 !core::GrassStandOn(config, world, core::FieldId{9999}).stands,
             "grass stand's view: no stand on an oat field or on a field that is not there");
  // Cut this calendar year: nothing stands to be lost; and a stand being cut says so.
  world.fields.rows[0].last_cut_day = 3U * core::kDaysPerYear + 5U;
  failures += Expect(core::GrassStandOn(config, world, clover).hay_standing == 0,
                     "grass stand's view: after this year's cut nothing stands to be lost");
  world.fields.rows[0].last_cut_day = 2U * core::kDaysPerYear + 30U;
  world.fields.rows[0].phase = core::FieldPhase::kHarvest;
  const core::GrassStandView mowing = core::GrassStandOn(config, world, clover);
  failures += Expect(mowing.being_cut && Kilograms(mowing.hay_standing) == 15400,
                     "grass stand's view: last year's cut does not empty this year's stand, and "
                     "a stand being cut says so");
  return failures;
}

}  // namespace

int CheckTheGrassStandsAge() {
  int failures = 0;
  failures += CheckTheYield();
  failures += CheckTheCut();
  failures += CheckTheRenewal();
  failures += CheckTheView();
  return failures;
}
