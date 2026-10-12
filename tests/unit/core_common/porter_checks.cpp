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

}  // namespace

int TestThePorter() {
  return CheckTripGrams() + CheckTheLogGate() + CheckTheHauledBook();
}

}  // namespace core_test
