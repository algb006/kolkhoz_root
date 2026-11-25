/// @file
/// @brief PLANLOSS2157 (#2157's measurement, econ's verdict boss-econ-2157-plan-losses [2]; boss
///        25 Nov): per village-year, years 1-5, each plan position - due, delivered, met - and
///        where its produce went: the stores' losses (stolen, spoiled, samogon paid) against
///        the issue, the eating and the harvest, from the closed book; the day the plan rung
///        BINDS (the produce lying first at or under what is still owed) and the loss rate
///        then (stolen + spoiled, kg a day over the four days from it - the village's, not a
///        store's: the book keeps no store); the produce lying on the year's first day; and
///        the first day a watchman post can be held (a built unit whose staff names one) and
///        is held. Test-side: the world is unchanged.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.

#ifndef TESTS_RUN_COMMON_PLAN_LOSS_TALLY_H_
#define TESTS_RUN_COMMON_PLAN_LOSS_TALLY_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class PlanLossTally {
 public:
  explicit PlanLossTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      for (const char* name : {"rye",
                               "wheat",
                               "oat",
                               "barley",
                               "buckwheat",
                               "pea",
                               "potato",
                               "vegetables",
                               "cabbage",
                               "milk",
                               "meat",
                               "eggs",
                               "wool",
                               "hay",
                               "flax",
                               "honey"}) {
        const std::uint32_t row = resources->FindRowByKey(name);
        if (row != core::kNoTableRow) {
          names_[row] = name;
        }
      }
    }
    if (const core::ITable* const professions = tables.FindTable("professions")) {
      watchman_ = professions->FindRowByKey("watchman");
    }
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      for (const char* name : {"food_yard", "utility_yard", "crafts_yard", "orchard"}) {
        const std::uint32_t row = types->FindRowByKey(name);
        if (row != core::kNoTableRow) {
          watched_types_.push_back(row);
        }
      }
    }
  }

  /// @brief Once a day, after the day's step.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    const std::uint32_t day_of_year = day % core::kDaysPerYear;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (year > kYears + 1U) {
      return;
    }
    // THE YEAR GONE: its book is closed now; its plan was read yesterday (today's is new).
    if (day_of_year == 0 && day > 0 && year - 1U <= kYears) {
      Close(world, year - 1U);
    }
    if (year > kYears) {
      return;
    }
    Year& row = by_year_[year];
    const std::size_t count = world.plan.due.size();
    row.positions.resize(count);
    for (std::size_t at = 0; at < count; ++at) {
      Position& position = row.positions[at];
      const core::Grams lying = Lying(world, at);
      if (day_of_year == 0) {
        position.start_lying = lying;
      }
      if (world.plan.announced == 0) {
        continue;
      }
      position.due = world.plan.due[at];
      position.delivered = at < world.plan.delivered.size() ? world.plan.delivered[at] : 0;
      const core::Grams owed =
          position.due > position.delivered ? position.due - position.delivered : 0;
      const core::Grams losses = Losses(world.ledger.current, at);
      if (position.binding_day < 0 && owed > 0 && lying <= owed) {
        position.binding_day = static_cast<std::int32_t>(day_of_year) + 1;
        position.losses_at_binding = losses;
      }
      if (position.binding_day > 0 &&
          static_cast<std::int32_t>(day_of_year) + 1 == position.binding_day + kRateDays) {
        position.loss_kg_a_day =
            static_cast<double>(losses - position.losses_at_binding) / 1000.0 / kRateDays;
      }
    }
    // The watchman: a unit that can hold one, and a resident who holds it.
    if (row.watch_post_day < 0) {
      for (const core::UnitRow& unit : world.units.rows) {
        bool watched = false;
        for (const std::uint32_t type : watched_types_) {
          watched = watched || unit.type.value == type;
        }
        if (watched && unit.level >= 1 &&
            unit.construction.phase == core::ConstructionPhase::kNone) {
          row.watch_post_day = static_cast<std::int32_t>(day_of_year) + 1;
          break;
        }
      }
    }
    if (row.watchman_day < 0 && watchman_ != core::kNoTableRow) {
      for (const core::ResidentRow& person : world.residents.rows) {
        if (person.post.profession.value == watchman_) {
          row.watchman_day = static_cast<std::int32_t>(day_of_year) + 1;
          break;
        }
      }
    }
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [year, row] : by_year_) {
      if (!row.closed) {
        continue;
      }
      out << "population_curve: PLANLOSS2157 seed " << seed << " year " << year
          << " ground=closed_book;kg;binding=lying<=owed;rate=village(stolen+spoiled)/day_over_"
          << kRateDays << "_days;watch=food/utility/crafts_yard,orchard"
          << " watch_post_day " << row.watch_post_day << " watchman_day " << row.watchman_day;
      for (std::size_t at = 0; at < row.positions.size(); ++at) {
        const Position& p = row.positions[at];
        if (p.due <= 0) {
          continue;
        }
        const auto name = names_.find(static_cast<std::uint32_t>(at));
        out << " [" << (name != names_.end() ? name->second : "r" + std::to_string(at)) << " due "
            << Kg(p.due) << " delivered " << Kg(p.delivered) << " met "
            << (p.delivered >= p.due ? 1 : 0) << " start_lying " << Kg(p.start_lying) << " harvest "
            << Kg(p.harvest) << " issued " << Kg(p.issued) << " eaten " << Kg(p.eaten) << " stolen "
            << Kg(p.stolen) << " spoiled " << Kg(p.spoiled) << " samogon_paid " << Kg(p.samogon)
            << " binding_day " << p.binding_day << " loss_kg_a_day " << p.loss_kg_a_day << "]";
      }
      out << '\n';
    }
  }

 private:
  static constexpr std::uint32_t kYears = 5;
  static constexpr std::int32_t kRateDays = 4;

  struct Position {
    core::Grams due = 0;
    core::Grams delivered = 0;
    core::Grams start_lying = 0;
    core::Grams harvest = 0;
    core::Grams issued = 0;
    core::Grams eaten = 0;
    core::Grams stolen = 0;
    core::Grams spoiled = 0;
    core::Grams samogon = 0;
    std::int32_t binding_day = -1;
    core::Grams losses_at_binding = 0;
    double loss_kg_a_day = -1.0;
  };

  struct Year {
    std::vector<Position> positions;
    std::int32_t watch_post_day = -1;
    std::int32_t watchman_day = -1;
    bool closed = false;
  };

  static double Kg(core::Grams grams) { return static_cast<double>(grams) / 1000.0; }

  static core::Grams At(const core::ResourceAmounts& amounts, std::size_t at) {
    return at < amounts.size() ? amounts[at] : 0;
  }

  static core::Grams Losses(const core::YearLedger& book, std::size_t at) {
    return At(book.stolen, at) + At(book.spoiled, at);
  }

  static core::Grams Lying(const core::WorldState& world, std::size_t at) {
    core::Grams lying = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      lying += At(unit.stock, at);
    }
    return lying;
  }

  void Close(const core::WorldState& world, std::uint32_t year) {
    const auto found = by_year_.find(year);
    if (found == by_year_.end()) {
      return;
    }
    Year& row = found->second;
    const core::YearLedger& book = world.ledger.closed;
    for (std::size_t at = 0; at < row.positions.size(); ++at) {
      Position& p = row.positions[at];
      p.harvest = At(book.harvest, at);
      p.issued = At(book.issued, at);
      p.eaten = At(book.eaten, at);
      p.stolen = At(book.stolen, at);
      p.spoiled = At(book.spoiled, at);
      p.samogon = At(book.samogon_paid, at);
      p.delivered = std::max(p.delivered, At(book.delivered, at));
    }
    row.closed = true;
  }

  std::map<std::uint32_t, std::string> names_;
  std::uint32_t watchman_ = core::kNoTableRow;
  std::vector<std::uint32_t> watched_types_;
  std::map<std::uint32_t, Year> by_year_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_PLAN_LOSS_TALLY_H_
