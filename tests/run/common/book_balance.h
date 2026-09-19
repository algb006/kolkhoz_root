/// @file
/// @brief The book's balance: every gram of a harvested resource on a line.
/// @threading SINGLE_THREADED
/// A run-test tally, read between steps on the run's own thread.
///
/// WHY (boss seq 162, 165; host core-host-l1 seq 39): a run found 130 t of
/// vegetables «in no column», and the answer was a heap on a field that the
/// reader's book did not count. The same instrument, pointed at every
/// harvested resource, found three doors that DID let grams out with no line
/// — straw into buildings, the goats' hay, and a distiller's raw material
/// read as a transfer. The next such door is found by this, not by a lucky
/// run.
///
/// THE BALANCE, per resource the fields have ever given, at each year's turn:
/// what the village holds — every unit's stock, every family's pantry, every
/// field's reaped heap, the district's carts — has moved by exactly the
/// year's inflows less its outflows. A transfer inside the village (the
/// issue, the ration, the samogon price paid pantry to pantry) moves a gram
/// between two holdings the census counts both of, and is in neither sum.
/// The stolen raw material is an OUTflow: the distiller carries it off and
/// it becomes drink, which the model does not hold as a resource.

#ifndef KOLKHOZ_TESTS_RUN_COMMON_BOOK_BALANCE_H_
#define KOLKHOZ_TESTS_RUN_COMMON_BOOK_BALANCE_H_

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class BookBalance {
 public:
  /// Grams a year's balance may miss by before it is a door: the pantry's
  /// share arithmetic rounds per family per day.
  static constexpr double kToleranceGrams = 1000.0;

  explicit BookBalance(const core::WorldState& start) : held_(Held(start)) {}

  /// @brief Checks the year that has just closed (world.ledger.closed) and
  /// rolls the census over. Prints each resource with a residual.
  /// @return The failures: one per resource whose residual passes the
  /// tolerance.
  int CloseYear(const core::WorldState& world, const core::ITableSet& tables) {
    const core::YearLedger& book = world.ledger.closed;
    const std::vector<double> now = Held(world);
    const core::ITable* const resources = tables.FindTable("resources");
    int failures = 0;
    std::uint32_t checked = 0;
    for (std::size_t r = 0; r < now.size(); ++r) {
      if (r >= harvested_.size()) {
        harvested_.resize(r + 1, false);
      }
      harvested_[r] = harvested_[r] || At(book.harvest, r) > 0.0;
      if (!harvested_[r]) {
        continue;
      }
      ++checked;
      const double before = r < held_.size() ? held_[r] : 0.0;
      const double residual = (now[r] - before) - NetFlow(book, r);
      if (std::fabs(residual) > kToleranceGrams) {
        const std::string name =
            resources != nullptr
                ? std::string(resources->CellText(static_cast<std::uint32_t>(r), 0))
                : std::to_string(r);
        std::cout << "book balance, year " << book.year << ": " << name << " moved "
                  << (now[r] - before) / 1e6 << " t against the book's " << NetFlow(book, r) / 1e6
                  << " t — " << residual / 1e6 << " t through a door with no line\n";
        ++failures;
      }
    }
    // The count beside the answer: a balance over no resource is no check.
    std::cout << "book balance, year " << book.year << ": " << checked << " harvested resources, "
              << failures << " off the book\n";
    held_ = now;
    return failures;
  }

 private:
  static double At(const core::ResourceAmounts& amounts, std::size_t index) {
    return index < amounts.size() ? static_cast<double>(amounts[index]) : 0.0;
  }

  static std::vector<double> Held(const core::WorldState& world) {
    std::size_t count = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      count = unit.stock.size() > count ? unit.stock.size() : count;
    }
    for (const core::FamilyRow& family : world.families.rows) {
      count = family.pantry.size() > count ? family.pantry.size() : count;
    }
    for (const core::LimitDeliveryRow& cart : world.limit_deliveries.rows) {
      count = cart.goods.size() > count ? cart.goods.size() : count;
    }
    for (const core::FieldRow& field : world.fields.rows) {
      if (field.reaped_grams > 0 && field.reaped_resource.value >= count) {
        count = field.reaped_resource.value + 1U;
      }
    }
    std::vector<double> held(count, 0.0);
    for (const core::UnitRow& unit : world.units.rows) {
      for (std::size_t r = 0; r < unit.stock.size(); ++r) {
        held[r] += static_cast<double>(unit.stock[r]);
      }
    }
    for (const core::FamilyRow& family : world.families.rows) {
      for (std::size_t r = 0; r < family.pantry.size(); ++r) {
        held[r] += static_cast<double>(family.pantry[r]);
      }
    }
    for (const core::FieldRow& field : world.fields.rows) {
      if (field.reaped_grams > 0) {
        held[field.reaped_resource.value] += static_cast<double>(field.reaped_grams);
      }
    }
    for (const core::LimitDeliveryRow& cart : world.limit_deliveries.rows) {
      for (std::size_t r = 0; r < cart.goods.size(); ++r) {
        held[r] += static_cast<double>(cart.goods[r]);
      }
    }
    return held;
  }

  /// The year's net inflow by the book (see @file for what is not in it).
  static double NetFlow(const core::YearLedger& book, std::size_t r) {
    const double in = At(book.harvest, r) + At(book.plot_harvest, r) + At(book.yard_produce, r) +
                      At(book.nets, r) + At(book.night_catch, r) + At(book.herd_produce, r) +
                      At(book.made, r);
    const double out = At(book.eaten, r) + At(book.spoiled, r) + At(book.lost_no_room, r) +
                       At(book.seed, r) + At(book.feed, r) + At(book.yard_feed, r) +
                       At(book.delivered, r) + At(book.seized, r) + At(book.stolen, r) +
                       At(book.built_in, r) + At(book.processed, r);
    return in - out;
  }

  std::vector<double> held_;
  std::vector<bool> harvested_;
};

}  // namespace run

#endif  // KOLKHOZ_TESTS_RUN_COMMON_BOOK_BALANCE_H_
