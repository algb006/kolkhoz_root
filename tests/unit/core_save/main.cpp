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
  world.epoch = core::Epoch::kTwo;
  world.world_seed = 0x0BADC0FFEEULL;
  world.rng = core::SeedRngState(world.world_seed, 3);
  core::NextRandomBits(world.rng);
  world.chairman.raikom_reputation = 61.5F;
  world.plan.due = Amounts({7'000'000, 0, 0, 0, 0, 0});
  world.plan.delivered = Amounts({1'500'000, 0, 0});
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
  first.offense_count = 2;
  first.traits = 0xBEEF;
  const core::ResidentId first_id = core::AppendRow(world.residents, first);

  core::ResidentRow second;
  second.sex = core::Sex::kFemale;
  second.birth_day = 12;
  second.mother = first_id;
  core::AppendRow(world.residents, second);
  core::ResidentRow third;
  const core::ResidentId third_id = core::AppendRow(world.residents, third);
  // A death: the id is spent and must never be reissued, so next_id_value
  // has to survive the save on its own (state_table.h).
  core::RemoveRow(world.residents, third_id);

  core::FamilyRow rich;
  rich.pantry = Amounts({400'000, 0, 900'000, 60'000, 0, 0});
  rich.satiety_year_mean = 88.5F;
  rich.food_variety_mask = 0b1011;
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
  field.weather_stress = 0.125F;
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
  core::FieldRow derelict;
  derelict.kind = core::LandKind::kDerelict;  // the top of the enum: its bound is checked too
  derelict.area_ga = 45.0F;
  derelict.fertility = 65.0F;
  core::AppendRow(world.fields, derelict);

  core::UnitRow barn;
  barn.type = core::UnitTypeId{0};
  barn.position = core::Vec2{.x = 10.0F, .y = 20.0F};
  barn.stock = Amounts({12'000'000, 3'000'000, 0, 0, 165'000'000, 0});
  core::AppendRow(world.units, barn);
  core::UnitRow house;
  house.type = core::UnitTypeId{1};
  house.level = 2;
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
  core::AppendRow(world.units, site);

  core::HerdRow herd;
  herd.kind = core::LivestockKindId{0};
  herd.unit = core::UnitId{1};
  herd.adult_count = 39;
  herd.adult_male_count = 2;
  herd.billeted_count = 4;
  herd.adult_age_game_years_total = 137.5F;
  herd.hunger_progress = 0.375F;
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
  // The top of each enum: their bounds are checked on the way in.
  refused.kind = core::OrderKind::kDemolishUnit;
  refused.status = core::OrderStatus::kCancelled;
  refused.refusal = core::OrderRefusal::kNotEmpty;
  refused.issued_tick = 69;
  refused.unit = core::UnitId{1};
  core::AppendRow(world.orders, refused);

  world.ledger.closed.year = 2;
  world.ledger.closed.births = 6;
  world.ledger.closed.deaths = 3;
  world.ledger.closed.harvest = Amounts({43'000'000, 5'000'000, 0, 0, 0, 0});
  world.ledger.closed.eaten = Amounts({0, 0, 9'000'000, 1'200'000, 0, 0});
  world.ledger.closed.work_days_by_kind[static_cast<std::size_t>(core::WorkKind::kHarvest)] =
      241.5F;
  world.ledger.closed.trudodni_burned = 4200;
  world.ledger.current.year = 0;
  world.ledger.current.births = 1;
  return world;
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
  failures +=
      Expect(loaded.plan.delivered.size() == 3, "a short dense vector was not silently padded");
  failures +=
      Expect(loaded.ledger.closed.year == 2 && loaded.ledger.closed.trudodni_burned == 4200 &&
                 loaded.ledger.current.births == 1,
             "both ledger books came back");

  // The order book: a campaign saved with an order waiting resumes with it
  // waiting, and the waiting row keeps every field the consumer will read.
  failures += Expect(
      loaded.orders.rows.size() == 4 && loaded.orders.next_id_value == world.orders.next_id_value,
      "the order book came back whole");
  failures += Expect(loaded.orders.rows[0].kind == core::OrderKind::kAssignWork &&
                         loaded.orders.rows[0].status == core::OrderStatus::kAccepted &&
                         loaded.orders.rows[0].work == core::WorkKind::kHarvest &&
                         loaded.orders.rows[0].issued_tick == 71 &&
                         loaded.orders.rows[0].resident.value == 1,
                     "a waiting order kept its status, its target and the tick it was issued on");
  failures += Expect(loaded.orders.rows[2].position.x == -12.5F &&
                         loaded.orders.rows[3].refusal == core::OrderRefusal::kNotEmpty,
                     "the build order's position and the refusal reason survived");

  // The site came back mid-build, every field of it.
  const core::UnitRow& site_back = loaded.units.rows[2];
  failures += Expect(site_back.level == 0 &&
                         site_back.construction.phase == core::ConstructionPhase::kBuilding &&
                         site_back.construction.target_level == 1 &&
                         site_back.construction.labor_days_total == 17.5F &&
                         site_back.construction.labor_days_remaining == 6.25F &&
                         site_back.construction.max_crew == 8,
                     "a half-built unit resumes half-built, crew ceiling included");

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
