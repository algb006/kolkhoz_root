// Unit test of core_save: the round trip, the remap by key, and every
// refusal the format promises (manual/67-save-format.md).
//
// The round-trip criterion is byte equality of a RE-ENCODE: if
// Encode(Decode(Encode(w))) equals Encode(w), then every field the codec
// touches survived the trip exactly, floats included — and unlike a
// hand-written memberwise compare it cannot fall out of date with the row
// headers. What it deliberately does NOT cover is a field the codec never
// touches at all; that class is caught at compile time by the sizeof
// tripwires in save_rows.cpp, not here.
//
// The tables are synthesized into a temporary directory rather than taken
// from tables/: the remap test needs the SAME keys in a DIFFERENT order,
// which is exactly what a balance edit between two runs looks like.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/version.h"
#include "core_common/world_state.h"
#include "core_save/save.h"
#include "core_tables/tables.h"

namespace {

/// THE LAST VALUE OF EACH ORDER ENUM, derived rather than written out.
///
/// The codec range-checks each of these against kCount - 1 on the way in
/// (core_save/save_rows.cpp), so the row that exercises the bound has to
/// CARRY the bound: a value below the top passes the check even when the
/// bound has been left behind by an append. All three HAD been left behind —
/// the test said "the top of each enum" and named kDemolishUnit and
/// kNotEmpty, neither of which had been the top since task A7, and the
/// comment went on being believed.
///
/// Derived once and used both to write the row and to read it back. A
/// literal in either half would be the fact's second home, and the two
/// halves would part company on the next append without a word — which is
/// exactly what the first draft of this repair did.
constexpr core::OrderKind kTopOrderKind =
    static_cast<core::OrderKind>(static_cast<std::uint8_t>(core::OrderKind::kOrderKindCount) - 1);
constexpr core::OrderStatus kTopOrderStatus = static_cast<core::OrderStatus>(
    static_cast<std::uint8_t>(core::OrderStatus::kOrderStatusCount) - 1);
constexpr core::OrderRefusal kTopOrderRefusal = static_cast<core::OrderRefusal>(
    static_cast<std::uint8_t>(core::OrderRefusal::kOrderRefusalCount) - 1);

int Expect(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: " << label << '\n';
  return 1;
}

/// The six resources of the synthetic table set, in their default order.
const std::vector<std::string>& DefaultResources() {
  static const std::vector<std::string> kKeys = {
      "oat", "barley", "potato", "milk", "hay", "manure"};
  return kKeys;
}

void WriteTableFile(const std::filesystem::path& path,
                    const std::vector<std::string>& keys,
                    const char* extra_column) {
  std::ofstream out(path);
  out << "key," << extra_column << '\n';
  for (const std::string& key : keys) {
    out << key << ",1\n";
  }
}

/// Writes a table set holding exactly the four definition tables core_save
/// remaps by, with the resources in the given order.
void WriteTableSet(const std::filesystem::path& root, const std::vector<std::string>& resources) {
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  WriteTableFile(root / "resources.csv", resources, "edible");
  WriteTableFile(root / "crops.csv", {"rye", "potato"}, "yield_kg_per_ha");
  WriteTableFile(root / "unit_types.csv", {"barn", "house"}, "capacity_kg");
  WriteTableFile(root / "livestock.csv", {"cow", "goat"}, "feed_units_per_real_day");
  // The fifth dictionary (task A7): a post is a key like any other, and a
  // save that names one must find it again in a reshuffled roster.
  WriteTableFile(root / "professions.csv", {"groom", "storekeeper"}, "min_age");
  // The sixth (2026-09-13): the limit's lots, named by key in an order and on
  // a cart.
  WriteTableFile(root / "limit_catalog.csv", {"glass_container_lot", "roofing_lot"}, "points");
  // The seventh (save 82): the tree species, named by key in a planting
  // order and on a planting stand.
  WriteTableFile(root / "tree_species.csv", {"pine", "birch"}, "plantable");
}

core::ResourceAmounts Amounts(std::initializer_list<core::Grams> values) {
  return {values.begin(), values.end()};
}

/// A world with something interesting in every block: gaps in the id
/// sequence, dense vectors of three DIFFERENT lengths (empty, short, full),
/// a float that is not representable in decimal, and a ledger that is not
/// all zeros.
core::WorldState MakeWorld() {
  core::WorldState world;
  world.calendar.tick = (3U * core::kTicksPerDay) + 7U;
  world.calendar.day_zero_weekday = core::Weekday::kThursday;
  core::RefreshCalendarCaches(world.calendar);
  world.weather.air_temperature_celsius = -17.25F;
  world.weather.daylight_hours = 6.5F;
  world.weather.precipitation = core::Precipitation::kSnow;
  // The day's two NAMES (the wind parcel, 2026-09-05). Deliberately a pair
  // that cannot be re-derived from the numbers beside them: a blizzard is
  // snow plus a strong wind plus a temperature, and a codec that dropped
  // either field would still load a snowy day and look right.
  world.weather.phenomenon = core::WeatherPhenomenon::kBlizzard;
  world.weather.wind = core::WindBand::kStrongWind;
  // The sky step and its heavy phase (save format 56): the top of the enum,
  // and an hour and a length that are neither nought nor each other.
  world.weather.sky = core::SkyStep::kHeavyPrecipitation;
  world.weather.heavy_from_hour = 21;
  world.weather.heavy_hours = 7;
  // AND THE SNOW ON THE GROUND, which is the one weather field that cannot
  // be recomputed from (seed, day): lose it here and a loaded January shows
  // bare earth until the next snowfall. It was written and NOT read once,
  // and this test did not see it — the run suite did, four sections later,
  // when the epoch landed on two bytes of snow. A round trip that omits a
  // field is a round trip that certifies the fields it happens to name.
  world.weather.snow_cover_days = 9;
  // Set to the value that is NOT the default: a round trip that loses the
  // field would still read back `false` and pass on a default-shaped world.
  world.weather.cover_since_leaf_fall = true;
  world.weather.mud = true;  // not the default, for the same reason
  world.epoch = core::Epoch::kTwo;
  world.world_seed = 0x0BADC0FFEEULL;
  world.rng = core::SeedRngState(world.world_seed, 3);
  core::NextRandomBits(world.rng);
  world.chairman.raikom_reputation = 61.5F;
  world.chairman.horses_stabled = 1;  // the campaign's one-time milestone (task A7)
  world.chairman.ration_auto = 0;     // save 57: off, against the struct's default on
  // Save 65: a series of two cancelled days off, and the next one standing.
  world.chairman.days_off_cancelled_in_a_row = 2;
  world.chairman.cancelled_day_off = 55;
  world.chairman.last_talk_season = 5;  // save 69: a talk had, not nought
  // Save 77: away on a summons, a trip of his own before, a trade had.
  world.chairman.away_from_tick = 2'000;
  world.chairman.away_until_tick = 2'012;
  world.chairman.last_trip_day = 70;
  world.chairman.summon_letter_day = 81;
  world.chairman.summon_day = 83;
  world.chairman.plan_traded_year = 2;
  world.chairman.summon_cause = static_cast<std::uint8_t>(core::SummonCause::kOnThePencil);
  world.chairman.away_summoned = 1;
  world.chairman.pencil_pending = 1;  // save 81: the top, not the default
  world.plan.due = Amounts({7'000'000, 0, 0, 0, 0, 0});
  world.plan.delivered = Amounts({1'500'000, 0, 0});
  // The accumulation limit (save 62): not empty, or a codec that forgot it
  // would round-trip an empty vector perfectly.
  world.plan.accumulation_limit = Amounts({15'000'000, 0, 4'000'000});
  // The milk cart (save 66): a daily share, and the winter's milk that went
  // with no position — both away from their empty defaults.
  world.plan.milk_daily_share = 250'000;
  world.plan.milk_debt = 37'000;  // save 85, away from its default and from the share
  world.plan.delivered_outside = Amounts({0, 0, 3'000'000});
  // Save 89: the goods loan owed (the markup in it) and taken this year.
  world.plan.goods_loan_owed = Amounts({0, 1'500'000});
  world.plan.goods_loan_taken = Amounts({0, 1'250'000});
  // The district's verdict on the year and the two runs it keeps. Set to
  // three DIFFERENT values on purpose: equal ones would survive a codec that
  // wrote the same field three times.
  world.plan.last_verdict = core::PlanVerdict::kFailed;
  world.plan.failed_years_in_a_row = 2;
  // THE TWO THE NORM IS BUILT FROM (2026-09-13). The district has spoken this
  // year, and the area its next figure comes off is seventy hectares — last
  // year's, not today's. Lose the first on a round trip and a loaded campaign
  // cannot tell "asked for nothing" from "was never asked"; lose the second
  // and the spring after a load names a norm off nothing at all, which is the
  // plan that cannot be failed.
  world.plan.announced = 1;
  world.plan.worked_ha_last_year = 70.0F;
  world.plan.met_years_in_a_row = 5;
  world.vitals.life_expectancy_years = 61.75F;
  world.vitals.satiety_year_means = {71.5F, 68.25F, 0.1F + 0.2F};
  world.vitals.satiety_running_days = 19;

  core::ResidentRow first;
  first.sex = core::Sex::kMale;
  first.birth_day = -4321;  // an old-timer: born before day 0
  first.satiety = 0.1F + 0.2F;
  first.work.kind = core::WorkKind::kHarvest;
  first.education_stage = core::EducationStage::kVocational;
  first.social_status = core::SocialStatus::kKomsomol;
  first.night_trade = core::NightTrade::kHunter;  // the top of the enum, save format 39
  first.distiller_supplied_month = 7;             // save 59: not nought, so a lost read shows
  first.school = core::UnitId{6};                 // a pupil, save format 41
  first.days_worked_this_month = 3;               // the month's work, save format 43
  first.talk_until_day = 140;                     // talked into sport, save 69
  first.offense_count = 2;
  first.traits = 0xBEEF;
  // A post he HOLDS (task A7): the second half of the row that only the
  // fifth dictionary can move, and the pair the codec refuses to see broken.
  first.post.profession = core::ProfessionId{1};
  first.post.unit = core::UnitId{1};
  const core::ResidentId first_id = core::AppendRow(world.residents, first);

  core::ResidentRow second;
  second.sex = core::Sex::kFemale;
  second.birth_day = 12;
  second.mother = first_id;
  // A digger (save format 36): the NEWEST work kind, the one the bound in the
  // codec has to admit, and the site she digs at.
  second.work.kind = core::WorkKind::kExtraction;
  second.work.extraction_site = core::ExtractionSiteId{4};
  // Save 88: the placement's horse mark, 1 away from its nought. On a digger
  // it means nothing to the world (WorkRidesOut reads it for a carter only);
  // here it is the byte the codec must carry.
  second.work.rides_horse = 1;
  core::AppendRow(world.residents, second);
  core::ResidentRow third;
  const core::ResidentId third_id = core::AppendRow(world.residents, third);
  // Save 78: the first two are identical twins — each names the other, and
  // the mark is 1, away from its nought. Not the third: it dies below, and
  // the section carries the living (two of them — +10 bytes, not +15; the
  // prediction counted the appended rows, not the saved ones).
  world.residents.rows[0].twin = world.residents.row_ids[1];
  world.residents.rows[0].identical_twin = 1;
  world.residents.rows[1].twin = first_id;
  world.residents.rows[1].identical_twin = 1;
  // A death: the id is spent and must never be reissued, so next_id_value
  // has to survive the save on its own (state_table.h).
  core::RemoveRow(world.residents, third_id);

  core::FamilyRow rich;
  rich.pantry = Amounts({400'000, 0, 900'000, 60'000, 0, 0});
  rich.satiety_year_mean = 88.5F;
  rich.food_variety_mask = 0b1011;
  rich.first_meal_eaten = 1;  // `bare` below keeps 0: the pair a constant fails on
  rich.lost_house_position = core::Vec2{.x = 812.5F, .y = 9044.25F};  // save format 35
  rich.in_tent = 1;
  rich.ration_granted = 1;        // save 57; `bare` keeps 0
  rich.dry_months = 4;            // save 60, the yard's sobriety clock
  rich.overwork_penalty = 3.75F;  // save 65, the season's avrals and worked days off
  rich.household_hours = 4.25F;
  rich.plot_ratio_days = 27;
  rich.trudodni_account = 1234;
  rich.trudodni_redeemed = 567;
  core::AppendRow(world.families, rich);
  core::FamilyRow bare;  // empty pantry: the vector must stay empty
  core::AppendRow(world.families, bare);

  core::FieldRow field;
  field.center = core::Vec2{.x = 1234.5F, .y = -0.25F};
  field.area_ga = 7.75F;
  field.fertility = 65.0F;
  field.phase = core::FieldPhase::kGrowing;
  field.crop = core::CropId{1};
  field.rotation_year0 = core::CropId{0};
  field.rotation_year1 = core::CropId{1};
  field.last_crop = core::CropId{};  // invalid passes through untouched
  field.repeat_years = 2;
  // Both accumulators, both counters and the judgement: a round trip that
  // carries only one of them proves only one of them.
  field.drought_stress = 0.125F;
  field.wet_stress = 0.0625F;
  field.drought_run_days = 6;
  field.wet_run_days = 0;
  field.weather_state = core::FieldWeatherState::kDrying;
  field.rotation_assigned = 1;  // this one was told what to grow
  // ...and told in the autumn, so the coming year's turn leaves its chain
  // standing. Set on the SAME row as rotation_assigned on purpose: the two
  // bytes sit side by side in the row and in the stream, and a codec that
  // wrote one of them twice would pass a fixture that set only one.
  field.rotation_skips_turn = 1;
  // The harvest by parts (save 84): not nought, or a codec reading them into
  // the wrong member would round-trip the zeros perfectly.
  field.harvest_laid_share = 0.375F;
  field.harvest_laid_grams = 1'234'567;
  // The sown share (save 87), away from its default of 1 and from the laid
  // share beside it.
  field.sown_share = 0.625F;
  core::AppendRow(world.fields, field);

  // And one meadow: a different LandKind, so the byte the row gained in task
  // O2b travels through the codec too. Its sizeof did not move — the kind
  // went into padding the row already had — which is precisely why the round
  // trip has to carry a row of each kind (manual/67-save-format.md §7).
  core::FieldRow meadow;
  meadow.kind = core::LandKind::kMeadow;
  meadow.center = core::Vec2{.x = -900.0F, .y = 1100.0F};
  meadow.area_ga = 20.0F;
  meadow.phase = core::FieldPhase::kGrowing;
  core::AppendRow(world.fields, meadow);
  // The TOP of the land enum, so its bound is exercised, and the overgrown
  // byte set so the round trip carries it. The byte replaced a whole
  // LandKind value on 2026-09-12 — an overgrown field used to BE a kind of
  // land — so a row that sets one and not the other would test the shape
  // this delivery removed rather than the one it left.
  core::FieldRow overgrown;
  overgrown.kind = core::LandKind::kFloodplainMeadow;
  overgrown.overgrown = 1;
  overgrown.start_reserve = 1;  // the start quest's field
  overgrown.reaped_day = 39;    // reaped in the first October
  overgrown.rush_step = 3;      // save 65: an avral of +15 % on its harvest
  overgrown.rush_phase = core::FieldPhase::kHarvest;
  overgrown.rotation_assigned = 0;  // nobody has told this ground anything
  overgrown.area_ga = 45.0F;
  overgrown.fertility = 65.0F;
  core::AppendRow(world.fields, overgrown);

  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.position = core::Vec2{.x = 10.0F, .y = 20.0F};
  barn.stock = Amounts({12'000'000, 3'000'000, 0, 0, 165'000'000, 0});
  // Raising its next level: part of that stock is the works' own, and a
  // load that forgot it would hand the recipe back to the saw.
  barn.construction.phase = core::ConstructionPhase::kDelivering;
  barn.construction.target_level = 2;
  barn.construction.reserved = Amounts({0, 2'500'000, 0, 0, 0, 0});
  core::AppendRow(world.units, barn);
  core::UnitRow house;
  house.type = core::UnitTypeId{1};
  house.level = 2;
  // The three bytes that keep landing in the row's padding, set here so the
  // stream is what carries them: sizeof has stayed still for four
  // extensions running (67-save-format §7б), so the round trip is the only
  // guard that ever notices.
  house.paused = 1;
  house.dead = 1;
  house.insulated = 1;  // warm, save format 44
  // The top of the phase enum, being insulated (save format 44).
  house.construction.phase = core::ConstructionPhase::kInsulating;
  house.construction.target_level = house.level;
  house.wear = 70.0F;
  core::AppendRow(world.units, house);

  // And a construction site (task A2): a unit row at level 0 with its site
  // block filled. A campaign saved mid-build must resume mid-build — the
  // progress bar is state, not a screen.
  core::UnitRow site;
  site.type = core::UnitTypeId{0};
  site.position = core::Vec2{.x = 300.5F, .y = -12.25F};
  site.level = 0;
  site.stock = Amounts({0, 4'000'000, 0, 0, 0, 0});
  site.construction.phase = core::ConstructionPhase::kBuilding;
  site.construction.target_level = 1;
  site.construction.labor_days_total = 17.5F;
  site.construction.labor_days_remaining = 6.25F;
  site.construction.max_crew = 8;
  site.construction.rush_step = 5;     // save 65: the avral at its ceiling
  site.construction.winter_works = 0;  // save 80: away from its default of 1
  // Save 67: a store being emptied, its carrying half done — all three away
  // from their zero defaults.
  site.emptying = 1;
  site.haul_days_remaining = 2.5F;
  site.haul_days_written = 4.0F;
  core::AppendRow(world.units, site);

  core::HerdRow herd;
  herd.kind = core::LivestockKindId{0};
  herd.unit = core::UnitId{1};
  herd.adult_count = 39;
  herd.adult_male_count = 2;
  herd.billeted_count = 4;
  herd.adult_age_game_years_total = 137.5F;
  herd.adult_age_min_game_years = 1.25F;  // save 91: the band, away from its zero defaults
  herd.adult_age_max_game_years = 6.5F;
  herd.hunger_progress = 0.375F;
  herd.fed_share = 0.625F;         // save 71: a third short of the ration, not its default 1
  herd.autumn_slaughter_done = 1;  // save 76: this October's slaughter done
  core::AppendRow(world.herds, herd);

  // The chairman's order book (the boundary, manual/70-boundary.md §2): one
  // order still waiting, one already refused, and two that carry the parts
  // of the row nothing else reaches — the definition ids, which the remap
  // must move, and the position.
  core::OrderRow assign;
  assign.kind = core::OrderKind::kAssignWork;
  assign.status = core::OrderStatus::kAccepted;
  assign.work = core::WorkKind::kHarvest;
  assign.issued_tick = 71;
  assign.resident = first_id;
  assign.field = core::FieldId{1};
  core::AppendRow(world.orders, assign);

  core::OrderRow rotation;
  rotation.kind = core::OrderKind::kSetRotation;
  rotation.issued_tick = 72;
  rotation.field = core::FieldId{1};
  rotation.rotation_year0 = core::CropId{1};
  rotation.rotation_year1 = core::CropId{0};
  rotation.rotation_year2 = core::CropId{};  // invalid = fallow, passes through
  core::AppendRow(world.orders, rotation);

  core::OrderRow build;
  build.kind = core::OrderKind::kBuildUnit;
  build.issued_tick = 73;
  build.unit_type = core::UnitTypeId{1};
  build.position = core::Vec2{.x = -12.5F, .y = 0.125F};
  core::AppendRow(world.orders, build);

  core::OrderRow refused;
  // THE TOP OF EACH ENUM, and it has to BE the top, because the bound is what
  // is under test: the codec range-checks against kCount - 1 on the way in,
  // and a row carrying anything below the top exercises the check at a value
  // it would pass even if the bound had been left behind.
  //
  // ALL THREE HAD DRIFTED (UB-004, 2026-09-07). `kDemolishUnit` stopped being
  // the last OrderKind when task A7 appended the post orders, and
  // `kNotEmpty` stopped being the last OrderRefusal twice over. The comment
  // went on saying "the top of each enum" and was believed — which is the
  // whole trouble with a top written out by hand beside an enum that grows:
  // it is the one length in this codebase the counts did NOT abolish,
  // because it lives in a test rather than in the codec.
  //
  // Written as the enumerator before the sentinel, so that the next append
  // moves it by making this line a compile error rather than a lie.
  refused.kind = kTopOrderKind;
  refused.status = kTopOrderStatus;
  refused.refusal = kTopOrderRefusal;
  refused.issued_tick = 69;
  refused.unit = core::UnitId{1};
  refused.lot = core::LimitLotId{1};
  // AND THE SEX IS 1, NOT 0 (save 48). A byte that is nil in every witness
  // row is a byte a codec can forget to READ and still round-trip perfectly —
  // the writer's side changes the recorded hash, the reader's side changes
  // nothing at all. This is the same blind spot the world block had until
  // this morning, one field further down, and one non-zero value is the whole
  // cure.
  refused.male = 1;
  // The ration's switch (save 57): a family and a 1, for the same reason.
  refused.family = core::FamilyId{2};
  refused.enable = 1;
  core::AppendRow(world.orders, refused);

  // The district's limit (save format 31): the year's points, a cart on the
  // road carrying a lot's frozen goods, and the year's three flows.
  world.limit.points = 215;
  // Every point ever granted (save format 50). NOT a multiple of the year's
  // points and not equal to it: the two sit side by side in the record, and a
  // codec that read one where it meant the other would pass against any
  // fixture where they matched.
  world.limit.points_granted_total = 1265;
  core::LimitDeliveryRow cart;
  cart.lot = core::LimitLotId{0};
  cart.arrive_day = 131;
  cart.goods = Amounts({0, 0, 4'800'000});
  core::AppendRow(world.limit_deliveries, cart);

  // A head bought and still on its way (save format 48). EVERY FIELD IS
  // NON-DEFAULT, sex and stage included: a row of zeroes and first
  // enumerators would round-trip perfectly through a codec that read none of
  // them, which is the blind spot this whole fixture exists to close.
  core::LivestockArrivalRow bought;
  bought.lot = core::LimitLotId{1};
  // The SECOND kind of the fixture's two, not the first: the herd above uses
  // row 0, and a row that reused it would pass a codec that wrote a constant.
  bought.kind = core::LivestockKindId{1};
  bought.head_count = 3;
  bought.arrive_day = 133;
  bought.stage = core::LivestockArrivalStage::kYoung;
  bought.male = 1;
  core::AppendRow(world.livestock_arrivals, bought);

  // A specialist on the road (save format 34): the post through the
  // dictionary, the unit he is appointed to, the day he is due.
  core::SpecialistArrivalRow teacher;
  teacher.profession = core::ProfessionId{1};
  teacher.unit = core::UnitId{3};
  teacher.arrive_day = 97;
  core::AppendRow(world.specialist_arrivals, teacher);

  // A district visit on its way (save format 38): every field off its
  // default, so a field the codec forgets comes back as the default and fails.
  core::DistrictVisitRow visit;
  visit.arrive_day = 203;
  visit.face = core::DistrictFace::kPolushkina;
  visit.kind = core::DistrictVisitKind::kExtraordinary;
  visit.cause = core::DistrictVisitCause::kJuniorSignal;
  core::AppendRow(world.district_visits, visit);
  // Save 79: the ambulance at the yard for the first resident, every field
  // away from its default; and the second resident away in the hospital,
  // walking the last three hours in.
  core::DistrictCarRow car;
  car.kind = core::DistrictCarKind::kAmbulance;
  car.phase = core::DistrictCarPhase::kAtTheYard;
  car.resident = world.residents.row_ids[0];
  car.arrive_tick = 1'234;
  car.leave_tick = 1'236;
  core::AppendRow(world.district_cars, car);
  // Save 92: a road the player laid — its axis IS saved, unlike a map road's
  // — gravel, rare, two stretches of differing wear, a bridge mark.
  core::RoadRow road;
  road.kind = core::RoadKind::kRoad;
  road.surface = core::RoadSurface::kGravel;
  road.origin = core::RoadOrigin::kPlayer;
  road.removable = 1;
  road.traffic_word = core::RoadTrafficWord::kRare;
  road.axis = {
      core::RoadPoint{.position = {.x = 10.0F, .y = 20.0F}},
      core::RoadPoint{.position = {.x = 30.0F, .y = 20.0F}, .mark = core::RoadMark::kBridge},
      core::RoadPoint{.position = {.x = 45.0F, .y = 25.0F}}};
  road.stretches = {core::RoadStretch{.wear_pct = 12.5F}, core::RoadStretch{.wear_pct = 40.0F}};
  core::AppendRow(world.roads, road);
  world.residents.rows[1].away_until_day = 91;
  world.residents.rows[1].away_until_hour = 14;
  world.residents.rows[1].away_walk_hours = 3;
  world.residents.rows[1].away_reason = static_cast<std::uint8_t>(core::AwayReason::kHospital);

  // A night fisher out tonight (save format 39): every field off its default.
  core::NightOutingRow outing;
  outing.resident = core::ResidentId{5};
  outing.trade = core::NightTrade::kNetFisher;
  outing.day = 146;
  outing.position = core::Vec2{.x = 8650.0F, .y = 10160.0F};
  outing.hour_out = 23;
  outing.hour_back = 3;
  core::AppendRow(world.night_outings, outing);

  // A couple waiting for a free house (save format 35).
  core::WeddingWaitRow couple;
  couple.bride = core::ResidentId{7};
  couple.groom = core::ResidentId{9};
  couple.since_day = 211;
  core::AppendRow(world.wedding_waits, couple);

  // A clay pit half dug (save format 36): the resource through the
  // dictionary, a mark still standing, a load waiting for the carts.
  core::ExtractionSiteRow pit;
  pit.table_row = 3;
  pit.resource = core::ResourceId{2};
  pit.position = core::Vec2{.x = 7281.5F, .y = 8504.25F};
  pit.stock_grams = 3'900'000'000;
  pit.marked_grams = 20'000'000;
  pit.work_days_remaining = 0.75F;
  pit.load_grams = 6'000'000;
  pit.haul_days_remaining = 1.5F;
  pit.haul_days_written = 2.25F;
  pit.exhausted = 1;
  core::AppendRow(world.extraction_sites, pit);

  // A planting (save 82): birch, the second species, so a codec that wrote
  // the row index raw would come back pine after a reshuffle; planted and
  // growing, both days set.
  core::TimberStandRow planting;
  planting.table_row = 0xFFFFFFFFU;
  planting.kind = core::TimberStandKind::kPlanted;
  planting.position = core::Vec2{.x = 5120.5F, .y = 6400.25F};
  planting.species = core::TreeSpeciesId{1};
  planting.planted_area_ha = 2.5F;
  planting.planted_day = 97;
  planting.matures_day = 337;
  core::AppendRow(world.stands, planting);

  // An appointment still waiting (task A7): kAccepted is exactly the status
  // that has to survive a save — the order is visible, cancellable, and
  // takes effect at a day's close that may fall after the campaign is
  // reloaded. The top of the two enums travels with it.
  core::OrderRow appoint;
  appoint.kind = core::OrderKind::kDismiss;
  appoint.status = core::OrderStatus::kAccepted;
  appoint.refusal = core::OrderRefusal::kNoVacancy;
  appoint.issued_tick = 74;
  appoint.resident = first_id;
  appoint.unit = core::UnitId{1};
  appoint.profession = core::ProfessionId{0};
  core::AppendRow(world.orders, appoint);

  // The unsealing (kUnsealFund, 2026-09-12): the three fields that grew
  // OrderRow from 48 bytes to 64 and that the journal's hand-counted record
  // length had to be brought along for.
  core::OrderRow unseal;
  unseal.kind = core::OrderKind::kUnsealFund;
  unseal.status = core::OrderStatus::kDone;
  unseal.issued_tick = 91;
  unseal.fund = core::FundKind::kSeed;
  unseal.resource = core::ResourceId{2};
  unseal.amount = 640'000;
  core::AppendRow(world.orders, unseal);

  // And the release it left behind, which is world state of its own.
  world.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)] =
      Amounts({0, 0, 640'000});
  world.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kPlanReserve)] =
      Amounts({0, 310'000});

  world.ledger.closed.year = 2;
  world.ledger.closed.births = 6;
  world.ledger.closed.deaths = 3;
  world.ledger.closed.harvest = Amounts({43'000'000, 5'000'000, 0, 0, 0, 0});
  world.ledger.closed.eaten = Amounts({0, 0, 9'000'000, 1'200'000, 0, 0});
  // What the district asked (save 58, M12): not empty, or a codec that forgot
  // to read the column would round-trip it perfectly.
  world.ledger.closed.plan_due = Amounts({12'000'000, 0, 0});
  // What went against the position (save 83): not empty either — in BOTH
  // books, or a codec that swapped them would still round-trip.
  world.ledger.closed.plan_delivered = Amounts({11'800'000, 0, 0});
  world.ledger.current.plan_delivered = Amounts({5'000});
  // Save 86: the milk debt of each book, two different values, so a codec
  // that swapped the books could not round-trip them.
  world.ledger.closed.milk_debt = 200'000;
  world.ledger.current.milk_debt = 7'000;
  // Save 89: a closed year that paid its loan back; the rest stay empty.
  world.ledger.closed.goods_loan_repaid = Amounts({0, 0, 900'000});
  // Save 90: the autumn slaughter of the closed year, by kind — kind 1 of the
  // fixture's livestock; the other two removal columns empty.
  world.ledger.closed.herd_autumn_slaughtered = Amounts({0, 4});
  // The drink's price in kind (save 60): not empty either.
  world.ledger.closed.samogon_paid = Amounts({3'000, 5'000});
  // The standing crop the snow took (save 61): host's 150 t of potato.
  world.ledger.closed.lost_to_snow = Amounts({0, 150'000'000});
  // What the district seized above the limit (save 62).
  world.ledger.closed.seized = Amounts({5'000'000});
  world.ledger.closed.built_in = Amounts({0, 250});      // save 70: straw into a roof
  world.ledger.closed.yard_feed = Amounts({0, 0, 300});  // save 70: a goat's hay
  world.ledger.closed.processed = Amounts({0, 0, 400});  // save 73: cabbage pickled
  // Cell 2 and not 3: resource 3 is the key a later check removes as unused,
  // and a gram of it here made it used (2026-09-19, the first build of save 73).
  world.ledger.closed.made = Amounts({0, 0, 300});  // save 73: sauerkraut made
  // The season's reaping pace (save 63).
  world.ledger.closed.reaping_today = 3.25F;
  world.ledger.closed.reaping_last_day = 22.5F;
  // And the daylight of those two days (save 64).
  world.ledger.closed.reaping_today_daylight = 8.25F;
  world.ledger.closed.reaping_last_day_daylight = 15.5F;
  world.ledger.closed.work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kHarvest)] =
      241.5F;
  world.ledger.closed.trudodni_burned = 4200;
  // The office wall (ledger_state.h, Chronicle): three years, so that the
  // round trip proves the LENGTH and the ORDER and not just that one row
  // survives. A campaign is fifty years of these, and they are the one
  // history the simulation cannot rederive.
  world.ledger.chronicle = {
      {.year = 1, .residents = 80, .fertility = 55.5F, .harvest_kcal = 41'000'000},
      {.year = 2, .residents = 93, .fertility = 54.25F, .harvest_kcal = 48'500'000},
      {.year = 3, .residents = 88, .fertility = 56.0F, .harvest_kcal = 39'250'000},
  };
  world.ledger.current.year = 0;
  world.ledger.current.births = 1;
  world.ledger.current.limit_points_granted = 350;
  world.ledger.current.limit_points_spent = 135;
  world.ledger.current.limit_points_burned = 7;
  // The night trades' catch (save format 40): a vector of its own.
  world.ledger.current.night_catch = Amounts({0, 1'500, 5'000});
  // What the distillers stole, and their month at the stores (save format 42).
  world.ledger.current.stolen = Amounts({48'000, 0, 2'000});
  world.night_theft.stolen_this_month = 73'000;
  world.night_theft.month_index = 17;
  world.night_theft.complaint_raised = 1;
  // Save 60: the month's open leak, the distillers' vacancy, the settlement's
  // alcoholism — none at its default, so the round trip can tell each from a
  // field nobody wrote. (dry_months left for the yard, save 60.)
  world.night_theft.leak_open_this_month = 1;
  world.night_theft.distiller_short_since = 23;
  world.night_theft.settlement_alcoholism = 31.5F;
  // The sports field's month (save 68): both away from their zero defaults.
  world.sport_month.open_days = 3;
  world.sport_month.downpour_yesterday = 1;
  // The district MTS's column (save format 46): every field set apart.
  world.mts_column.phase = core::MtsColumnPhase::kWorking;
  world.mts_column.lot = core::LimitLotId{1};
  world.mts_column.arrive_day = 110;
  world.mts_column.camp = core::UnitId{9};
  world.mts_column.worked_ha = 32.5F;
  world.mts_column.field = core::FieldId{4};
  world.mts_column.field_ha = 7.5F;
  // The era events that have come (save format 50). Set, and the witness
  // below left at nought: a codec that skipped the read would hand back the
  // witness's own value and the round trip would pass on a field it never
  // carried.
  world.era_events.electrification_unlocked = 1;
  // Readiness for the transition (save format 52). Every field given a value
  // NO DEFAULT SHARES, for the same reason as the byte above: a codec that
  // skipped one of them would hand the witness's own number back and the
  // round trip would pass on a field it never carried. The two runs and the
  // six blocker bytes matter most — a run is what a save exists to carry.
  world.readiness.year = 7;
  world.readiness.economy.plan = {.score = 61.5F, .available = 1, .measured = 1};
  // ONE COMPONENT CARRIES THE OTHER COMBINATION, so the codec cannot pass by
  // writing `measured` from `available`: available and UNmeasured is the
  // state that was invisible until 2026-09-17.
  world.readiness.economy.winter_stocks = {.score = 0.0F, .available = 1, .measured = 0};
  world.readiness.economy.mechanisation = {.score = 12.0F, .available = 1, .measured = 1};
  world.readiness.economy.funds = {.score = 73.5F, .available = 1, .measured = 1};
  world.readiness.society.satisfaction = {.score = 54.75F, .available = 1, .measured = 1};
  world.readiness.society.kolkhoz_effort = {.score = 39.0F, .available = 1, .measured = 1};
  world.readiness.society.social_objects = {.score = 33.5F, .available = 1, .measured = 1};
  world.readiness.society.demography = {.score = 21.25F, .available = 0, .measured = 0};
  world.readiness.economic_index = 47.5F;
  world.readiness.social_index = 41.25F;
  world.readiness.both_above_run = 2;
  world.readiness.wintering_run = 1;
  world.readiness.plan_percent_years = {41.0F, 58.5F, 77.25F};
  world.readiness.plan_years_filled = 3;
  world.readiness.blocks.food_variety = 1;
  world.readiness.blocks.social_objects = 0;
  world.readiness.blocks.own_traction = 1;
  world.readiness.blocks.wintering_two_years = 0;
  world.readiness.blocks.units_at_level = 1;
  world.readiness.blocks.office_repaired = 0;
  world.readiness.satisfaction_stub_points = 45.0F;
  return world;
}

// -- THE POINT OUTSIDE THE CODEC --------------------------------------------
//
// THE ROUND TRIP ABOVE CANNOT SEE A DAMAGED CODEC, and that is not a flaw of
// how it is written: Encode(Decode(Encode(w))) and Encode(w) both come out of
// the SAME writer. A writer that puts a zero on the wire where a field
// belongs is read back as that zero and written again identically, so the two
// sides agree and the test is green. Damage D6 of 2026-09-15 proved it — the
// MTS column's hectares were written as zero and only the one assertion that
// NAMES the field went red; the two re-encodes agreed with each other all the
// way down.
//
// Boss named the shape on 2026-09-16: "две согласные стороны из трёх — это и
// есть «зелёное, потому что вопроса не задавали»". So the two checks below
// ask the codec nothing:
//
//   * the WITNESS WORLD's world block is written out here BY HAND, field by
//     field, from the byte layout documented in core_save/save.h — the values
//     come from the witness struct, the ORDER, the WIDTHS and the PRESENCE of
//     every field come from the document. A field written in the wrong width,
//     in the wrong order, from the wrong member, or not written at all, is a
//     difference at a named byte;
//   * the FIXTURE's whole payload carries a RECORDED size and hash, so that a
//     field written differently in a row — which the hand-written block above
//     does not reach — cannot pass unnoticed either. The number is recorded,
//     not derived: changing it is a deliberate act with a VERSION_SAVE bump
//     beside it.

/// One named piece of the expected stream. The name is what a failure prints.
struct Chunk {
  const char* name;
  std::vector<std::uint8_t> bytes;
};

/// Little-endian in `width` bytes — the format's one integer rule
/// (core_save/save.h, "ENCODING RULES"), written out here rather than taken
/// from the codec's own ByteWriter, which is the whole point of the exercise.
std::vector<std::uint8_t> Little(std::uint64_t value, std::size_t width) {
  std::vector<std::uint8_t> bytes(width, 0);
  for (std::size_t index = 0; index < width; ++index) {
    bytes[index] = static_cast<std::uint8_t>((value >> (8U * index)) & 0xFFU);
  }
  return bytes;
}

std::vector<std::uint8_t> U8(std::uint8_t value) {
  return Little(value, 1);
}

std::vector<std::uint8_t> U16(std::uint16_t value) {
  return Little(value, 2);
}

std::vector<std::uint8_t> U32(std::uint32_t value) {
  return Little(value, 4);
}

std::vector<std::uint8_t> U64(std::uint64_t value) {
  return Little(value, 8);
}

/// A float is its IEEE-754 bit pattern in four bytes, never text.
std::vector<std::uint8_t> F32(float value) {
  return Little(std::bit_cast<std::uint32_t>(value), 4);
}

/// An enum is its underlying integer, and every enum of the world block is a
/// byte wide.
template <typename EnumT>
std::vector<std::uint8_t> Enum8(EnumT value) {
  return U8(static_cast<std::uint8_t>(value));
}

/// ResourceAmounts: a u16 count, then that many i64 grams.
void AppendAmounts(std::vector<Chunk>& chunks,
                   const char* name,
                   const core::ResourceAmounts& amounts) {
  chunks.push_back({name, U16(static_cast<std::uint16_t>(amounts.size()))});
  for (const core::Grams grams : amounts) {
    chunks.push_back({name, Little(static_cast<std::uint64_t>(grams), 8)});
  }
}

/// A world whose every world-block field carries a value of its own, none of
/// them the field's default: a witness that leaves a codec nowhere to write
/// the right bytes from the wrong member. It has no rows at all — the rows
/// are the recorded payload's business below.
core::WorldState MakeWitnessWorld() {
  core::WorldState witness;
  // The calendar's caches are written out with the rest and re-derived on
  // load; here they are simply four more fields with four more values.
  witness.calendar.tick = 79;
  witness.calendar.day = 3;
  witness.calendar.date.year = 1930;
  witness.calendar.date.month = core::Month::kApril;
  witness.calendar.date.day_in_month = 2;
  witness.calendar.weekday = core::Weekday::kFriday;
  witness.calendar.season = core::Season::kSummer;
  witness.calendar.day_zero_weekday = core::Weekday::kThursday;

  witness.weather.air_temperature_celsius = -17.25F;
  witness.weather.daylight_hours = 6.5F;
  witness.weather.precipitation = core::Precipitation::kSnow;
  witness.weather.temperature_swing_celsius = 3.75F;
  witness.weather.cloud_cover = 0.25F;
  witness.weather.phenomenon = core::WeatherPhenomenon::kBlizzard;
  witness.weather.wind = core::WindBand::kStrongWind;
  witness.weather.sky = core::SkyStep::kHeavyPrecipitation;
  witness.weather.heavy_from_hour = 5;
  witness.weather.heavy_hours = 11;
  witness.weather.snow_cover_days = 9;
  witness.weather.cover_since_leaf_fall = true;
  witness.weather.mud = true;

  witness.epoch = core::Epoch::kTwo;
  witness.world_seed = 0x0BADC0FFEEULL;
  witness.rng.state = 0x1234567890ABCDEFULL;
  witness.rng.stream = 7;

  witness.chairman.raikom_reputation = 61.5F;
  witness.chairman.authority = 12.25F;
  witness.chairman.shadow_reputation = 3.5F;
  witness.chairman.horses_stabled = 1;
  // NON-DEFAULT, all four: a standing order, a first night already had, and a
  // camp at a place no default would produce. A witness carrying zeroes here
  // would round-trip perfectly through a codec that read none of them.
  witness.chairman.night_pasture_ordered = 1;
  witness.chairman.night_pasture_begun = 1;
  witness.chairman.night_pasture_place = core::Vec2{.x = 8140.5F, .y = 10312.25F};
  // The ration's checkbox OFF (save 57): the struct's default is on, so a
  // codec that forgot to read it would load a 1 and fail here.
  witness.chairman.ration_auto = 0;
  // The cancelled day off (save 65): both away from their zero defaults.
  witness.chairman.days_off_cancelled_in_a_row = 2;
  witness.chairman.cancelled_day_off = 55;
  // The chairman's talk (save 69): the last season away from nought.
  witness.chairman.last_talk_season = 5;
  // The trip to the district (save 77): every field away from nought.
  witness.chairman.away_from_tick = 2'000;
  witness.chairman.away_until_tick = 2'012;
  witness.chairman.last_trip_day = 70;
  witness.chairman.summon_letter_day = 81;
  witness.chairman.summon_day = 83;
  witness.chairman.plan_traded_year = 2;
  witness.chairman.summon_cause = static_cast<std::uint8_t>(core::SummonCause::kOnThePencil);
  witness.chairman.away_summoned = 1;
  witness.chairman.pencil_pending = 1;

  witness.traction_ration = 0.75F;
  // The chairman's issue norms (save 57): NOT empty, since empty is what a
  // codec that forgot them would read back.
  witness.issue_norms = {500, 0, 1'500};
  witness.plan.due = Amounts({7'000'000, 250, 3});
  witness.plan.delivered = Amounts({11});
  witness.plan.accumulation_limit = Amounts({0, 21'000'000});
  witness.plan.milk_daily_share = 180'000;
  witness.plan.milk_debt = 9'000;
  witness.plan.delivered_outside = Amounts({0, 500'000});
  witness.plan.goods_loan_owed = Amounts({0, 0, 60'000});
  witness.plan.goods_loan_taken = Amounts({50'000});
  witness.plan.last_verdict = core::PlanVerdict::kFailed;
  witness.plan.failed_years_in_a_row = 2;
  witness.plan.met_years_in_a_row = 5;
  witness.plan.announced = 1;
  witness.plan.worked_ha_last_year = 70.0F;
  witness.plan.worked_ha_this_year = 12.5F;
  // One fund opened and the others not: the array is written whole, and a
  // codec that wrote one fund three times would still fill the same bytes.
  witness.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)] = Amounts({0, 1500});

  witness.vitals.life_expectancy_years = 61.75F;
  witness.vitals.satiety_year_means = {71.5F, 68.25F, 0.5F};
  witness.vitals.satiety_running_sum = 123.25F;
  witness.vitals.satiety_running_days = 19;

  witness.limit.points = 380;
  witness.limit.points_granted_total = 977;

  witness.night_theft.stolen_this_month = 4500;
  witness.night_theft.month_index = 17;
  witness.night_theft.complaint_raised = 1;
  witness.night_theft.leak_open_this_month = 1;
  witness.night_theft.distiller_short_since = 29;
  witness.night_theft.settlement_alcoholism = 12.25F;
  witness.sport_month.open_days = 2;
  witness.sport_month.downpour_yesterday = 1;

  witness.mts_column.phase = core::MtsColumnPhase::kWorking;
  witness.mts_column.lot = core::LimitLotId{1};
  witness.mts_column.arrive_day = 110;
  witness.mts_column.camp = core::UnitId{9};
  witness.mts_column.worked_ha = 32.5F;
  witness.mts_column.field = core::FieldId{4};
  witness.mts_column.field_ha = 7.5F;
  return witness;
}

/// The world block as core_save/save.h says it is laid out: this list is the
/// document, and it is read by nothing that writes a save.
std::vector<Chunk> ExpectedWorldBlock(const core::WorldState& world) {
  std::vector<Chunk> chunks;
  chunks.push_back({"calendar.tick", U64(world.calendar.tick)});
  chunks.push_back({"calendar.day", U32(world.calendar.day)});
  chunks.push_back({"calendar.date.year", U16(world.calendar.date.year)});
  chunks.push_back({"calendar.date.month", Enum8(world.calendar.date.month)});
  chunks.push_back({"calendar.date.day_in_month", U8(world.calendar.date.day_in_month)});
  chunks.push_back({"calendar.weekday", Enum8(world.calendar.weekday)});
  chunks.push_back({"calendar.season", Enum8(world.calendar.season)});
  chunks.push_back({"calendar.day_zero_weekday", Enum8(world.calendar.day_zero_weekday)});

  chunks.push_back({"weather.air_temperature_celsius", F32(world.weather.air_temperature_celsius)});
  chunks.push_back({"weather.daylight_hours", F32(world.weather.daylight_hours)});
  chunks.push_back({"weather.precipitation", Enum8(world.weather.precipitation)});
  chunks.push_back(
      {"weather.temperature_swing_celsius", F32(world.weather.temperature_swing_celsius)});
  chunks.push_back({"weather.cloud_cover", F32(world.weather.cloud_cover)});
  chunks.push_back({"weather.sky", Enum8(world.weather.sky)});
  chunks.push_back({"weather.heavy_from_hour", U8(world.weather.heavy_from_hour)});
  chunks.push_back({"weather.heavy_hours", U8(world.weather.heavy_hours)});
  chunks.push_back({"weather.phenomenon", Enum8(world.weather.phenomenon)});
  chunks.push_back({"weather.wind", Enum8(world.weather.wind)});
  chunks.push_back({"weather.snow_cover_days", U16(world.weather.snow_cover_days)});
  chunks.push_back(
      {"weather.cover_since_leaf_fall", U8(world.weather.cover_since_leaf_fall ? 1U : 0U)});
  chunks.push_back({"weather.mud", U8(world.weather.mud ? 1U : 0U)});

  chunks.push_back({"epoch", Enum8(world.epoch)});
  chunks.push_back({"world_seed", U64(world.world_seed)});
  chunks.push_back({"rng.state", U64(world.rng.state)});
  chunks.push_back({"rng.stream", U64(world.rng.stream)});

  chunks.push_back({"chairman.raikom_reputation", F32(world.chairman.raikom_reputation)});
  chunks.push_back({"chairman.authority", F32(world.chairman.authority)});
  chunks.push_back({"chairman.shadow_reputation", F32(world.chairman.shadow_reputation)});
  chunks.push_back({"chairman.horses_stabled", U8(world.chairman.horses_stabled)});
  // The night pasture (save 49): the standing order, whether it has ever
  // begun, and the camp the children keep.
  chunks.push_back({"chairman.night_pasture_ordered", U8(world.chairman.night_pasture_ordered)});
  chunks.push_back({"chairman.night_pasture_begun", U8(world.chairman.night_pasture_begun)});
  chunks.push_back({"chairman.night_pasture_place.x", F32(world.chairman.night_pasture_place.x)});
  chunks.push_back({"chairman.night_pasture_place.y", F32(world.chairman.night_pasture_place.y)});
  // The ration's checkbox (save 57).
  chunks.push_back({"chairman.ration_auto", U8(world.chairman.ration_auto)});
  // The cancelled day off (save 65): the series, then the day.
  chunks.push_back(
      {"chairman.days_off_cancelled_in_a_row", U8(world.chairman.days_off_cancelled_in_a_row)});
  chunks.push_back({"chairman.cancelled_day_off", U32(world.chairman.cancelled_day_off)});
  chunks.push_back({"chairman.last_talk_season", U32(world.chairman.last_talk_season)});
  // The trip to the district (save 77).
  chunks.push_back({"chairman.away_from_tick", U64(world.chairman.away_from_tick)});
  chunks.push_back({"chairman.away_until_tick", U64(world.chairman.away_until_tick)});
  chunks.push_back({"chairman.last_trip_day", U32(world.chairman.last_trip_day)});
  chunks.push_back({"chairman.summon_letter_day", U32(world.chairman.summon_letter_day)});
  chunks.push_back({"chairman.summon_day", U32(world.chairman.summon_day)});
  chunks.push_back({"chairman.plan_traded_year", U16(world.chairman.plan_traded_year)});
  chunks.push_back({"chairman.summon_cause", U8(world.chairman.summon_cause)});
  chunks.push_back({"chairman.away_summoned", U8(world.chairman.away_summoned)});
  chunks.push_back({"chairman.pencil_pending", U8(world.chairman.pencil_pending)});

  chunks.push_back({"traction_ration", F32(world.traction_ration)});
  AppendAmounts(chunks, "issue_norms", world.issue_norms);
  AppendAmounts(chunks, "plan.due", world.plan.due);
  AppendAmounts(chunks, "plan.delivered", world.plan.delivered);
  AppendAmounts(chunks, "plan.accumulation_limit", world.plan.accumulation_limit);  // save 62
  // The milk cart's daily share and the deliveries outside any position (save 66).
  chunks.push_back(
      {"plan.milk_daily_share", U64(static_cast<std::uint64_t>(world.plan.milk_daily_share))});
  chunks.push_back({"plan.milk_debt", U64(static_cast<std::uint64_t>(world.plan.milk_debt))});
  AppendAmounts(chunks, "plan.delivered_outside", world.plan.delivered_outside);
  AppendAmounts(chunks, "plan.goods_loan_owed", world.plan.goods_loan_owed);    // save 89
  AppendAmounts(chunks, "plan.goods_loan_taken", world.plan.goods_loan_taken);  // save 89
  chunks.push_back({"plan.last_verdict", Enum8(world.plan.last_verdict)});
  chunks.push_back({"plan.failed_years_in_a_row", U8(world.plan.failed_years_in_a_row)});
  chunks.push_back({"plan.met_years_in_a_row", U8(world.plan.met_years_in_a_row)});
  chunks.push_back({"plan.announced", U8(world.plan.announced)});
  chunks.push_back({"plan.worked_ha_last_year", F32(world.plan.worked_ha_last_year)});
  chunks.push_back({"plan.worked_ha_this_year", F32(world.plan.worked_ha_this_year)});
  for (const core::ResourceAmounts& opened : world.unsealed.by_fund) {
    AppendAmounts(chunks, "unsealed.by_fund", opened);
  }

  chunks.push_back({"vitals.life_expectancy_years", F32(world.vitals.life_expectancy_years)});
  for (const float mean : world.vitals.satiety_year_means) {
    chunks.push_back({"vitals.satiety_year_means", F32(mean)});
  }
  chunks.push_back({"vitals.satiety_running_sum", F32(world.vitals.satiety_running_sum)});
  chunks.push_back({"vitals.satiety_running_days", U32(world.vitals.satiety_running_days)});

  chunks.push_back({"limit.points", U32(static_cast<std::uint32_t>(world.limit.points))});
  chunks.push_back({"limit.points_granted_total",
                    U32(static_cast<std::uint32_t>(world.limit.points_granted_total))});

  chunks.push_back({"night_theft.stolen_this_month",
                    Little(static_cast<std::uint64_t>(world.night_theft.stolen_this_month), 8)});
  chunks.push_back({"night_theft.month_index", U32(world.night_theft.month_index)});
  chunks.push_back({"night_theft.complaint_raised", U8(world.night_theft.complaint_raised)});
  chunks.push_back(
      {"night_theft.leak_open_this_month", U8(world.night_theft.leak_open_this_month)});
  chunks.push_back(
      {"night_theft.distiller_short_since", U32(world.night_theft.distiller_short_since)});
  chunks.push_back(
      {"night_theft.settlement_alcoholism", F32(world.night_theft.settlement_alcoholism)});
  // The sports field's month (save 68).
  chunks.push_back({"sport_month.open_days", U8(world.sport_month.open_days)});
  chunks.push_back({"sport_month.downpour_yesterday", U8(world.sport_month.downpour_yesterday)});

  chunks.push_back({"mts_column.phase", Enum8(world.mts_column.phase)});
  chunks.push_back({"mts_column.lot", U16(world.mts_column.lot.value)});
  chunks.push_back({"mts_column.arrive_day", U32(world.mts_column.arrive_day)});
  chunks.push_back({"mts_column.camp", U32(world.mts_column.camp.value)});
  chunks.push_back({"mts_column.worked_ha", F32(world.mts_column.worked_ha)});
  chunks.push_back({"mts_column.field", U32(world.mts_column.field.value)});
  chunks.push_back({"mts_column.field_ha", F32(world.mts_column.field_ha)});
  chunks.push_back(
      {"era_events.electrification_unlocked", U8(world.era_events.electrification_unlocked)});

  // Readiness (save format 52). Written out component by component rather
  // than by a loop over the struct, which is the whole point of this list: a
  // loop here would share the codec's own idea of the order, and a side built
  // out of the thing it measures can only ever find disagreements with
  // itself.
  const auto component = [&chunks](const char* name, const core::ReadinessComponent& value) {
    chunks.push_back({name, F32(value.score)});
    chunks.push_back({name, U8(value.available)});
    chunks.push_back({name, U8(value.measured)});
  };
  chunks.push_back({"readiness.year", U16(world.readiness.year)});
  component("readiness.economy.plan", world.readiness.economy.plan);
  component("readiness.economy.winter_stocks", world.readiness.economy.winter_stocks);
  component("readiness.economy.mechanisation", world.readiness.economy.mechanisation);
  component("readiness.economy.funds", world.readiness.economy.funds);
  component("readiness.society.satisfaction", world.readiness.society.satisfaction);
  component("readiness.society.kolkhoz_effort", world.readiness.society.kolkhoz_effort);
  component("readiness.society.social_objects", world.readiness.society.social_objects);
  component("readiness.society.demography", world.readiness.society.demography);
  chunks.push_back({"readiness.economic_index", F32(world.readiness.economic_index)});
  chunks.push_back({"readiness.social_index", F32(world.readiness.social_index)});
  chunks.push_back({"readiness.both_above_run", U8(world.readiness.both_above_run)});
  chunks.push_back({"readiness.wintering_run", U8(world.readiness.wintering_run)});
  for (const float year : world.readiness.plan_percent_years) {
    chunks.push_back({"readiness.plan_percent_years", F32(year)});
  }
  chunks.push_back({"readiness.plan_years_filled", U8(world.readiness.plan_years_filled)});
  chunks.push_back({"readiness.blocks.food_variety", U8(world.readiness.blocks.food_variety)});
  chunks.push_back({"readiness.blocks.social_objects", U8(world.readiness.blocks.social_objects)});
  chunks.push_back({"readiness.blocks.own_traction", U8(world.readiness.blocks.own_traction)});
  chunks.push_back(
      {"readiness.blocks.wintering_two_years", U8(world.readiness.blocks.wintering_two_years)});
  chunks.push_back({"readiness.blocks.units_at_level", U8(world.readiness.blocks.units_at_level)});
  chunks.push_back(
      {"readiness.blocks.office_repaired", U8(world.readiness.blocks.office_repaired)});
  chunks.push_back(
      {"readiness.satisfaction_stub_points", F32(world.readiness.satisfaction_stub_points)});
  return chunks;
}

/// Reads one u64 of the stream by hand, for positioning only.
std::uint64_t LittleAt(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
             << (8U * index);
  }
  return value;
}

/// Where section `index` of the payload begins and how long it is: the
/// sections are length-prefixed and in a fixed order (core_save/save.h), so
/// walking the lengths reaches any of them without reading a field.
/// @return the offset of the section's first byte, or 0 when the file is too
///         short; `length` takes the section's size.
std::size_t SectionAt(std::span<const std::byte> save, int index, std::uint64_t& length) {
  std::size_t offset = core::kSaveHeaderSize;
  for (int section = 0; section <= index; ++section) {
    if (save.size() < offset + 8) {
      return 0;
    }
    const std::uint64_t here = LittleAt(save, offset);
    offset += 8;
    if (save.size() - offset < here) {
      return 0;
    }
    if (section == index) {
      length = here;
      return offset;
    }
    offset += static_cast<std::size_t>(here);
  }
  return 0;
}

/// The world block out of a save. The payload's sections are length-prefixed
/// and in a fixed order (core_save/save.h), and the world block is the second
/// of them — positioning by two lengths is not decoding: no field is read.
std::span<const std::byte> WorldSection(std::span<const std::byte> save) {
  std::size_t offset = core::kSaveHeaderSize;
  for (int section = 0; section < 2; ++section) {
    if (save.size() < offset + 8) {
      return {};
    }
    const std::uint64_t length = LittleAt(save, offset);
    offset += 8;
    if (save.size() - offset < length) {
      return {};
    }
    if (section == 1) {
      return save.subspan(offset, static_cast<std::size_t>(length));
    }
    offset += static_cast<std::size_t>(length);
  }
  return {};
}

/// FNV-1a 64, the arithmetic the header's own hash uses — here to turn a
/// payload into ONE number that can be recorded and compared.
std::uint64_t Fnv1a64(std::span<const std::byte> bytes) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (const std::byte byte : bytes) {
    hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byte));
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

/// One section of the payload as it was recorded: its length and its hash.
struct RecordedSection {
  const char* name;
  std::size_t bytes;
  std::uint64_t hash;
};

/// THE FIXTURE'S PAYLOAD, SECTION BY SECTION, recorded on 2026-09-16 at save
/// format 47 — the world of MakeWorld() encoded against the synthetic table
/// set of this file. It is a WITNESS, not a second codec: it says "this many
/// bytes, hashing to this", and nothing about what any of them mean.
///
/// SECTION BY SECTION AND NOT AS ONE NUMBER, at boss's asking (2026-09-16):
/// one number says "something changed" and leaves the search everywhere,
/// while the sections are named in the format itself — one per state table —
/// so a failure says "in 'fields', which begins at payload byte N". That is
/// most of the use of a hand-written row layout at none of its cost, and the
/// cost was the point: a hand-written layout for every row would be a second
/// home for the contract, which is the disease and not the cure.
///
/// WHAT IT CANNOT DO, plainly: a recorded number catches a field that STARTS
/// being written differently. A field that was wrong from its first day is
/// wrong in the recording too. For the world block there is a second path
/// (ExpectedWorldBlock above); for the rows there is not, and boss took that
/// price knowingly — a row carries names and keys, and a wrong one breaks
/// loudly, at the first load that cannot find its key.
///
/// WHEN IT GOES RED the question is which of two things happened. A
/// deliberate change of the format or of the fixture: re-record the lines the
/// failure prints, and a change of the FORMAT carries a VERSION_SAVE bump
/// beside it (manual/setup/57-versioning.md). No deliberate change: the codec
/// has begun writing something else, which is the whole reason these numbers
/// are here. Either way the number moves WITH ITS REASON, in the same commit.
constexpr std::array<RecordedSection, 20> kRecordedPayload = {{
    // Save 82: +15 — the seventh dictionary, tree_species (count 2, «pine»
    // 6, «birch» 7); predicted before the build, held.
    // Save 92: +2 — the map roads' dictionary, empty in this fixture's
    // tables (a u16 count). Predicted before the build, held.
    {"dictionaries", 160, 0xefef7308b1995f8bULL},
    // 2026-09-17, save 49: +10 bytes — the night pasture's standing order,
    // its first night and the camp's two floats.
    // 2026-09-17, save 50: +5 — four bytes for every limit point ever
    // granted, beside the points left this year, and one for the era events
    // that have come. The running total is stored and not summed: the books
    // keep one closed year and a chronicle year carries no points at all, so
    // it has nowhere else to live.
    // 2026-09-17, save 52: +62 bytes — readiness for the era transition, the
    // year, its eight scored components, the two indices, the two RUNS and
    // the six blocker bytes (readiness_state.h). The runs are why this is
    // record and not derivation: "three years running" is what a campaign
    // accumulated, and nothing in a loaded world could stand it up again.
    // 2026-09-17 again, save 53: the plan's three-year ring and its fill, so
    // the plan component can be «средний процент за последние три года»
    // rather than the last one. +13 bytes.
    // 2026-09-17, save 54: `measured` split off `available`, one byte per
    // component. +8 bytes.
    // 2026-09-18, save 55: +1 — the months the village has gone without a
    // distiller, the clock of the sobriety the human asked for.
    // 2026-09-18, save 56: +3 — the sky step and its heavy phase' hour and
    // length (the human's five steps of the sky).
    // 2026-09-18, save 57: +3 — the ration's checkbox (1) and the chairman's
    // issue norms, empty in this fixture (a 2-byte length). Predicted +3.
    // Save 60: +8 — the tally lost dry_months (1) and gained the month's open
    // leak (1), the vacancy (4) and the settlement's alcoholism (4).
    // Save 62: +26 — the accumulation limit, three positions (2 + 3 x 8),
    // predicted before the build.
    // Save 65: +5 — the cancelled day off's series (1) and day (4).
    // Save 66: +34 — the milk cart's share (8) and the deliveries outside any
    // position (2 + 3 x 8). The first build ran unpredicted (a miss, named);
    // 437 with the empty vector and then 461 were predicted and held.
    // Save 68: +2 — the sports field's month, two bytes; predicted before
    // the fields were added, and held.
    // Save 69: +4 — the season of the chairman's last talk; predicted before
    // the field was added, and held.
    // 2026-09-19, save 72: +1 — РАСПУТИЦА (WeatherState::mud), predicted
    // before the build together with the seventeen sections that did not move.
    // 2026-09-23, save 81: +1 — the pencil's deferred summons (boss seq 8),
    // predicted 500 -> 501 with the eighteen other sections unmoved before
    // the build; held. The arity tripwire (19 -> 20) was not named with it
    // — a miss in the prediction's inventory, caught by the tripwire.
    // Save 89: +36 — the goods loan owed and taken, two amounts of two
    // resources (2 + 16 each); predicted 509 -> 545 before the build, held.
    {"world", 545, 0x7b51f038030187ULL},
    // 2026-09-17, save 51: +8 bytes — four for each of the two residents, the
    // personal cleanliness that the filth disease is read off (health design
    // §3). Both ResidentRow tripwires fired on it, the size and the arity:
    // the float did NOT land in padding, so 184 became 188.
    // 2026-09-18, save 59: distiller_supplied_month, 4 bytes by 2 residents.
    // Save 69: talk_until_day, 4 bytes by 2 residents; predicted, and held.
    // Save 88: +2 — the placement's horse mark, a byte for each of the two
    // living residents; predicted 402 -> 404 with every other section
    // unmoved before the build, and held.
    // Save 93: +8 — the assignment's travel_hours, four bytes by two
    // residents; predicted 404 -> 412 before the build, held.
    {"residents", 412, 0x9f42bc247dabc6fcULL},
    // 2026-09-18, save 57: +2 — ration_granted, one byte per family of two.
    // Save 60: +2 — a yard's dry months, one byte per family of two.
    // Save 65: families +8 (overwork_penalty, two yards), fields +6 (the avral's
    // step and phase, three fields), units +3 (the site's avral step, three
    // units). The first build ran before a prediction was written — a miss,
    // named; the second, with the fixture's values, was predicted and held.
    // 2026-09-19, save 74: +13 a family — the certificate asked (a byte and
    // a day), the house lodged in and the lodging's cost; two families, +26.
    // Predicted +18 before the cost was added, held; then +26, held.
    // Save 75: +2 a family — in_barrack and the hunger alarm's memory; two
    // families, +4, each byte predicted before its build.
    {"families", 234, 0xb453a11b8ea5ef95ULL},
    // Save 84: +36 — the harvest by parts' laid share (4) and grams (8), 12
    // a row, three rows; predicted before the build and held. Then the first
    // row given non-zero values: the size held at 305, the hash moved.
    // Save 87: +12 — the sown share, a float a row, three rows; predicted 317
    // before the build and held (the struct's size was the miss, not this).
    {"fields", 317, 0xad9f9678a2051685ULL},
    // Save 67: +27 — the store's emptying byte and the perevalka's two floats,
    // three units; predicted before the fields were added, and held.
    // Save 74: +1 a unit — the house held for a specialist; three units, +3,
    // predicted.
    // Save 80: +1 a unit — the site's winter class; three units, +3,
    // predicted before the build.
    {"units", 359, 0x61bc71a02abf847bULL},
    // Save 71: +4 — fed_share, one herd; predicted before the field, held.
    // Save 91: +8 — the adult age band, two floats, one herd; predicted
    // 71 -> 79 with every other section unmoved before the build.
    {"herds", 79, 0xdb8c5a2f8e799ee2ULL},
    // 2026-09-16, save 48: +6 bytes, one for each of the six orders — the
    // bought head's sex. The witness named the section, the delta and the
    // offset without being asked, which is what it was rewritten for this
    // morning.
    // And the hash again, at the same LENGTH, when kNoRoomForStock became the
    // last OrderRefusal: the fixture carries the top of each enum on purpose,
    // so a new enumerator moves the recorded byte without moving the count.
    // And again at the same length when kGrazeAtNight became the last
    // OrderKind: the fixture carries the top of each enum on purpose.
    // And a fourth time, 2026-09-17, when handing stock back to the district
    // appended BOTH tops at once — kHandStock and kLastSire. Same 464 bytes,
    // two bytes of the six orders changed. VERSION_SAVE does not move for it:
    // an appended enumerator widens a range that old saves were already
    // inside, and nothing in the record grew or shrank.
    // A fifth, 2026-09-18: kTakeNightTrader became the last OrderKind — the
    // first way out of a night trade. Same 464 bytes, same reason, same save
    // number.
    // A sixth, the same day: kAdvanceEra became the last OrderKind and
    // kUnitsBelowLevel the last OrderRefusal — the chairman's order into
    // Epoch II and its seven answers. Both tops again, same 464 bytes, same
    // save number.
    // 2026-09-18, save 57: +30 — the ration order's family (4) and switch (1),
    // five bytes a row over six rows; the top OrderKind moved to kDeliverPlan
    // in the same stroke.
    // 2026-09-19, the avral's contract: the top OrderKind moved to
    // kCancelDayOff — same 494 bytes, same save number. Not predicted before
    // the build; read off it and named so.
    // 2026-09-19 again, the store-emptying contract: the top OrderKind moved
    // to kEmptyStore — same 494 bytes, predicted before the build this time.
    // 2026-09-19, the chairman's talk contract: the top OrderKind moved to
    // kTalkToSport and the top OrderRefusal to kNowhereToGo — same 494
    // bytes, same save number, predicted before the build.
    // The same day, kNowhereToStore became the top OrderRefusal — the lot
    // with nowhere to store: same 494 bytes, predicted.
    // Save 74: kAnswerLeaveRequest became the top OrderKind — same 494 bytes,
    // the hash moved, predicted.
    // 2026-09-23: the hash again at the same 494 bytes, when kSiteExhausted
    // became the last OrderRefusal — the top of the enum the fixture carries.
    // Not written down before the build (a miss); VERSION_SAVE stays 81.
    // Save 82: +36 — a planting's hectares (4) and species (2) on each of
    // the six orders; predicted, held.
    // Save 89: kTakeGoodsLoan became the top OrderKind — same 530 bytes, the
    // hash moved. NOT predicted: the prediction named world and ledger and
    // left this one out, the fifth time the top of the enum has moved here.
    {"orders", 530, 0x1e7fbae9a9b126e8ULL},
    // Save 82: the fixture's first stand, a birch planting — 8 -> 67 (its id
    // 4, the old fields 41, species 2, hectares 4, two days 8); predicted,
    // held.
    {"stands", 67, 0x9d5183df2ce65288ULL},
    {"limit_deliveries", 44, 0x9bfa765670c30958ULL},
    // 2026-09-16, save 48: the stock bought and still on its way. A section of
    // its own beside the carts, because a head rides nothing.
    {"livestock_arrivals", 24, 0x216b896caae651cbULL},
    {"specialist_arrivals", 22, 0x11b22bab116fbc84ULL},
    {"wedding_waits", 24, 0x3ce63fbbccfcbd4aULL},
    {"extraction_sites", 63, 0x52bb8a69b99d0d65ULL},
    {"district_visits", 19, 0x3785c4246ee3283bULL},
    {"night_outings", 31, 0xa7633d02f71169f5ULL},
    {"district_cars", 34, 0xaeab8ced44f30f22ULL},
    // Save 92: the road network — one player road: 8 of table, 4 of id, 5 of
    // words, 2 of the map-road id, 4 + 3 x 9 of axis, 4 + 2 x 4 of stretches.
    // Predicted 62 before the build, with the dictionaries +2 (the new kind's
    // empty count) and every other section unmoved.
    {"roads", 62, 0x1b467446569a40b2ULL},
    // 2026-09-17, save 52: +56 bytes over the two books — the nine yearly
    // inputs the readiness index asks of a year and the year did not keep
    // (ledger_state.h): satisfaction's sum and count, able-bodied
    // person-days, the plan's per cent with the byte that says it exists,
    // and the four numbers of the wintering as it stood on 1 December.
    // 2026-09-17 again, save 53: the worst season's food variety and the
    // count of seasons lived, over the two books. +10 bytes.
    // 2026-09-18, save 58: plan_due (M12) — the current book's empty column
    // (a 2-byte length) and the closed book's three positions (2 + 3 x 8).
    // +28, counted before the build.
    // Save 60: +20 — samogon_paid, the current book's empty column (2) and
    // the closed book's two positions (2 + 2 x 8).
    // Save 61: +20 again — lost_to_snow, the same shape, predicted before the
    // build and read off it.
    // Save 62: +12 — seized, the current book's empty column (2) and the
    // closed book's one position (2 + 8), predicted before the build.
    // Save 63: +16 — the reaping pace, two floats in each of the two books.
    // Not predicted before the build this time; read off it and named so.
    // Save 64: +16 — the daylight of those two days, two floats in each book.
    // Predicted before the build (756) and read off it.
    // Save 70: +20 — built_in in both books, the current one empty (a 2-byte
    // length) and the closed one two cells (2 + 2 x 8); predicted and held.
    // Then +28 — yard_feed the same way, three cells closed; predicted, held.
    // Save 73: +56 — processed (three cells closed, 2 + 24; the current book
    // empty, 2) and made (three cells, 2 + 24; 2). First predicted +64 with
    // made on four cells, and held to the byte; the fourth cell was moved off
    // a key another check removes, and the second prediction, +56, held too.
    // Save 82: +8 — the year's work-day arrays run by WorkKind, and
    // kPlanting lengthened them. NOT predicted (a miss, named).
    // Save 83: +28 — plan_delivered, the current book's empty column (2) and
    // the closed book's three positions (2 + 3 x 8); predicted before the
    // build, and held. Then +8 more: the current book's column given one cell
    // (2 + 8), so a codec swapping the books cannot round-trip — predicted
    // 904 before the build. The hash is recorded after it.
    // Save 86: +16 — milk_debt, eight bytes in each of the two books;
    // predicted 920 before the build, and held.
    // Save 89: +32 — the goods loan taken and repaid in each of the two books:
    // three empty (2 each) and the closed book's repayment of three
    // resources (2 + 24); predicted 920 -> 952 before the build, held.
    // Save 90: +28 — the removals by cause and by kind, three columns in
    // each book: five empty (2 each) and the closed book's autumn slaughter
    // of two kinds (2 + 16). Predicted 964 with all six empty, held; then
    // 980 with the fixture's value, held. The first value was sized at
    // three kinds against a fixture of two — refused by the dictionary's
    // count, a miss of the fixture's reading, not of the size.
    {"ledger", 980, 0x5a57a2a4b5c74385ULL},
    {"staged", 8, 0xa8c7f832281a39c5ULL},
}};

int TestTheWorldBlockIsWhatTheFormatSays(const core::ITableSet& tables) {
  int failures = 0;
  const core::WorldState witness = MakeWitnessWorld();
  const std::vector<std::byte> save = core::EncodeWorld(witness, tables);
  const std::span<const std::byte> block = WorldSection(save);
  const std::vector<Chunk> expected = ExpectedWorldBlock(witness);
  std::size_t wanted_size = 0;
  for (const Chunk& chunk : expected) {
    wanted_size += chunk.bytes.size();
  }
  if (block.size() != wanted_size) {
    // A note, not a failure of its own: the scan below names the field where
    // the two partings company, and one damage must redden one assertion.
    std::cout << "  the world block is " << block.size() << " bytes, the format's own rules give "
              << "its fields " << wanted_size << '\n';
  }
  std::size_t offset = 0;
  for (const Chunk& chunk : expected) {
    for (std::size_t index = 0; index < chunk.bytes.size(); ++index) {
      const std::size_t at = offset + index;
      if (at >= block.size()) {
        std::cout << "FAIL: the world block ends inside '" << chunk.name << "', at byte " << at
                  << '\n';
        return failures + 1;
      }
      if (std::to_integer<std::uint8_t>(block[at]) != chunk.bytes[index]) {
        std::cout << "FAIL: byte " << at << " of the world block, in '" << chunk.name
                  << "': the codec wrote "
                  << static_cast<int>(std::to_integer<std::uint8_t>(block[at]))
                  << ", the format says " << static_cast<int>(chunk.bytes[index]) << '\n';
        return failures + 1;
      }
    }
    offset += chunk.bytes.size();
  }
  failures += Expect(offset == block.size() && block.size() == wanted_size,
                     "the world block is field for field what core_save/save.h says it is");
  return failures;
}

int TestTheFixturePayloadIsWhatWasRecorded(std::span<const std::byte> save) {
  if (save.size() < core::kSaveHeaderSize) {
    return Expect(false, "the fixture's save has a header");
  }
  // Walked by the length prefixes, in the order the format fixes
  // (core_save/save.h): the names are the format's own, one per state table,
  // and the walk reads no field of any of them.
  std::vector<RecordedSection> found;
  std::vector<std::size_t> starts;
  std::size_t offset = core::kSaveHeaderSize;
  for (const RecordedSection& recorded : kRecordedPayload) {
    if (save.size() < offset + 8) {
      std::cout << "  the payload ends before section '" << recorded.name << "'\n";
      break;
    }
    const std::uint64_t length = LittleAt(save, offset);
    offset += 8;
    if (save.size() - offset < length) {
      std::cout << "  section '" << recorded.name << "' runs past the end of the payload\n";
      break;
    }
    const std::span<const std::byte> body = save.subspan(offset, static_cast<std::size_t>(length));
    starts.push_back(offset - core::kSaveHeaderSize);
    found.push_back({recorded.name, body.size(), Fnv1a64(body)});
    offset += static_cast<std::size_t>(length);
  }
  bool same = found.size() == kRecordedPayload.size();
  for (std::size_t index = 0; same && index < found.size(); ++index) {
    same = found[index].bytes == kRecordedPayload[index].bytes &&
           found[index].hash == kRecordedPayload[index].hash;
  }
  if (!same) {
    for (std::size_t index = 0; index < found.size(); ++index) {
      if (found[index].bytes == kRecordedPayload[index].bytes &&
          found[index].hash == kRecordedPayload[index].hash) {
        continue;
      }
      std::cout << "  section '" << found[index].name << "' differs: recorded "
                << kRecordedPayload[index].bytes << " bytes hashing to 0x" << std::hex
                << kRecordedPayload[index].hash << std::dec << ", now " << found[index].bytes
                << " bytes hashing to 0x" << std::hex << found[index].hash << std::dec
                << "; it begins at payload byte " << starts[index] << '\n';
    }
    std::cout << "  to re-record, with the reason in the same commit:\n";
    for (std::size_t index = 0; index < found.size(); ++index) {
      std::cout << "    {\"" << found[index].name << "\", " << found[index].bytes << ", 0x"
                << std::hex << found[index].hash << std::dec << "ULL},\n";
    }
  }
  return Expect(same,
                "the fixture encodes, section by section, to what was recorded for this "
                "save format");
}

core::Grams AmountAt(const core::ResourceAmounts& amounts, std::size_t index) {
  return index < amounts.size() ? amounts[index] : 0;
}

}  // namespace

int main() {
  int failures = 0;
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "kolkhoz_unit_core_save";
  const std::filesystem::path plain = root / "plain";
  const std::filesystem::path shuffled = root / "shuffled";
  const std::filesystem::path shortened = root / "shortened";

  WriteTableSet(plain, DefaultResources());
  // The same six keys, reordered: oat and manure swap, potato moves. This
  // is what a balancer's morning does to resources.csv.
  WriteTableSet(shuffled, {"manure", "milk", "oat", "hay", "barley", "potato"});
  // Milk removed entirely — the "a key the save may no longer use" case.
  WriteTableSet(shortened, {"oat", "barley", "potato", "hay", "manure"});

  std::string error;
  const auto tables = core::LoadTableSet(plain.string(), &error);
  const auto shuffled_tables = core::LoadTableSet(shuffled.string(), &error);
  const auto short_tables = core::LoadTableSet(shortened.string(), &error);
  if (tables == nullptr || shuffled_tables == nullptr || short_tables == nullptr) {
    std::cout << "FAIL: the synthetic table sets did not load (" << error << ")\n";
    return 1;
  }

  const core::WorldState world = MakeWorld();
  const std::vector<std::byte> bytes = core::EncodeWorld(world, *tables);
  failures += Expect(!bytes.empty(), "a world encodes");
  failures += Expect(bytes.size() > core::kSaveHeaderSize, "the save has a payload");

  // Encoding is deterministic: two saves of one state are the same file.
  failures += Expect(core::EncodeWorld(world, *tables) == bytes,
                     "encoding the same world twice gives the same bytes");

  // And the two checks that do not ask the codec anything (the long note
  // beside them): the world block against the layout written out by hand, the
  // fixture's payload against the size and hash recorded for this format.
  failures += TestTheWorldBlockIsWhatTheFormatSays(*tables);
  failures += TestTheFixturePayloadIsWhatWasRecorded(bytes);

  // A FINITE FLOAT IS NOT YET A USABLE ONE. The reader has refused NaN and
  // infinity since the format was young; 1e30 passes that door untouched
  // and is undefined behaviour at the first cast to an integer, which the
  // core makes on restored floats in a dozen places. The bound is judged
  // ONCE, where the number enters the state (boss, 2026-09-06: fix it in
  // the codec, not with a guard at every cast).
  {
    core::WorldState wild = MakeWorld();
    wild.units.rows[0].position.x = 1.0e30F;
    core::WorldState refused;
    std::string wild_error;
    failures +=
        Expect(!core::DecodeWorld(core::EncodeWorld(wild, *tables), *tables, &refused, &wild_error),
               "a save carrying a float too large to be a quantity is refused");
    // THE CONTROL, or the check above would pass on a codec that refuses
    // any unusual number. A hundred million metres is nonsense as a
    // position too, but it is inside the bound and the codec is not the
    // place that judges sense — only representability.
    core::WorldState large = MakeWorld();
    large.units.rows[0].position.x = 1.0e8F;
    core::WorldState restored;
    std::string large_error;
    failures += Expect(
        core::DecodeWorld(core::EncodeWorld(large, *tables), *tables, &restored, &large_error),
        "while a large but representable one goes through");
    failures += Expect(restored.units.rows[0].position.x == 1.0e8F,
                       "and arrives unchanged — the bound refuses, it does not clamp");
  }

  // A NEGATIVE MILK DEBT is no state the simulation makes, and taken as it
  // stands it would ask the stores for a negative amount (save 85). The
  // control is the same world with no debt, which loads.
  {
    core::WorldState owing = MakeWorld();
    owing.plan.milk_debt = -1;
    core::WorldState refused;
    std::string owing_error;
    failures += Expect(
        !core::DecodeWorld(core::EncodeWorld(owing, *tables), *tables, &refused, &owing_error),
        "a save carrying a negative milk debt is refused");
    core::WorldState clear = MakeWorld();
    clear.plan.milk_debt = 0;
    core::WorldState restored;
    std::string clear_error;
    failures += Expect(
        core::DecodeWorld(core::EncodeWorld(clear, *tables), *tables, &restored, &clear_error),
        "while a debt of nothing goes through");
  }

  // A NEGATIVE GOODS LOAN OWED is no state the simulation makes (save 89):
  // paid down to nought and no further. The control is the fixture itself.
  {
    core::WorldState owing = MakeWorld();
    owing.plan.goods_loan_owed = Amounts({0, -1});
    core::WorldState refused;
    std::string owing_error;
    failures += Expect(
        !core::DecodeWorld(core::EncodeWorld(owing, *tables), *tables, &refused, &owing_error),
        "a save owing a negative goods loan is refused");
  }

  // -- the round trip ------------------------------------------------------
  core::WorldState loaded;
  loaded.epoch = core::Epoch::kThree;  // a marker, to catch a partial write
  failures += Expect(core::DecodeWorld(bytes, *tables, &loaded, &error), "a save decodes");
  failures += Expect(core::EncodeWorld(loaded, *tables) == bytes,
                     "re-encoding the loaded world reproduces the file byte for byte");

  failures += Expect(loaded.calendar.tick == world.calendar.tick &&
                         loaded.calendar.date.month == world.calendar.date.month &&
                         loaded.calendar.weekday == world.calendar.weekday,
                     "the calendar and its rebuilt caches came back");
  failures += Expect(loaded.rng.state == world.rng.state && loaded.rng.stream == world.rng.stream,
                     "the RNG came back to the bit");
  failures += Expect(loaded.residents.rows[0].satiety == world.residents.rows[0].satiety &&
                         loaded.vitals.satiety_year_means[2] == world.vitals.satiety_year_means[2],
                     "floats came back exactly, not rounded through decimal");
  failures += Expect(loaded.residents.rows[0].birth_day == -4321,
                     "a birth day before day 0 survived as a negative");
  failures += Expect(loaded.residents.rows[0].post.profession.value == 1 &&
                         loaded.residents.rows[0].post.unit.value == 1,
                     "the post he holds came back whole");
  failures += Expect(loaded.weather.phenomenon == core::WeatherPhenomenon::kBlizzard &&
                         loaded.weather.wind == core::WindBand::kStrongWind,
                     "the day's name and its wind band survive the round trip");
  failures += Expect(loaded.weather.sky == core::SkyStep::kHeavyPrecipitation &&
                         loaded.weather.heavy_from_hour == 21 && loaded.weather.heavy_hours == 7,
                     "the sky step and its heavy phase survive the round trip (save format 56)");
  failures +=
      Expect(loaded.weather.snow_cover_days == 9, "and so does the snow lying on the ground");
  failures += Expect(loaded.weather.cover_since_leaf_fall,
                     "and the word that separates the count's two zeros — a cover having lain "
                     "since the leaf fall — survives with it");
  failures += Expect(loaded.weather.mud, "and the mud season survives the round trip (save 72)");
  failures += Expect(loaded.chairman.horses_stabled == 1,
                     "and the milestone that cannot be undone came back set");
  failures += Expect(loaded.chairman.ration_auto == 0,
                     "the ration's checkbox the chairman switched off comes back off (save 57)");
  failures += Expect(
      loaded.chairman.days_off_cancelled_in_a_row == 2 && loaded.chairman.cancelled_day_off == 55,
      "the cancelled day off and its series come back (save 65)");
  failures += Expect(
      loaded.chairman.last_talk_season == 5 && loaded.residents.rows[0].talk_until_day == 140,
      "the talk's season and a man's talk come back (save 69)");
  failures += Expect(loaded.families.rows[0].overwork_penalty == 3.75F,
                     "a yard's overwork memory comes back (save 65)");
  failures += Expect(loaded.fields.rows[2].rush_step == 3 &&
                         loaded.fields.rows[2].rush_phase == core::FieldPhase::kHarvest &&
                         loaded.fields.rows[0].rush_step == 0,
                     "a field's avral and the phase it stands on come back (save 65)");
  failures += Expect(loaded.residents.next_id_value == world.residents.next_id_value &&
                         loaded.residents.rows.size() == 2,
                     "the spent id of a dead resident was not reissued");
  failures += Expect(core::FindRow(loaded.residents, loaded.residents.row_ids[1]) == 1,
                     "the id lookup was rebuilt");
  failures += Expect(core::FindRow(loaded.residents, core::ResidentId{3}) == core::kNoRow,
                     "the dead resident's id reads as gone, not as somebody else");

  // Unchanged tables: dense vectors come back AS THEY ARE, length included.
  failures += Expect(loaded.families.rows[0].pantry == world.families.rows[0].pantry &&
                         loaded.families.rows[1].pantry.empty(),
                     "pantries came back with their exact lengths, the empty one still empty");
  failures += Expect(loaded.families.rows[0].first_meal_eaten == 1 &&
                         loaded.families.rows[1].first_meal_eaten == 0,
                     "a family that has eaten comes back one, and a new one comes back new");
  failures += Expect(loaded.families.rows[0].in_tent == 1 && loaded.families.rows[1].in_tent == 0 &&
                         loaded.families.rows[0].lost_house_position.x == 812.5F &&
                         loaded.families.rows[0].lost_house_position.y == 9044.25F,
                     "a family in a tent comes back in its tent, on its old plot");
  failures += Expect(
      loaded.families.rows[0].ration_granted == 1 && loaded.families.rows[1].ration_granted == 0,
      "a yard granted the ration comes back granted, and one not granted, not");
  failures +=
      Expect(loaded.families.rows[0].dry_months == 4, "a yard's dry months come back (save 60)");
  failures += Expect(loaded.wedding_waits.rows.size() == 1 &&
                         loaded.wedding_waits.rows[0].bride.value == 7 &&
                         loaded.wedding_waits.rows[0].groom.value == 9 &&
                         loaded.wedding_waits.rows[0].since_day == 211,
                     "a couple waiting for a house comes back waiting, since its day");
  failures += Expect(loaded.extraction_sites.rows.size() == 1 &&
                         loaded.extraction_sites.rows[0].table_row == 3 &&
                         loaded.extraction_sites.rows[0].resource.value == 2 &&
                         loaded.extraction_sites.rows[0].position.x == 7281.5F &&
                         loaded.extraction_sites.rows[0].position.y == 8504.25F &&
                         loaded.extraction_sites.rows[0].stock_grams == 3'900'000'000 &&
                         loaded.extraction_sites.rows[0].marked_grams == 20'000'000 &&
                         loaded.extraction_sites.rows[0].work_days_remaining == 0.75F &&
                         loaded.extraction_sites.rows[0].load_grams == 6'000'000 &&
                         loaded.extraction_sites.rows[0].haul_days_remaining == 1.5F &&
                         loaded.extraction_sites.rows[0].haul_days_written == 2.25F &&
                         loaded.extraction_sites.rows[0].exhausted == 1,
                     "a clay pit comes back half dug, its load waiting and its mark standing");
  failures += Expect(loaded.stands.rows.size() == 1 &&
                         loaded.stands.rows[0].kind == core::TimberStandKind::kPlanted &&
                         loaded.stands.rows[0].species.value == 1 &&
                         loaded.stands.rows[0].planted_area_ha == 2.5F &&
                         loaded.stands.rows[0].planted_day == 97 &&
                         loaded.stands.rows[0].matures_day == 337,
                     "a birch planting comes back birch, its hectares and both its days");
  failures += Expect(loaded.residents.rows[1].work.kind == core::WorkKind::kExtraction &&
                         loaded.residents.rows[1].work.extraction_site.value == 4 &&
                         loaded.residents.rows[1].work.rides_horse == 1,
                     "a digger comes back at her pit, with the placement's horse mark");
  failures +=
      Expect(loaded.plan.delivered.size() == 3, "a short dense vector was not silently padded");
  failures += Expect(
      loaded.ledger.closed.plan_due.size() == 3 && loaded.ledger.closed.plan_due[0] == 12'000'000,
      "what the district asked comes back in the closed book (save 58)");
  failures += Expect(loaded.ledger.closed.plan_delivered.size() == 3 &&
                         loaded.ledger.closed.plan_delivered[0] == 11'800'000 &&
                         loaded.ledger.current.plan_delivered.size() == 1 &&
                         loaded.ledger.current.plan_delivered[0] == 5'000,
                     "what went against the position comes back in the closed book (save 83)");
  failures +=
      Expect(loaded.ledger.closed.milk_debt == 200'000 && loaded.ledger.current.milk_debt == 7'000,
             "and the milk debt of each book comes back (save 86)");
  // Filled since save 60 and read by nothing until save 61 was written: a
  // codec that forgot to READ samogon_paid would have passed this file.
  failures += Expect(AmountAt(loaded.ledger.closed.samogon_paid, 0) == 3'000 &&
                         AmountAt(loaded.ledger.closed.samogon_paid, 1) == 5'000,
                     "the drink's price in kind comes back in the closed book (save 60)");
  failures += Expect(AmountAt(loaded.ledger.closed.lost_to_snow, 1) == 150'000'000,
                     "the standing crop the snow took comes back in the closed book (save 61)");
  failures += Expect(AmountAt(loaded.ledger.closed.processed, 2) == 400 &&
                         AmountAt(loaded.ledger.closed.made, 2) == 300,
                     "the shops' two lines survive the round trip (save 73)");
  failures += Expect(AmountAt(loaded.ledger.closed.built_in, 1) == 250 &&
                         AmountAt(loaded.ledger.closed.yard_feed, 2) == 300,
                     "what went into a building and what the yards' beasts ate come back in the "
                     "closed book (save 70)");
  failures += Expect(!loaded.herds.rows.empty() && loaded.herds.rows[0].fed_share == 0.625F,
                     "a herd's covered share of the ration comes back (save 71)");
  failures += Expect(loaded.district_cars.rows.size() == 1 &&
                         loaded.district_cars.rows[0].phase == core::DistrictCarPhase::kAtTheYard &&
                         loaded.district_cars.rows[0].leave_tick == 1'236 &&
                         loaded.residents.rows.size() >= 2 &&
                         loaded.residents.rows[1].away_until_day == 91 &&
                         loaded.residents.rows[1].away_until_hour == 14 &&
                         loaded.residents.rows[1].away_walk_hours == 3 &&
                         loaded.residents.rows[1].away_reason ==
                             static_cast<std::uint8_t>(core::AwayReason::kHospital),
                     "the district's car and a resident away in the hospital come back (save 79)");
  failures += Expect(loaded.roads.rows.size() == 1 &&
                         loaded.roads.rows[0].origin == core::RoadOrigin::kPlayer &&
                         loaded.roads.rows[0].surface == core::RoadSurface::kGravel &&
                         loaded.roads.rows[0].traffic_word == core::RoadTrafficWord::kRare &&
                         loaded.roads.rows[0].axis.size() == 3 &&
                         loaded.roads.rows[0].axis[1].mark == core::RoadMark::kBridge &&
                         loaded.roads.rows[0].axis[2].position.x == 45.0F &&
                         loaded.roads.rows[0].stretches.size() == 2 &&
                         loaded.roads.rows[0].stretches[1].wear_pct == 40.0F,
                     "a player's road comes back with its axis and its stretches (save 92)");
  failures += Expect(loaded.residents.rows.size() >= 2 &&
                         loaded.residents.rows[0].twin.value == loaded.residents.row_ids[1].value &&
                         loaded.residents.rows[1].identical_twin == 1,
                     "twins name each other and keep their mark (save 78)");
  failures += Expect(!loaded.herds.rows.empty() && loaded.herds.rows[0].autumn_slaughter_done == 1,
                     "a herd remembers its autumn slaughter was done (save 76)");
  failures += Expect(AmountAt(loaded.ledger.closed.seized, 0) == 5'000'000 &&
                         AmountAt(loaded.plan.accumulation_limit, 0) == 15'000'000 &&
                         AmountAt(loaded.plan.accumulation_limit, 2) == 4'000'000,
                     "the seizure and the accumulation limit come back (save 62)");
  failures += Expect(loaded.plan.milk_daily_share == 250'000 &&
                         AmountAt(loaded.plan.delivered_outside, 2) == 3'000'000,
                     "the milk cart's share and the winter's milk come back (save 66)");
  failures += Expect(loaded.plan.milk_debt == 37'000, "and the milk debt comes back (save 85)");
  failures += Expect(AmountAt(loaded.plan.goods_loan_owed, 1) == 1'500'000 &&
                         AmountAt(loaded.plan.goods_loan_taken, 1) == 1'250'000 &&
                         AmountAt(loaded.ledger.closed.goods_loan_repaid, 2) == 900'000,
                     "the goods loan owed and taken come back, and the book's repayment (save 89)");
  failures += Expect(AmountAt(loaded.ledger.closed.herd_autumn_slaughtered, 1) == 4,
                     "the book's autumn slaughter by kind comes back (save 90)");
  failures += Expect(
      loaded.ledger.closed.reaping_today == 3.25F && loaded.ledger.closed.reaping_last_day == 22.5F,
      "the season's reaping pace comes back (save 63)");
  failures += Expect(loaded.ledger.closed.reaping_today_daylight == 8.25F &&
                         loaded.ledger.closed.reaping_last_day_daylight == 15.5F,
                     "and the daylight it was reaped under (save 64)");
  // PlanState carried no tripwire at all until 2026-09-12 — the only
  // serialized block without one — so these three are the first thing that
  // would have noticed a field quietly dropped by the codec.
  // THE LOOK OF THE GROUND, and it is here because dropping it cost nothing
  // in the first draft of this delivery: the byte replaced a whole LandKind
  // value on 2026-09-12, the damage test wrote a zero instead of it, and
  // every assertion in this file stayed green. A field added to a row and
  // not to this round trip is a field the codec may quietly forget.
  failures += Expect(loaded.fields.rows[0].harvest_laid_share == 0.375F &&
                         loaded.fields.rows[0].harvest_laid_grams == 1'234'567 &&
                         loaded.fields.rows[2].harvest_laid_grams == 0,
                     "the harvest by parts' laid share and grams come back (save 84)");
  failures +=
      Expect(loaded.fields.rows[0].sown_share == 0.625F, "and the sown share comes back (save 87)");
  failures += Expect(loaded.fields.rows[2].overgrown == 1,
                     "the weeds on the unworked ground survive the round trip");
  // AND WHETHER ANYBODY EVER TOLD THE FIELD WHAT TO GROW, which the three
  // crop slots beside it cannot say: an empty slot in a chain that exists is
  // a fallow year, and the same emptiness in a field nobody assigned is
  // nothing at all. Lose this byte and every unworked hectare comes back
  // farmed — ploughed, recovered and manured for ever after.
  failures += Expect(loaded.fields.rows[0].rotation_assigned == 1,
                     "a field that was given a chain comes back holding one");
  failures += Expect(loaded.fields.rows[0].rotation_skips_turn == 1,
                     "and the held turn comes back held: a campaign saved in November and "
                     "loaded in December still sows the crop the chairman named first");
  failures += Expect(loaded.fields.rows[2].rotation_skips_turn == 0,
                     "while a field that never asked for one comes back turning normally — the "
                     "pair a codec writing a constant would fail on");
  failures += Expect(loaded.fields.rows[2].rotation_assigned == 0,
                     "and ground nobody assigned comes back unassigned, not as three fallow "
                     "years — a codec writing a constant would satisfy one of these two");
  failures += Expect(loaded.fields.rows[0].overgrown == 0,
                     "and a worked field does not come back overgrown, which a codec writing a "
                     "constant would also satisfy the other way round");
  // THE START QUEST'S FIELD (2026-09-14): lose the mark and the reserve is
  // removed in a loaded campaign without its fact ever being raised.
  failures += Expect(loaded.fields.rows[2].start_reserve == 1,
                     "the start's reserve field comes back marked");
  failures += Expect(loaded.fields.rows[0].start_reserve == 0,
                     "and an ordinary field comes back unmarked — the pair a codec writing a "
                     "constant would fail on");
  // THE DAY OF THE LAST REAPING (2026-09-15): lose it and the seed fund of a
  // loaded campaign holds this year's seed for a crop already in the stores.
  failures += Expect(loaded.fields.rows[2].reaped_day == 39 &&
                         loaded.fields.rows[0].reaped_day == core::kNeverReapedDay,
                     "the day a field was last reaped comes back, and a field never reaped "
                     "comes back never reaped");
  failures += Expect(loaded.plan.last_verdict == core::PlanVerdict::kFailed,
                     "the district's verdict on the year survives the round trip");
  failures += Expect(loaded.plan.failed_years_in_a_row == 2, "and the run of failed years");
  failures += Expect(loaded.plan.announced == 1,
                     "the district's having spoken survives the round trip — a tonnage of zero "
                     "cannot carry that fact");
  failures +=
      Expect(loaded.plan.worked_ha_last_year > 69.9F && loaded.plan.worked_ha_last_year < 70.1F,
             "and so does the area next spring's norm is computed from");
  failures += Expect(loaded.plan.met_years_in_a_row == 5,
                     "and the run of met ones, which is a different number");
  // The unsealing, field by field: an order kind whose payload is three new
  // fields is three chances for one of them to be dropped.
  const core::OrderRow& unseal_back = loaded.orders.rows[5];
  failures += Expect(unseal_back.kind == core::OrderKind::kUnsealFund,
                     "the unsealing order comes back as an unsealing");
  failures += Expect(unseal_back.fund == core::FundKind::kSeed, "and names the fund it opened");
  failures += Expect(unseal_back.resource.value == 2, "and the resource taken out of it");
  failures += Expect(unseal_back.amount == 640'000, "and the figure the chairman named");
  // The bought head's sex (save 48), asserted on the READER's side: the
  // recorded payload proves only that the byte was written.
  failures += Expect(loaded.orders.rows[3].male == 1,
                     "the sex the chairman chose for a head comes back off the save");
  failures += Expect(loaded.orders.rows[3].family.value == 2 && loaded.orders.rows[3].enable == 1,
                     "the ration order's family and switch come back off the save (save 57)");
  // The head on its way, field by field. The byte-for-byte re-encode above
  // would catch a dropped field too, but it would say only "the file differs";
  // this says WHICH of the six.
  {
    const core::LivestockArrivalRow& head = loaded.livestock_arrivals.rows[0];
    failures += Expect(head.lot.value == 1 && head.kind.value == 1,
                       "a bought head comes back knowing its lot and its kind");
    failures += Expect(head.head_count == 3 && head.arrive_day == 133,
                       "and how many head there are and the day they stand in the village");
    failures += Expect(head.stage == core::LivestockArrivalStage::kYoung && head.male == 1,
                       "and the age it arrives at and the sex that was chosen");
  }
  failures +=
      Expect(loaded.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kSeed)][2] == 640'000,
             "and the release against the seed fund is world state that survives");
  failures += Expect(
      loaded.unsealed.by_fund[static_cast<std::size_t>(core::FundKind::kPlanReserve)][1] == 310'000,
      "as is the one against the plan reserve, which is a different number");
  failures +=
      Expect(loaded.ledger.closed.year == 2 && loaded.ledger.closed.trudodni_burned == 4200 &&
                 loaded.ledger.current.births == 1,
             "both ledger books came back");
  const bool wall_intact =
      loaded.ledger.chronicle.size() == 3 && loaded.ledger.chronicle[0].year == 1 &&
      loaded.ledger.chronicle[2].year == 3 && loaded.ledger.chronicle[1].residents == 93 &&
      loaded.ledger.chronicle[1].fertility == 54.25F &&
      loaded.ledger.chronicle[2].harvest_kcal == 39'250'000;
  failures += Expect(wall_intact, "and the office wall came back whole, in order");

  // The order book: a campaign saved with an order waiting resumes with it
  // waiting, and the waiting row keeps every field the consumer will read.
  failures += Expect(
      loaded.orders.rows.size() == 6 && loaded.orders.next_id_value == world.orders.next_id_value,
      "the order book came back whole");
  failures += Expect(loaded.orders.rows[4].status == core::OrderStatus::kAccepted &&
                         loaded.orders.rows[4].profession.value == 0 &&
                         loaded.orders.rows[4].refusal == core::OrderRefusal::kNoVacancy,
                     "an appointment still waiting resumes still waiting");
  failures += Expect(loaded.orders.rows[0].kind == core::OrderKind::kAssignWork &&
                         loaded.orders.rows[0].status == core::OrderStatus::kAccepted &&
                         loaded.orders.rows[0].work == core::WorkKind::kHarvest &&
                         loaded.orders.rows[0].issued_tick == 71 &&
                         loaded.orders.rows[0].resident.value == 1,
                     "a waiting order kept its status, its target and the tick it was issued on");
  failures += Expect(loaded.orders.rows[2].position.x == -12.5F &&
                         loaded.orders.rows[3].kind == kTopOrderKind &&
                         loaded.orders.rows[3].status == kTopOrderStatus &&
                         loaded.orders.rows[3].refusal == kTopOrderRefusal,
                     "the build order's position and the TOP of every order enum survived the "
                     "round trip — which is where the codec's bound is actually exercised");
  failures += Expect(loaded.orders.rows[3].lot.value == 1, "the order's limit lot came back");
  failures += Expect(loaded.limit.points == 215, "the year's limit points came back");
  failures += Expect(loaded.limit_deliveries.rows.size() == 1 &&
                         loaded.limit_deliveries.rows[0].lot.value == 0 &&
                         loaded.limit_deliveries.rows[0].arrive_day == 131 &&
                         AmountAt(loaded.limit_deliveries.rows[0].goods, 2) == 4'800'000,
                     "a cart on the road came back with its lot, its day and its goods");
  failures += Expect(loaded.specialist_arrivals.rows.size() == 1 &&
                         loaded.specialist_arrivals.rows[0].profession.value == 1 &&
                         loaded.specialist_arrivals.rows[0].unit.value == 3 &&
                         loaded.specialist_arrivals.rows[0].arrive_day == 97,
                     "a specialist on the road came back with his post, his unit and his day");
  failures += Expect(
      loaded.district_visits.rows.size() == 1 && loaded.district_visits.rows[0].arrive_day == 203 &&
          loaded.district_visits.rows[0].face == core::DistrictFace::kPolushkina &&
          loaded.district_visits.rows[0].kind == core::DistrictVisitKind::kExtraordinary &&
          loaded.district_visits.rows[0].cause == core::DistrictVisitCause::kJuniorSignal,
      "a district visit on its way came back with its day, face, kind and cause");
  failures += Expect(loaded.night_outings.rows.size() == 1 &&
                         loaded.night_outings.rows[0].resident.value == 5 &&
                         loaded.night_outings.rows[0].trade == core::NightTrade::kNetFisher &&
                         loaded.night_outings.rows[0].day == 146 &&
                         loaded.night_outings.rows[0].position.x == 8650.0F &&
                         loaded.night_outings.rows[0].position.y == 10160.0F &&
                         loaded.night_outings.rows[0].hour_out == 23 &&
                         loaded.night_outings.rows[0].hour_back == 3,
                     "a night outing came back with who, what, the night, the place and the hours");
  failures += Expect(!loaded.residents.rows.empty() &&
                         loaded.residents.rows[0].night_trade == core::NightTrade::kHunter &&
                         loaded.residents.rows[0].distiller_supplied_month == 7,
                     "a resident came back with his night trade");
  failures += Expect(!loaded.residents.rows.empty() && loaded.residents.rows[0].school.value == 6,
                     "a pupil came back enrolled in his school");
  failures +=
      Expect(!loaded.residents.rows.empty() && loaded.residents.rows[0].days_worked_this_month == 3,
             "a resident came back with the days he worked this month");
  failures += Expect(loaded.ledger.current.limit_points_granted == 350 &&
                         loaded.ledger.current.limit_points_spent == 135 &&
                         loaded.ledger.current.limit_points_burned == 7,
                     "the year's limit flows came back, each its own number");
  failures += Expect(AmountAt(loaded.ledger.current.night_catch, 1) == 1'500 &&
                         AmountAt(loaded.ledger.current.night_catch, 2) == 5'000,
                     "the night trades' catch came back in the year's book");
  failures +=
      Expect(AmountAt(loaded.ledger.current.stolen, 0) == 48'000 &&
                 AmountAt(loaded.ledger.current.stolen, 2) == 2'000 &&
                 loaded.night_theft.stolen_this_month == 73'000 &&
                 loaded.night_theft.month_index == 17 && loaded.night_theft.complaint_raised == 1,
             "the stolen and the distillers' month came back, each its own number");
  failures += Expect(loaded.night_theft.leak_open_this_month == 1 &&
                         loaded.night_theft.distiller_short_since == 23 &&
                         loaded.night_theft.settlement_alcoholism == 31.5F,
                     "the leak's month, the vacancy and the settlement's drinking came back "
                     "(save 60)");
  failures +=
      Expect(loaded.sport_month.open_days == 3 && loaded.sport_month.downpour_yesterday == 1,
             "the sports field's month comes back (save 68)");
  failures +=
      Expect(loaded.mts_column.phase == core::MtsColumnPhase::kWorking &&
                 loaded.mts_column.lot.value == 1 && loaded.mts_column.arrive_day == 110 &&
                 loaded.mts_column.camp.value == 9 && loaded.mts_column.worked_ha == 32.5F &&
                 loaded.mts_column.field.value == 4 && loaded.mts_column.field_ha == 7.5F,
             "the MTS column came back: phase, lot, day, camp, hectares and its field");

  // The condition bytes came back as well, each one separately: a check
  // that read them together would pass on a codec that swapped them.
  const core::UnitRow& barn_back = loaded.units.rows[0];
  failures += Expect(barn_back.construction.reserved.size() >= 2 &&
                         barn_back.construction.reserved[1] == 2'500'000 &&
                         barn_back.construction.reserved[0] == 0,
                     "an upgrade resumes with its recipe still held back from the store");
  const core::UnitRow& house_back = loaded.units.rows[1];
  failures += Expect(house_back.paused == 1, "a stopped unit resumes stopped");
  failures += Expect(house_back.dead == 1, "and a dead one resumes dead, not merely worn");
  failures += Expect(house_back.wear == 70.0F, "and its wear is its own number");
  failures += Expect(house_back.insulated == 1 &&
                         house_back.construction.phase == core::ConstructionPhase::kInsulating,
                     "and a warm house being insulated resumes warm and mid-insulation");

  // The site came back mid-build, every field of it.
  const core::UnitRow& site_back = loaded.units.rows[2];
  failures += Expect(
      site_back.level == 0 && site_back.construction.phase == core::ConstructionPhase::kBuilding &&
          site_back.construction.target_level == 1 &&
          site_back.construction.labor_days_total == 17.5F &&
          site_back.construction.labor_days_remaining == 6.25F &&
          site_back.construction.max_crew == 8 && site_back.construction.rush_step == 5,
      "a half-built unit resumes half-built, crew ceiling and avral included");
  failures += Expect(site_back.emptying == 1 && site_back.haul_days_remaining == 2.5F &&
                         site_back.haul_days_written == 4.0F,
                     "a store being emptied comes back emptying, its carrying half done (save 67)");
  failures += Expect(site_back.construction.winter_works == 0,
                     "a site that stands in winter still stands after a load (save 80)");

  // -- the staged batch ----------------------------------------------------
  //
  // The one section that is not the WorldState. A campaign is saved on pause,
  // after the day's orders have been handed out, and those orders are not in
  // the book yet — the engine applies them at the next step (order_state.h).
  core::StagedOrders staged;
  //
  // TWO ROWS, AND THE SECOND IS THE TOP OF ITS ENUM. The codec validates
  // every enum byte it reads against an upper bound written by hand in
  // save_rows.cpp, and that bound was left behind when task A2 appended
  // kStartBuild, kUpgradeUnit and three refusals: a save carrying a staged
  // build order was refused whole, over a byte "outside 0..7". A guard that
  // stages anything below the top cannot see the bound slip back by one, so
  // one row carries kUpgradeUnit — the last kind there is — and the refused
  // row on the order book above carries kNotEmpty, the last refusal. The
  // other row is the ordinary case that first broke.
  core::OrderRow waiting;
  waiting.kind = core::OrderKind::kStartBuild;
  waiting.unit = core::UnitId{2};
  waiting.issued_tick = 96;
  staged.issued.push_back(waiting);
  core::OrderRow upgrading;
  upgrading.kind = core::OrderKind::kUpgradeUnit;
  upgrading.unit = core::UnitId{3};
  upgrading.issued_tick = 97;
  staged.issued.push_back(upgrading);
  staged.cancelled.push_back(core::OrderId{7});

  const std::vector<std::byte> with_batch = core::EncodeWorld(world, staged, *tables);
  core::WorldState batch_world;
  core::StagedOrders batch_back;
  failures += Expect(core::DecodeWorld(with_batch, *tables, &batch_world, &batch_back, &error),
                     "a save with a staged batch decodes");
  failures +=
      Expect(batch_back.issued.size() == 2 && batch_back.cancelled.size() == 1 &&
                 batch_back.issued[0].kind == core::OrderKind::kStartBuild &&
                 batch_back.issued[0].unit.value == 2 && batch_back.issued[0].issued_tick == 96 &&
                 batch_back.issued[1].kind == core::OrderKind::kUpgradeUnit &&
                 batch_back.issued[1].unit.value == 3 && batch_back.issued[1].issued_tick == 97 &&
                 batch_back.cancelled[0].value == 7,
             "and hands the batch back exactly as it was staged, in order");
  core::WorldState no_batch_world;
  failures += Expect(!core::DecodeWorld(with_batch, *tables, &no_batch_world, &error),
                     "a caller with no session to resume them into refuses, never drops them");
  failures += Expect(core::DecodeWorld(bytes, *tables, &no_batch_world, &error),
                     "and a save with an empty batch loads for such a caller");

  // -- the header ----------------------------------------------------------
  const auto info = core::PeekSaveInfo(bytes);
  failures += Expect(info.has_value(), "the header reads without the payload");
  if (info.has_value()) {
    failures += Expect(info->world_seed == world.world_seed && info->tick == world.calendar.tick,
                       "the header carries the seed and the tick");
    failures += Expect(info->payload_size == bytes.size() - core::kSaveHeaderSize,
                       "the header's payload size is the payload's size");
  }

  // -- the remap -----------------------------------------------------------
  // Same keys, new order. Amounts must follow their KEY, not their index.
  const std::vector<std::string>& plain_keys = DefaultResources();
  core::WorldState remapped;
  failures +=
      Expect(core::DecodeWorld(bytes, *shuffled_tables, &remapped, &error), "a save remaps by key");
  const core::ITable* shuffled_resources = shuffled_tables->FindTable("resources");
  if (shuffled_resources != nullptr) {
    const std::uint32_t oat = shuffled_resources->FindRowByKey("oat");
    const std::uint32_t potato = shuffled_resources->FindRowByKey("potato");
    const std::uint32_t hay = shuffled_resources->FindRowByKey("hay");
    failures += Expect(AmountAt(remapped.families.rows[0].pantry, oat) ==
                               AmountAt(world.families.rows[0].pantry, 0) &&
                           AmountAt(remapped.families.rows[0].pantry, potato) ==
                               AmountAt(world.families.rows[0].pantry, 2),
                       "the pantry's grain and potato moved to their new indices");
    failures += Expect(AmountAt(remapped.units.rows[0].stock, hay) == 165'000'000,
                       "the barn's hay followed its key");
    failures += Expect(remapped.families.rows[0].pantry.size() == plain_keys.size(),
                       "a permuted vector is rebuilt to the live row count");
    failures += Expect(remapped.families.rows[1].pantry.empty(),
                       "an empty pantry stays empty through a permutation");
    failures += Expect(remapped.ledger.closed.harvest.size() == plain_keys.size() &&
                           AmountAt(remapped.ledger.closed.harvest, oat) == 43'000'000,
                       "the ledger's per-resource columns remap too");
  }

  // A key the save USES is gone: refusal, and the target world untouched.
  core::WorldState untouched;
  untouched.epoch = core::Epoch::kThree;
  error.clear();
  failures += Expect(!core::DecodeWorld(bytes, *short_tables, &untouched, &error),
                     "a save that uses a removed key is refused");
  failures +=
      Expect(error.find("milk") != std::string::npos, "the refusal names the key that vanished");
  failures += Expect(untouched.epoch == core::Epoch::kThree && untouched.residents.rows.empty(),
                     "a refused load leaves the caller's world alone");

  // The same table set, but a world that never mentions milk: it loads, and
  // the empty column is dropped in silence.
  core::WorldState without_milk = MakeWorld();
  without_milk.families.rows[0].pantry[3] = 0;
  without_milk.ledger.closed.eaten[3] = 0;
  const std::vector<std::byte> lean = core::EncodeWorld(without_milk, *tables);
  core::WorldState lean_loaded;
  failures += Expect(core::DecodeWorld(lean, *short_tables, &lean_loaded, &error),
                     "an unused removed key is dropped without a word");

  // -- the refusals --------------------------------------------------------
  // Every refusal below logs its reason, so the [error] lines that follow
  // are the test working, not the test failing.
  std::cout << "unit_core_save: the [error] lines below are the refusals under test\n";
  const auto refuses = [&](std::vector<std::byte> broken, const char* label) {
    core::WorldState target;
    std::string reason;
    const bool refused = !core::DecodeWorld(broken, *tables, &target, &reason);
    return Expect(refused && !reason.empty(), label);
  };

  std::vector<std::byte> tampered = bytes;
  tampered[0] = std::byte{'X'};
  failures += refuses(tampered, "a file without the magic is refused");

  tampered = bytes;
  tampered[8] = static_cast<std::byte>(static_cast<std::uint8_t>(tampered[8]) + 1U);
  failures += refuses(tampered, "a save of another format version is refused");
  {
    core::WorldState target;
    std::string reason;
    core::DecodeWorld(tampered, *tables, &target, &reason);
    failures += Expect(reason.find(std::to_string(core::kSaveFormatVersion)) != std::string::npos,
                       "the version refusal names this build's number");
  }

  tampered = bytes;
  tampered.resize(tampered.size() - 1);
  failures += refuses(tampered, "a truncated save is refused");

  tampered = bytes;
  tampered.back() = static_cast<std::byte>(static_cast<std::uint8_t>(tampered.back()) ^ 0xFFU);
  failures += refuses(tampered, "a save with a flipped byte fails its checksum");

  tampered = bytes;
  tampered.push_back(std::byte{0});
  failures += refuses(tampered, "a save with bytes glued to the end is refused");

  failures += refuses({}, "an empty file is refused");

  // -- THE REFUSALS BEHIND THE CHECKSUM ------------------------------------
  //
  // Every tampering above is caught by the payload hash before the reader
  // looks at a single section, so the codec's own refusals — a section
  // claiming more bytes than the payload holds, a table holding an id outside
  // its range, a staged batch claiming a million orders — had never been
  // executed by any test. Measured by coverage on 2026-09-16: five Refuse
  // sites in save.cpp, all cold (boss, standstill parcels 11, 14).
  //
  // A REAL BROKEN FILE IS NOT A FLIPPED BYTE. A save truncated by a full disk
  // or written by a build with a different idea of a row carries a hash that
  // MATCHES its own bytes; that is exactly the file these refusals exist for,
  // and reaching them means re-hashing after the damage, as the writer would
  // have done.
  const auto rehash = [](std::vector<std::byte>& file) {
    const std::uint64_t hash =
        Fnv1a64(std::span<const std::byte>(file).subspan(core::kSaveHeaderSize));
    for (std::size_t index = 0; index < 8; ++index) {
      file[44 + index] = static_cast<std::byte>((hash >> (8U * index)) & 0xFFU);
    }
  };
  const auto refusal_of = [&](const std::vector<std::byte>& broken) {
    core::WorldState target;
    std::string reason;
    const bool refused = !core::DecodeWorld(broken, *tables, &target, &reason);
    return refused ? reason : std::string();
  };

  // A section that claims more than the payload has left.
  tampered = bytes;
  {
    std::uint64_t length = 0;
    const std::size_t at = SectionAt(tampered, 1, length);  // the world block
    for (std::size_t index = 0; index < 8; ++index) {
      const std::uint64_t huge = 1ULL << 40U;
      tampered[at - 8 + index] = static_cast<std::byte>((huge >> (8U * index)) & 0xFFU);
    }
    rehash(tampered);
    const std::string reason = refusal_of(tampered);
    failures += Expect(
        reason.find("claims") != std::string::npos && reason.find("world") != std::string::npos,
        "a section claiming more bytes than the payload holds is refused by name");
  }

  // A row id of zero, which the id space never issues.
  tampered = bytes;
  {
    std::uint64_t length = 0;
    const std::size_t at = SectionAt(tampered, 2, length);  // the residents
    // next_id_value (u32), row count (u32), then the ids: the first one is
    // eight bytes in.
    for (std::size_t index = 0; index < 4; ++index) {
      tampered[at + 8 + index] = std::byte{0};
    }
    rehash(tampered);
    const std::string reason = refusal_of(tampered);
    failures += Expect(
        reason.find("residents") != std::string::npos && reason.find("id 0") != std::string::npos,
        "a table holding the id nobody issues is refused, and the id is named");
  }

  // A staged batch that claims more orders than the bytes could hold.
  tampered = bytes;
  {
    std::uint64_t length = 0;
    // The staged batch, and its INDEX moved from 16 to 17 on 2026-09-16 when
    // the livestock arrivals took a section of their own. A section index
    // written by hand is a length written by hand: it is right until the file
    // grows in the middle, and then it is quietly pointing at the neighbour.
    // And it did again on 2026-09-19 (save 79): the district's cars came
    // before the ledger, and the staged batch moved to 18. And a third time
    // on 2026-09-25 (save 92, the roads) — so the index is no longer written
    // by hand at all: it is the staged section's place in kRecordedPayload,
    // the list that already names every section in order.
    int staged_index = 0;
    for (std::size_t index = 0; index < kRecordedPayload.size(); ++index) {
      if (std::string_view(kRecordedPayload[index].name) == "staged") {
        staged_index = static_cast<int>(index);
      }
    }
    const std::size_t at = SectionAt(tampered, staged_index, length);  // the staged batch
    if (at != 0) {
      for (std::size_t index = 0; index < 4; ++index) {
        const std::uint32_t many = 1U << 24U;
        tampered[at + index] = static_cast<std::byte>((many >> (8U * index)) & 0xFFU);
      }
      rehash(tampered);
      const std::string reason = refusal_of(tampered);
      failures += Expect(reason.find("staged") != std::string::npos,
                         "a staged batch claiming more orders than the file holds is refused");
    } else {
      failures += Expect(false, "the staged section is where the format says it is");
    }
  }

  // A WHOLE SECTION GONE, which is a bigger question than any field and is
  // asked here because the answer was not obvious: THE SECTIONS OF THIS
  // FORMAT ARE POSITIONAL. The file carries a length before each one and no
  // name at all — the names in the codec are the words of its error messages,
  // nothing the reader matches against. So a section removed does not leave a
  // hole the reader trips over; it leaves every later section shifted one
  // place up, being read as its neighbour.
  //
  // The refusal that catches it is therefore not a name but a shape: the next
  // section's bytes do not decode as this one's rows. That is a real guard and
  // a weaker one than a name would be, and it is measured rather than assumed
  // (boss, parcel 28 — «что скажет круговой прогон, если из записи пропадёт
  // ЦЕЛАЯ СЕКЦИЯ?»).
  {
    std::uint64_t cut_length = 0;
    const std::size_t at = SectionAt(bytes, 9, cut_length);  // the limit's carts
    if (at != 0) {
      const auto head = static_cast<std::ptrdiff_t>(at - 8);
      const auto tail = static_cast<std::ptrdiff_t>(at + static_cast<std::size_t>(cut_length));
      std::vector<std::byte> cut_save(bytes.begin(), bytes.begin() + head);
      cut_save.insert(cut_save.end(), bytes.begin() + tail, bytes.end());
      rehash(cut_save);
      const std::string cut_reason = refusal_of(cut_save);
      failures += Expect(!cut_reason.empty(),
                         "a save with a whole section cut out of it is refused: the sections are "
                         "positional, so losing one is read as every later one moving up");
    } else {
      failures += Expect(false, "the limit deliveries section is where the format says it is");
    }
  }

  // TWO SECTIONS OF THE SAME LENGTH, SWAPPED. This is the case nothing in the
  // format could catch before the sections carried names, and it is the whole
  // measure of whether the names are real (boss, parcel 30).
  //
  // The length walk cannot see it: both sections are 24 bytes, so every later
  // offset lands exactly where it did. The recorded payload table cannot see
  // it either — it knows each section's length and hash but not its NAME,
  // because until now there was no name in the file to know. What is left is
  // the reader parsing one table's rows out of another table's bytes, and
  // whether that refuses is luck: two rows of the same width and compatible
  // fields load silently into the wrong tables.
  //
  // The two chosen are the stock bought on the limit and the couples waiting
  // for a house — 24 bytes each in this fixture, and nothing alike inside.
  {
    // EVERY PAIR, not one chosen pair. The first draft of this check picked
    // the two 24-byte sections and passed — which measured the fixture's luck
    // and not the format. The question is how many swaps the shape guard
    // catches and how many go through, and that is a number, not a guess.
    // ADJACENT SECTIONS SWAPPED WHOLE — length prefix and body together. This
    // is what a writer and a reader disagreeing about the ORDER look like on
    // the wire, and it is the mistake a name makes impossible. It does not
    // need two sections of equal length: moving the prefix with the body
    // keeps the walk consistent, so every later offset lands where it did.
    //
    // The first draft of this check swapped BODIES of equal length and found
    // exactly one such pair in the fixture, caught. That measured the
    // fixture's luck. This measures the format.
    int blind = 0;
    int caught = 0;
    for (int left_index = 2; left_index <= 14; ++left_index) {
      std::uint64_t left_length = 0;
      std::uint64_t right_length = 0;
      const std::size_t left = SectionAt(bytes, left_index, left_length);
      const std::size_t right = SectionAt(bytes, left_index + 1, right_length);
      if (left == 0 || right == 0) {
        continue;
      }
      const std::size_t left_from = left - 8;
      const std::size_t right_from = right - 8;
      const std::size_t right_to = right + static_cast<std::size_t>(right_length);
      std::vector<std::byte> swapped(bytes.begin(),
                                     bytes.begin() + static_cast<std::ptrdiff_t>(left_from));
      swapped.insert(swapped.end(),
                     bytes.begin() + static_cast<std::ptrdiff_t>(right_from),
                     bytes.begin() + static_cast<std::ptrdiff_t>(right_to));
      swapped.insert(swapped.end(),
                     bytes.begin() + static_cast<std::ptrdiff_t>(left_from),
                     bytes.begin() + static_cast<std::ptrdiff_t>(right_from));
      swapped.insert(
          swapped.end(), bytes.begin() + static_cast<std::ptrdiff_t>(right_to), bytes.end());
      if (swapped == bytes) {
        continue;  // identical neighbours: the swap is not a change at all
      }
      rehash(swapped);
      if (refusal_of(swapped).empty()) {
        ++blind;
        std::cout << "save: sections " << left_index << " and " << (left_index + 1)
                  << " change places WITHOUT A WORD\n";
      } else {
        ++caught;
      }
    }
    // MEASURED 2026-09-17: thirteen caught, none blind. The format carries no
    // names — a section is found by walking lengths — so what refuses is not
    // a name but a SHAPE: the neighbour's bytes do not decode as this
    // section's rows. Every pair in this world happens to disagree in shape.
    //
    // THAT IS LUCK, AND THIS CHECK IS HERE TO SAY WHEN IT RUNS OUT. A section
    // added whose rows cross-parse with its neighbour's would change places
    // in silence, and nothing else in the suite would notice: the length walk
    // lands on the same offsets and the recorded payload table knows each
    // section's length and hash but not which section it is, because there is
    // nothing in the file to know it by.
    //
    // So the number is the point. While it reads "none blind" the guard the
    // format has is enough; the day it reads otherwise, the format needs
    // names and this line is the evidence for the version that adds them.
    std::cout << "save: sections that change places — caught " << caught << ", blind " << blind
              << '\n';
    failures += Expect(blind == 0,
                       "no two sections change places unnoticed: the format has no names, so this "
                       "is the shape guard's luck holding — and the line that says when it stops");
  }

  // A DEFINITION ID PAST THE SAVE'S OWN DICTIONARY. Every definition travels
  // as an index into the save's own list of keys, so that a reordered table
  // cannot turn the chairman's rye into somebody's flax.
  //
  // AND THE ANSWER IS NOT THE REFUSAL I CAME LOOKING FOR. `LoadSource::
  // ReadDefId` has one — "the save names livestock row N, its own dictionary
  // has M" — and coverage listed it as never executed. It is never executed
  // because A STRONGER GUARD STANDS IN FRONT OF IT: the table reader checks
  // every id against the live range first and says "table 'herds' holds id
  // 32766, outside 1..1". The cold edge is SHADOWED, not missing, and that is
  // a different fact with a different repair — none.
  //
  // Which is why this asserts what actually happens rather than what was
  // predicted. A test written to the prediction would have been green about
  // the wrong guard.
  //
  // The first herd row begins with its kind, and a table section begins with
  // its row count, so the two bytes after the count are that id.
  {
    std::uint64_t herds_length = 0;
    const std::size_t at = SectionAt(bytes, 6, herds_length);  // the herds
    if (at != 0 && herds_length > 10) {
      std::vector<std::byte> wild = bytes;
      wild[at + 8] = std::byte{0xFE};
      wild[at + 9] = std::byte{0x7F};  // large, and not the invalid id
      rehash(wild);
      const std::string reason = refusal_of(wild);
      failures += Expect(
          reason.find("holds id") != std::string::npos && reason.find("herds") != std::string::npos,
          "a definition id no table row can carry is refused by name and range — "
          "and it is this guard that answers, not the dictionary's own");
    } else {
      failures += Expect(false, "the herds section is where the format says it is");
    }
  }

  // A KEY THE TABLES NO LONGER CARRY, which is the other half of the same
  // contract and the half that happens for real: the save is honest, the
  // BALANCE moved under it. The refusal names the key and the table, because
  // "the save is broken" would send somebody looking at the file when the
  // answer is in the design db.
  {
    const std::filesystem::path thinner = root / "thinner";
    std::filesystem::create_directories(thinner);
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(root)) {
      if (entry.is_regular_file()) {
        std::filesystem::copy_file(entry.path(),
                                   thinner / entry.path().filename(),
                                   std::filesystem::copy_options::overwrite_existing);
      }
    }
    WriteTableFile(thinner / "livestock.csv", {"cow"}, "feed_units_per_real_day");
    const auto fewer = core::LoadTableSet(thinner.string(), nullptr);
    if (fewer != nullptr) {
      core::WorldState target;
      std::string reason;
      const bool refused = !core::DecodeWorld(bytes, *fewer, &target, &reason);
      failures += Expect(refused && reason.find("no longer has") != std::string::npos,
                         "a save naming a key the tables have since lost is refused BY THAT KEY'S "
                         "NAME — the file is right and the balance moved under it");
    } else {
      failures += Expect(false, "the thinned table set loads as tables");
    }
  }

  // -- the file wrappers ---------------------------------------------------
  const std::filesystem::path file = root / "campaign.kls";
  failures += Expect(core::SaveWorldToFile(world, *tables, file.string(), &error),
                     "a world writes to a file");
  core::WorldState from_file;
  failures += Expect(core::LoadWorldFromFile(file.string(), *tables, &from_file, &error),
                     "and reads back from it");
  failures += Expect(core::EncodeWorld(from_file, *tables) == bytes,
                     "the file round trip is the same file");
  const auto file_info = core::PeekSaveFile(file.string());
  failures += Expect(file_info.has_value() && file_info->tick == world.calendar.tick,
                     "a file's header reads without loading it");
  failures += Expect(!core::PeekSaveFile((root / "no-such-file").string()).has_value(),
                     "peeking a missing file says nothing rather than lying");

  std::filesystem::remove_all(root);
  if (failures == 0) {
    std::cout << "unit_core_save: all checks passed (" << bytes.size() << " bytes for the world)\n";
  }
  return failures;
}
