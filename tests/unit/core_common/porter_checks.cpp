// The porter's rules and the hauled book (#671; core_common/haul.h WalkerTripGrams and
// LogPairMayCarry, core_common/hauled_book.h). Called from main.cpp (TestThePorter).
//
// THE KNOWN ANSWERS, by hand from transport design §2 «Как носят» and boss's answers of 12
// October 2026: 20 kg alone for bulk, packed and boards; 10 kg a bundle; 40 kg a stretcher's
// pair, 20 kg a head; 20 kg the odd hand on manure; a 200 kg log 100 kg a head; nothing for the
// live, the people's cart or no load. The log gate opens only with all three terms. The book
// splits a settle's grams by whose units the drain was, and the two halves sum to the grams.
#include <cstdint>
#include <iostream>

#include "core_common/haul.h"
#include "core_common/hauled_book.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/load_resource.h"
#include "core_common/logistics_rules.h"
#include "core_common/logistics_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"

namespace core_test {
namespace {

int ExpectPorter(bool condition, const char* label) {
  if (condition) {
    return 0;
  }
  std::cout << "FAIL: porter: " << label << '\n';
  return 1;
}

constexpr core::Grams kKg = 1000;

int CheckTripGrams() {
  int failures = 0;
  const core::WalkerCarry carry{.adult = 20 * kKg, .rick = 10 * kKg, .stretcher_pair = 40 * kKg};
  const core::Grams log = 200 * kKg;
  using core::CargoForm;
  using core::WalkerHold;
  failures += ExpectPorter(
      core::WalkerTripGrams(CargoForm::kBulk, WalkerHold::kAlone, carry, log) == 20 * kKg,
      "a sack of bulk is 20 kg");
  failures += ExpectPorter(
      core::WalkerTripGrams(CargoForm::kPacked, WalkerHold::kAlone, carry, log) == 20 * kKg,
      "a packed load is 20 kg");
  failures += ExpectPorter(
      core::WalkerTripGrams(CargoForm::kRick, WalkerHold::kAlone, carry, log) == 10 * kKg,
      "a bundle of hay or straw is 10 kg - half a sack");
  failures += ExpectPorter(
      core::WalkerTripGrams(CargoForm::kManure, WalkerHold::kStretcher, carry, log) == 20 * kKg,
      "a stretcher of manure is 40 kg a pair, 20 kg a head");
  failures += ExpectPorter(
      core::WalkerTripGrams(CargoForm::kManure, WalkerHold::kAlone, carry, log) == 20 * kKg,
      "the odd hand on manure carries it as bulk, 20 kg");
  failures += ExpectPorter(
      core::WalkerTripGrams(CargoForm::kLong, WalkerHold::kAlone, carry, log) == 20 * kKg,
      "boards on the shoulder are 20 kg");
  failures += ExpectPorter(
      core::WalkerTripGrams(CargoForm::kLong, WalkerHold::kLogPair, carry, log) == 100 * kKg,
      "a 200 kg log by two is 100 kg a head");
  failures += ExpectPorter(
      core::WalkerTripGrams(CargoForm::kLive, WalkerHold::kAlone, carry, log) == 0 &&
          core::WalkerTripGrams(CargoForm::kPeople, WalkerHold::kAlone, carry, log) == 0 &&
          core::WalkerTripGrams(CargoForm::kNone, WalkerHold::kAlone, carry, log) == 0,
      "the live are led, not carried; no load is no grams");
  return failures;
}

int CheckTheLogGate() {
  int failures = 0;
  failures += ExpectPorter(core::LogPairMayCarry(true, false, 300.0F, 300.0F),
                           "a works waiting for logs, no horse free, 300 m: the pair carries");
  failures += ExpectPorter(!core::LogPairMayCarry(true, false, 301.0F, 300.0F),
                           "one metre beyond the leg's limit: no pair");
  failures += ExpectPorter(!core::LogPairMayCarry(true, true, 100.0F, 300.0F),
                           "a horse free in the morning: the cart takes the logs, no pair");
  failures += ExpectPorter(!core::LogPairMayCarry(false, false, 100.0F, 300.0F),
                           "a works not waiting for logs: no pair");
  return failures;
}

int CheckTheHauledBook() {
  int failures = 0;
  core::WorldState world;
  core::LogisticsTaskRow task;
  task.load_kind = core::LogisticsLoadKind::kFieldHeap;
  task.field = core::FieldId{7};
  core::AppendRow(world.logistics_tasks, task);
  core::WorkAssignment load;
  load.kind = core::WorkKind::kHauling;
  load.field = core::FieldId{7};
  core::WorkAssignment other = load;
  other.field = core::FieldId{8};

  failures += ExpectPorter(
      core::FootShareOf(world, load, true) == 0.0F && core::FootShareOf(world, load, false) == 1.0F,
      "nothing noted: by cart while the settlement has carts, on foot while not");
  core::NoteHaulDrain(world, load, 0.25F, /*on_foot=*/true);
  core::NoteHaulDrain(world, load, 0.75F, /*on_foot=*/false);
  core::NoteHaulDrain(world, other, 5.0F, /*on_foot=*/true);  // no task: noted nowhere
  failures += ExpectPorter(core::FootShareOf(world, load, true) == 0.25F,
                           "a quarter of the units on foot is a quarter of the grams");
  failures += ExpectPorter(core::FootShareOf(world, other, true) == 0.0F,
                           "a load with no task books by the settlement's carriers");

  core::YearLedger book;
  const core::ResourceId potato{3};
  core::BookHauled(book, potato, 1001, core::FootShareOf(world, load, true));
  failures += ExpectPorter(
      core::AmountOf(book.hauled_on_foot, potato) + core::AmountOf(book.hauled_by_cart, potato) ==
          1001,
      "the two books sum to the grams moved, to the gram");
  failures += ExpectPorter(core::AmountOf(book.hauled_on_foot, potato) == 250,
                           "on foot: a quarter of 1001 g, rounded once");

  // A settle the room held back: half converted keeps half noted, in the same ratio.
  core::SpendHaulNotes(world, load, 0.5F);
  const core::LogisticsTaskRow& kept = world.logistics_tasks.rows[0];
  failures += ExpectPorter(kept.drained_on_foot == 0.125F && kept.drained_by_cart == 0.375F,
                           "half converted: half the notes kept, the ratio unchanged");
  core::SpendHaulNotes(world, load, 1.0F);
  failures += ExpectPorter(kept.drained_on_foot == 0.0F && kept.drained_by_cart == 0.0F,
                           "all converted: the notes spent to nought");
  return failures;
}

// THE RULES AS DATA (#671, the second delivery; PorterRules). By hand: the bundle 10 kg, the
// stretcher's head 20, the sack 20; a resource the table gives NO form is a sack, never nought;
// a hand-made config (no table) carries every form as the 20-kg sack the world had before.
int CheckTheLoadByForm() {
  int failures = 0;
  core::PorterRules rules;
  failures += ExpectPorter(core::WalkerLoadGrams(rules, core::ResourceId{0}) == 20 * kKg &&
                               core::WalkerLoadGrams(rules, core::ResourceId{}) == 20 * kKg,
                           "a config with no table carries any load as the 20-kg sack");
  rules.carry = core::WalkerCarry{.adult = 20 * kKg, .rick = 10 * kKg, .stretcher_pair = 40 * kKg};
  using core::CargoForm;
  // Resources 0..5: bulk, rick, manure, long, live, and one the table left formless.
  rules.cargo_form = {CargoForm::kBulk,
                      CargoForm::kRick,
                      CargoForm::kManure,
                      CargoForm::kLong,
                      CargoForm::kLive,
                      CargoForm::kNone};
  const auto load = [&rules](std::uint16_t resource) {
    return core::WalkerLoadGrams(rules, core::ResourceId{resource});
  };
  failures += ExpectPorter(load(0) == 20 * kKg, "bulk by form: the sack, 20 kg");
  failures += ExpectPorter(load(1) == 10 * kKg, "a rick by form: the bundle, 10 kg");
  failures += ExpectPorter(load(2) == 20 * kKg, "manure by form: a stretcher's head, 20 kg");
  failures += ExpectPorter(load(3) == 20 * kKg, "long by form: boards on the shoulder, 20 kg");
  failures += ExpectPorter(load(4) == 20 * kKg && load(5) == 20 * kKg && load(9) == 20 * kKg,
                           "a load with no carried form is priced as a sack, never as nought");
  failures += ExpectPorter(core::CargoFormOf(rules, core::ResourceId{1}) == CargoForm::kRick &&
                               core::CargoFormOf(rules, core::ResourceId{9}) == CargoForm::kNone,
                           "the form is the table's, kNone past it");
  return failures;
}

// THE PAIR UNDER A LOG, by hand: a walker's round of 2 h (1 h out, 1 h back) becomes 1 h out and
// 2 h back at half pace - 3 h - with half a 200-kg log a head. Its share of a cart-day of 1800 kg
// whose round is 1/12 + 1/9 h a km against the pair's (1/5)(1 + 2) h a km:
// (100 / 1800) x ((1/12 + 1/9) x 12 / (0.6 x 12)) with hours a km x 12 (the clock's scale).
int CheckTheLogPair() {
  int failures = 0;
  core::PorterRules rules;
  rules.log = core::ResourceId{3};
  rules.log_piece_grams = 200 * kKg;
  rules.log_pair_loaded_pace = 0.5F;
  core::HaulRate walked;
  walked.load = 20 * kKg;
  walked.round_trip_hours = 2.0F;
  const core::HaulRate pair = core::LogPairRate(rules, walked);
  failures += ExpectPorter(pair.load == 100 * kKg, "a pair's head carries half the log, 100 kg");
  failures += ExpectPorter(pair.round_trip_hours > 2.999F && pair.round_trip_hours < 3.001F,
                           "the loaded half at half pace: a 2-hour round becomes 3");
  core::PorterRules no_log = rules;
  no_log.log_piece_grams = 0;
  failures += ExpectPorter(core::LogPairRate(no_log, walked).load == walked.load,
                           "rules with no log piece leave the rate as it was");
  const float walk = 12.0F / 5.0F;
  const float trot = 12.0F / 12.0F;
  const float loaded = 12.0F / 9.0F;
  const float share = core::LogPairShareOfCartDay(rules, 1800 * kKg, walk, trot, loaded);
  const float by_hand = (100.0F / 1800.0F) * ((trot + loaded) / (walk * 3.0F));
  failures += ExpectPorter(share > by_hand * 0.999F && share < by_hand * 1.001F,
                           "a pair head's share of a cart-day is half a log over the cart's "
                           "load by the rounds");
  failures +=
      ExpectPorter(core::LogPairShareOfCartDay(no_log, 1800 * kKg, walk, trot, loaded) == 1.0F,
                   "and one, not nought, when the rules name no log");
  return failures;
}

// ONE RESOURCE A TRIP: a works' supply is a walker's by the FIRST resource its front leg holds,
// a cart's by the one of MOST grams.
int CheckTheLoadResource() {
  int failures = 0;
  core::WorldState world;
  core::UnitRow site;
  core::SupplyLeg leg;
  leg.grams = {0, 500 * kKg, 0, 4000 * kKg};  // resource 1 first, resource 3 the most
  site.construction.legs.push_back(leg);
  const core::UnitId unit = core::AppendRow(world.units, site);
  core::LogisticsTaskRow task;
  task.load_kind = core::LogisticsLoadKind::kSiteSupply;
  task.unit = unit;
  failures += ExpectPorter(core::WalkerLoadResource(world, task, core::ResourceId{9}).value == 1,
                           "a walker's trip on a works' supply is the front leg's first resource");
  failures += ExpectPorter(core::TaskLoadResource(world, task, core::ResourceId{9}).value == 3,
                           "a cart's leg on it is named by the resource of most grams");
  world.units.rows[0].construction.legs.clear();
  failures += ExpectPorter(
      core::WalkerLoadResource(world, task, core::ResourceId{9}).value == core::kInvalidDefIdValue,
      "a supply with no leg left has no resource to carry");
  core::LogisticsTaskRow logs;
  logs.load_kind = core::LogisticsLoadKind::kStandLogs;
  failures += ExpectPorter(core::WalkerLoadResource(world, logs, core::ResourceId{9}).value == 9,
                           "a stand's load is the log, whoever asks");
  return failures;
}

}  // namespace

int TestThePorter() {
  return CheckTripGrams() + CheckTheLogGate() + CheckTheHauledBook() + CheckTheLoadByForm() +
         CheckTheLogPair() + CheckTheLoadResource();
}

}  // namespace core_test
