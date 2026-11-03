/// @file
/// @brief The herds of a canon village, y1-y6 - #1374's pair print (herds
/// without sex; boss's gates: heads, births, young-cull meat, hay, milk and
/// the yards' pork within 2 SE).
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHAT IT COUNTS, from the closed books and the herds at each year's turn:
/// the book's births and deaths (age, hunger) and the frozen; by kind (cow,
/// sheep, pig, horse, goat) the young culled at maturing and the autumn
/// slaughter, in heads; the kolkhoz milk into the stores (herd_produce), t;
/// the hay the herds ate (herd_hay_eaten), t; the adult heads at the turn of
/// y3 and y6, the kolkhoz's and the yards' apart (household_owned).
/// GROUND: the kolkhoz milk only - a yard cow's milk lands in its family's
/// pantry and is not in herd_produce; the yards' pork is read as the yards'
/// pig heads at the turn and the autumn slaughter of every pig herd (the
/// book does not split the slaughter by owner).
/// Compiles on both trees of the pair: before #1374 the young-cull slot was
/// named herd_males_culled (the same slot, save 90).

#ifndef TESTS_RUN_COMMON_HERD_TALLY_H_
#define TESTS_RUN_COMMON_HERD_TALLY_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

#include "core_common/herd_state.h"
#include "core_common/ledger_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

/// @brief The young-cull column of a closed book, whichever name its tree
///        gives it (a template, so the requires-test is not a hard error).
template <typename Book>
const core::ResourceAmounts& YoungCulledColumn(const Book& book) {
  if constexpr (requires { book.herd_young_culled; }) {
    return book.herd_young_culled;
  } else {
    return book.herd_males_culled;
  }
}

class HerdTally {
 public:
  static constexpr std::uint32_t kYears = 6;
  static constexpr std::size_t kKinds = 5;
  static constexpr std::array<const char*, kKinds> kKindKeys = {
      "cow", "sheep", "pig", "horse", "goat"};

  explicit HerdTally(const core::ITableSet& tables) {
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      for (std::size_t index = 0; index < kKinds; ++index) {
        kind_rows_[index] = kinds->FindRowByKey(kKindKeys[index]);
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      milk_row_ = resources->FindRowByKey("milk");
    }
  }

  /// @brief The closed year's book, y1-y6.
  void CloseYear(const core::YearLedger& closed, std::uint32_t year) {
    if (year < 1 || year > kYears) {
      return;
    }
    ++years_closed_;
    births_ += closed.herd_births;
    deaths_age_ += closed.herd_deaths_age;
    deaths_hunger_ += closed.herd_deaths_hunger;
    for (const core::Grams heads : closed.herd_frozen) {
      frozen_ += heads;
    }
    for (std::size_t index = 0; index < kKinds; ++index) {
      young_culled_[index] += At(YoungCulledColumn(closed), kind_rows_[index]);
      autumn_[index] += At(closed.herd_autumn_slaughtered, kind_rows_[index]);
    }
    milk_g_ += At(closed.herd_produce, milk_row_);
    for (const core::Grams grams : closed.herd_hay_eaten) {
      hay_g_ += grams;
    }
  }

  /// @brief The adult heads at the turn of y3 and y6, kolkhoz and yards apart.
  void CountTurn(const core::WorldState& world, std::uint32_t year) {
    if (year != 3 && year != kYears) {
      return;
    }
    auto& heads = year == 3 ? heads_y3_ : heads_y6_;
    for (const core::HerdRow& herd : world.herds.rows) {
      for (std::size_t index = 0; index < kKinds; ++index) {
        if (herd.kind.value == kind_rows_[index]) {
          heads[index][herd.household_owned != 0 ? 1 : 0] += herd.adult_count;
        }
      }
    }
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: H1374 seed " << seed << " years " << years_closed_ << " births_y1y6 "
        << births_ << " deaths_age_y1y6 " << deaths_age_ << " deaths_hunger_y1y6 " << deaths_hunger_
        << " frozen_y1y6 " << frozen_ << " milk_t_y1y6 " << static_cast<double>(milk_g_) / 1e6
        << " hay_eaten_t_y1y6 " << static_cast<double>(hay_g_) / 1e6;
    for (std::size_t index = 0; index < kKinds; ++index) {
      const char* const key = kKindKeys[index];
      out << ' ' << key << "_young_culled " << young_culled_[index] << ' ' << key << "_autumn "
          << autumn_[index] << ' ' << key << "_kolkhoz_y3 " << heads_y3_[index][0] << ' ' << key
          << "_yards_y3 " << heads_y3_[index][1] << ' ' << key << "_kolkhoz_y6 "
          << heads_y6_[index][0] << ' ' << key << "_yards_y6 " << heads_y6_[index][1];
    }
    if (years_closed_ != kYears) {
      out << " — " << years_closed_ << " OF " << kYears << " YEARS CLOSED (NOT MEASURED)";
    }
    out << '\n';
  }

 private:
  static core::Grams At(const core::ResourceAmounts& column, std::uint32_t row) {
    return row < column.size() ? column[row] : 0;
  }

  std::array<std::uint32_t, kKinds> kind_rows_ = {core::kNoTableRow,
                                                  core::kNoTableRow,
                                                  core::kNoTableRow,
                                                  core::kNoTableRow,
                                                  core::kNoTableRow};
  std::uint32_t milk_row_ = core::kNoTableRow;
  std::uint32_t years_closed_ = 0;
  std::uint64_t births_ = 0;
  std::uint64_t deaths_age_ = 0;
  std::uint64_t deaths_hunger_ = 0;
  core::Grams frozen_ = 0;
  std::array<core::Grams, kKinds> young_culled_ = {};
  std::array<core::Grams, kKinds> autumn_ = {};
  core::Grams milk_g_ = 0;
  core::Grams hay_g_ = 0;
  std::array<std::array<std::uint32_t, 2>, kKinds> heads_y3_ = {};
  std::array<std::array<std::uint32_t, 2>, kKinds> heads_y6_ = {};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HERD_TALLY_H_
