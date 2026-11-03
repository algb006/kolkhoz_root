/// @file
/// @brief The herds of a canon village, y1-y6 - #1374's pair print (herds
/// without sex; boss's gates: heads, births, young-cull meat, hay, milk and
/// the yards' pork within 2 SE).
/// @threading SINGLE_THREADED
/// Test-side code, read from the thread that owns the simulation.
///
/// WHAT IT COUNTS, from the closed books, the step's events and the herds at
/// the year turns: the book's births and deaths (age, hunger) and the frozen;
/// by kind (cow, sheep, pig, horse, goat) the births (kHerdBorn, every step,
/// by the herd's kind; the pig's by year - boss [5], so the autumn keep's
/// cure is seen), the young culled at maturing and the autumn slaughter, in
/// heads; the kolkhoz milk into the stores (herd_produce), t; the hay the
/// herds ate (herd_hay_eaten), t; the adult heads at the turns of y3, y5 and
/// y6, the kolkhoz's and the yards' apart (household_owned) - y3 and y5 are
/// the horse's gate (boss [5]: the invariant is the herd's growth, not the
/// births).
/// GROUND: the kolkhoz milk only - a yard cow's milk lands in its family's
/// pantry and is not in herd_produce; the book does not split the autumn
/// slaughter by owner. A birth whose herd row is gone by the step's end is
/// counted as «unknown» (printed).
/// Compiles on both trees of the pair: before #1374 the young-cull slot was
/// named herd_males_culled (the same slot, save 90).

#ifndef TESTS_RUN_COMMON_HERD_TALLY_H_
#define TESTS_RUN_COMMON_HERD_TALLY_H_

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/herd_state.h"
#include "core_common/labor_state.h"
#include "core_common/ledger_state.h"
#include "core_common/material_shortfall.h"
#include "core_common/quantities.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

/// @brief The young-cull column of a closed book, whichever name its tree
///        gives it (a template, so the requires-test is not a hard error).
/// @brief Whether a kind is one of a yard pig's knives: the autumn, the
///        surplus over the cap, and the young cull - kHerdYoungCulled in the
///        body's tree, kHerdMalesCulled before it (a template over the enum,
///        so the requires-test is not a hard error).
template <typename Kind>
bool IsPigKnife(Kind kind) {
  if constexpr (requires { Kind::kHerdYoungCulled; }) {
    return kind == Kind::kHerdAutumnSlaughter || kind == Kind::kHerdSurplusSlaughtered ||
           kind == Kind::kHerdYoungCulled;
  } else {
    return kind == Kind::kHerdAutumnSlaughter || kind == Kind::kHerdSurplusSlaughtered ||
           kind == Kind::kHerdMalesCulled;
  }
}

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
  static constexpr std::size_t kPig = 2;
  static constexpr std::array<const char*, kKinds> kKindKeys = {
      "cow", "sheep", "pig", "horse", "goat"};
  /// The decided yard ceilings in adult heads, econ's Y2 (boss [8]: the 1
  /// October start yards decide - a pig yard of two, three goats; the design
  /// text fixed in 4979b9652); sheep and horse stand in no yard.
  static constexpr std::array<std::uint32_t, kKinds> kDecidedCap = {1, 0xFFFF, 2, 0xFFFF, 3};
  /// The turns whose adult heads are printed.
  static constexpr std::array<std::uint32_t, 3> kTurns = {3, 5, 6};

  explicit HerdTally(const core::ITableSet& tables) {
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      for (std::size_t index = 0; index < kKinds; ++index) {
        kind_rows_[index] = kinds->FindRowByKey(kKindKeys[index]);
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      milk_row_ = resources->FindRowByKey("milk");
      // The yard's food in kcal (econ's beside-gate, boss [14]): milk, meat
      // and eggs by resources.csv's own kcal_per_gram.
      const std::uint32_t kcal = resources->FindColumn("kcal_per_gram");
      for (const char* key : {"milk", "meat", "egg"}) {
        const std::uint32_t row = resources->FindRowByKey(key);
        if (row != core::kNoTableRow && kcal != core::kNoTableColumn) {
          food_rows_.push_back({row, resources->CellReal(row, kcal).value_or(0.0F)});
        }
      }
    }
  }

  /// @brief The step's births, by the herd's kind and the game year.
  void CountStep(const core::WorldState& world) {
    const std::uint32_t year = world.calendar.date.year;  // one-based
    if (year < 1 || year > kYears) {
      return;
    }
    for (const core::SimEvent& event : world.step_events) {
      if (IsPigKnife(event.kind)) {
        // Y4 (econ): the yards' pigs that went to meat, heads, by year.
        const std::uint32_t knifed = core::FindRow(world.herds, event.herd);
        if (knifed != core::kNoRow && world.herds.rows[knifed].household_owned != 0 &&
            world.herds.rows[knifed].kind.value == kind_rows_[kPig]) {
          yard_pig_knifed_[year - 1] += event.amount;
        }
        continue;
      }
      if (event.kind != core::EventKind::kHerdBorn) {
        continue;
      }
      const std::uint32_t row = core::FindRow(world.herds, event.herd);
      if (row == core::kNoRow) {
        unknown_births_ += event.amount;
        continue;
      }
      const std::uint16_t kind = world.herds.rows[row].kind.value;
      for (std::size_t index = 0; index < kKinds; ++index) {
        if (kind == kind_rows_[index]) {
          born_[index][year - 1] += event.amount;
        }
      }
    }
  }

  /// @brief The teams' horse-days in the strada (Jun-Aug, as strada_tally.h):
  ///        the day's growth of the current book's horse_backed_assignment_days
  ///        (econ's horse gate via boss: the herd's size AND its work). Called
  ///        at every day's end; the book's reset at the year turn reads as a
  ///        fall and restarts the count.
  void CountDayEnd(const core::WorldState& world) {
    const float now = world.ledger.current.horse_backed_assignment_days;
    const float grown = now >= last_horse_days_ ? now - last_horse_days_ : now;
    last_horse_days_ = now;
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);  // 0-based
    const std::uint32_t year = world.calendar.date.year;
    if (year >= 1 && year <= kYears && month >= 5 && month <= 7) {
      strada_horse_days_[year - 1] += static_cast<double>(grown);
    }
    if (year < 1 || year > kYears) {
      return;
    }

    // Y1 and Y2 (econ, econ-boss-1374-yard-pig [1]): by yard, the `stock`
    // kinds it holds (any head) and its adults of cow, pig, goat.
    struct Yard {
      std::uint32_t family = 0;
      std::uint32_t kinds_mask = 0;
      std::array<std::uint32_t, kKinds> adults = {};
      std::array<std::uint32_t, kKinds> heads = {};
    };

    std::vector<Yard> yards;
    for (const core::HerdRow& herd : world.herds.rows) {
      const std::uint32_t total =
          static_cast<std::uint32_t>(herd.adult_count) + herd.juvenile_count + herd.newborn_count;
      if (herd.household_owned == 0 || total == 0) {
        continue;
      }
      std::size_t kind = kKinds;
      for (std::size_t index = 0; index < kKinds; ++index) {
        kind = herd.kind.value == kind_rows_[index] ? index : kind;
      }
      if (kind == kKinds) {
        continue;  // poultry and the rest: not `stock`
      }
      auto found = std::ranges::find_if(
          yards, [&](const Yard& yard) { return yard.family == herd.household.value; });
      if (found == yards.end()) {
        yards.push_back(Yard{.family = herd.household.value});
        found = yards.end() - 1;
      }
      found->kinds_mask |= 1U << kind;
      found->adults[kind] += herd.adult_count;
      found->heads[kind] += total;
    }
    std::uint32_t mixed_today = 0;
    for (const Yard& yard : yards) {
      mixed_today += std::popcount(yard.kinds_mask) >= 2 ? 1U : 0U;
      for (std::size_t index = 0; index < kKinds; ++index) {
        max_yard_adults_[index] = std::max(max_yard_adults_[index], yard.adults[index]);
        max_yard_heads_[index] = std::max(max_yard_heads_[index], yard.heads[index]);
        over_decided_cap_yard_days_[index] += yard.adults[index] > kDecidedCap[index] ? 1U : 0U;
      }
    }
    mixed_yard_days_ += mixed_today;
    mixed_yards_max_ = std::max(mixed_yards_max_, mixed_today);
    // Y1'S PLACE (#1374's p76: 37 mixed yard-days a village after the
    // inheritance cure): the day a yard first holds two stock kinds, each of
    // its herds with its row's yesterday - a new row, or the family and heads
    // it had - so the path that mixed it is named, not guessed.
    std::vector<std::uint32_t> mixed_now;
    for (const Yard& yard : yards) {
      if (std::popcount(yard.kinds_mask) < 2) {
        continue;
      }
      mixed_now.push_back(yard.family);
      if (std::ranges::find(mixed_yesterday_, yard.family) != mixed_yesterday_.end() ||
          mixed_log_.size() >= kMixedLogLines) {
        continue;
      }
      std::string line =
          "y" + std::to_string(year) + "d" +
          std::to_string(world.calendar.day % core::kDaysPerYear) + " family " +
          std::to_string(yard.family) +
          (core::FindRow(world.families, core::FamilyId{yard.family}) == core::kNoRow ? " GONE"
                                                                                      : "");
      for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
        const core::HerdRow& herd = world.herds.rows[row];
        if (herd.household_owned == 0 || herd.household.value != yard.family) {
          continue;
        }
        const std::uint32_t id = world.herds.row_ids[row].value;
        line += " [" + std::to_string(id) + " k" + std::to_string(herd.kind.value) + ' ' +
                std::to_string(herd.adult_count) + '/' + std::to_string(herd.juvenile_count) + '/' +
                std::to_string(herd.newborn_count);
        const auto was = std::ranges::find_if(
            yesterday_, [id](const HerdYesterday& entry) { return entry.id == id; });
        line += was == yesterday_.end()
                    ? std::string(" NEW")
                    : " was f" + std::to_string(was->family) + " h" + std::to_string(was->heads);
        line += ']';
      }
      mixed_log_.push_back(line);
    }
    mixed_yesterday_ = std::move(mixed_now);
    yesterday_.clear();
    for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
      const core::HerdRow& herd = world.herds.rows[row];
      yesterday_.push_back(HerdYesterday{.id = world.herds.row_ids[row].value,
                                         .family = herd.household.value,
                                         .heads = static_cast<std::uint32_t>(herd.adult_count) +
                                                  herd.juvenile_count + herd.newborn_count});
    }
    // kUnexplained («fit, free, wanted - and not placed: must stay nought»)
    // head-days by the day of the year, y1-y6 (boss, core-boss-1374 [12]:
    // pen 55's builders read it on y1 day 47, the year's last day).
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.idle_reason == core::IdleReason::kUnexplained) {
        ++unexplained_by_day_[world.calendar.day % core::kDaysPerYear];
      }
    }
  }

  /// @brief THE NEAR-SHORT RECIPES (boss, core-boss-1374 [16]: the warm hold's
  ///        margin rounded to 0 and seed 2035's warm read short by 19 g for
  ///        twelve days): every day, every unit a start could open (the two
  ///        cases MaterialsShortFor answers), each recipe line short by less
  ///        than a kilogram - counted in days by the unit's type and the line's
  ///        resource. A line held at «need − ε» for days names a hold or a
  ///        margin that cannot fire.
  void CountNearShort(const core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    const std::uint32_t year = world.calendar.date.year;
    if (year < 1 || year > kYears) {
      return;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const bool marked = unit.construction.phase == core::ConstructionPhase::kMarked;
      const bool standing = unit.construction.phase == core::ConstructionPhase::kNone &&
                            unit.level > 0 && unit.dead == 0;
      if (!marked && !standing) {
        continue;
      }
      for (const core::MaterialShortfall& line :
           simulation.MaterialsShortFor(world.units.row_ids[row])) {
        const core::Grams gap = line.needed - line.held;
        if (gap > 0 && gap < core::kGramsPerKilogram) {
          const std::uint32_t key = (static_cast<std::uint32_t>(unit.type.value) << 16U) |
                                    static_cast<std::uint32_t>(line.resource.value);
          auto found = std::ranges::find_if(near_short_,
                                            [&](const auto& entry) { return entry.first == key; });
          if (found == near_short_.end()) {
            near_short_.emplace_back(key, 0U);
            found = near_short_.end() - 1;
          }
          ++found->second;
        }
      }
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
      surplus_[index] += At(closed.herd_surplus_slaughtered, kind_rows_[index]);
    }
    milk_g_ += At(closed.herd_produce, milk_row_);
    yard_milk_g_ += At(closed.yard_produce, milk_row_);  // Y3
    for (const auto& [row, kcal_per_gram] : food_rows_) {
      yard_food_kcal_ +=
          static_cast<double>(At(closed.yard_produce, row)) * static_cast<double>(kcal_per_gram);
    }
    for (const core::Grams grams : closed.herd_hay_eaten) {
      hay_g_ += grams;
    }
  }

  /// @brief The adult heads at the turns of kTurns, kolkhoz and yards apart.
  void CountTurn(const core::WorldState& world, std::uint32_t year) {
    for (std::size_t turn = 0; turn < kTurns.size(); ++turn) {
      if (year != kTurns[turn]) {
        continue;
      }
      for (const core::HerdRow& herd : world.herds.rows) {
        for (std::size_t index = 0; index < kKinds; ++index) {
          if (herd.kind.value == kind_rows_[index]) {
            heads_[turn][index][herd.household_owned != 0 ? 1 : 0] += herd.adult_count;
            // The yards keeping the kind (any head), econ's goat carrier.
            const std::uint32_t total = static_cast<std::uint32_t>(herd.adult_count) +
                                        herd.juvenile_count + herd.newborn_count;
            yards_keeping_[turn][index] += herd.household_owned != 0 && total > 0 ? 1U : 0U;
          }
        }
      }
    }
  }

  /// @brief The village's line.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: H1374 seed " << seed << " years " << years_closed_ << " births_y1y6 "
        << births_ << " deaths_age_y1y6 " << deaths_age_ << " deaths_hunger_y1y6 " << deaths_hunger_
        << " frozen_y1y6 " << frozen_ << " milk_t_y1y6 " << static_cast<double>(milk_g_) / 1e6
        << " hay_eaten_t_y1y6 " << static_cast<double>(hay_g_) / 1e6 << " unknown_births "
        << unknown_births_;
    double strada_total = 0.0;
    for (std::uint32_t year = 0; year < kYears; ++year) {
      strada_total += strada_horse_days_[year];
      out << " strada_horse_days_y" << year + 1 << ' ' << strada_horse_days_[year];
    }
    out << " strada_horse_days_y1y6 " << strada_total;
    // econ's yard gates Y1-Y4 (econ-boss-1374-yard-pig [1]).
    std::uint64_t unexplained = 0;
    for (const std::uint64_t heads : unexplained_by_day_) {
      unexplained += heads;
    }
    out << " U_unexplained_head_days_y1y6 " << unexplained << " U_by_day";
    for (std::uint32_t day = 0; day < core::kDaysPerYear; ++day) {
      if (unexplained_by_day_[day] != 0) {
        out << ' ' << day << ':' << unexplained_by_day_[day];
      }
    }
    out << " U_end";
    std::uint64_t near_days = 0;
    for (const auto& entry : near_short_) {
      near_days += entry.second;
    }
    out << " EPS_near_short_line_days_y1y6 " << near_days << " EPS_list";
    for (const auto& [key, days] : near_short_) {
      out << ' ' << "type" << (key >> 16U) << "/res" << (key & 0xFFFFU) << ':' << days;
    }
    out << " EPS_end";
    out << " Y1_mixed_yard_days " << mixed_yard_days_ << " Y1_mixed_yards_max " << mixed_yards_max_
        << " Y3_yard_milk_t_y1y6 " << static_cast<double>(yard_milk_g_) / 1e6
        << " Y3b_yard_food_Mkcal_y1y6 " << yard_food_kcal_ / 1e6;
    for (const std::size_t index : {kPig, std::size_t{4}}) {
      const char* const key = kKindKeys[index];
      out << ' ' << key << "_surplus_knifed_y1y6 " << surplus_[index];
      for (std::size_t turn = 0; turn < kTurns.size(); ++turn) {
        out << ' ' << key << "_yards_keeping_y" << kTurns[turn] << ' '
            << yards_keeping_[turn][index];
      }
    }
    std::int64_t knifed_y1y3 = 0;
    for (std::uint32_t year = 0; year < 3; ++year) {
      knifed_y1y3 += yard_pig_knifed_[year];
    }
    out << " Y4_yard_pigs_knifed_y1y3 " << knifed_y1y3 << " Y4_yard_pork_by_y3 "
        << (knifed_y1y3 > 0 ? 1 : 0);
    for (const std::size_t index : {std::size_t{0}, kPig, std::size_t{4}}) {
      const char* const key = kKindKeys[index];
      out << " Y2_" << key << "_yard_adults_max " << max_yard_adults_[index] << " Y2_" << key
          << "_yard_heads_max " << max_yard_heads_[index] << " Y2_" << key
          << "_yard_days_over_decided " << over_decided_cap_yard_days_[index];
    }
    for (std::size_t index = 0; index < kKinds; ++index) {
      const char* const key = kKindKeys[index];
      std::int64_t born = 0;
      for (const std::int64_t heads : born_[index]) {
        born += heads;
      }
      out << ' ' << key << "_born_y1y6 " << born;
      if (index == kPig) {
        for (std::uint32_t year = 0; year < kYears; ++year) {
          out << ' ' << key << "_born_y" << year + 1 << ' ' << born_[index][year];
        }
      }
      out << ' ' << key << "_young_culled " << young_culled_[index] << ' ' << key << "_autumn "
          << autumn_[index];
      for (std::size_t turn = 0; turn < kTurns.size(); ++turn) {
        out << ' ' << key << "_kolkhoz_y" << kTurns[turn] << ' ' << heads_[turn][index][0] << ' '
            << key << "_yards_y" << kTurns[turn] << ' ' << heads_[turn][index][1];
      }
    }
    if (years_closed_ != kYears) {
      out << " — " << years_closed_ << " OF " << kYears << " YEARS CLOSED (NOT MEASURED)";
    }
    out << '\n';
    for (const std::string& line : mixed_log_) {
      out << "population_curve: Y1PLACE seed " << seed << ' ' << line << '\n';
    }
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
  std::int64_t unknown_births_ = 0;
  float last_horse_days_ = 0.0F;
  std::uint64_t mixed_yard_days_ = 0;
  std::uint32_t mixed_yards_max_ = 0;
  std::array<std::uint64_t, core::kDaysPerYear> unexplained_by_day_ = {};
  std::vector<std::pair<std::uint32_t, std::uint32_t>> near_short_;  ///< type<<16|res, days

  struct HerdYesterday {
    std::uint32_t id = 0;
    std::uint32_t family = 0;
    std::uint32_t heads = 0;
  };

  static constexpr std::size_t kMixedLogLines = 12;
  std::vector<HerdYesterday> yesterday_;
  std::vector<std::uint32_t> mixed_yesterday_;
  std::vector<std::string> mixed_log_;
  core::Grams yard_milk_g_ = 0;
  std::vector<std::pair<std::uint32_t, float>> food_rows_;
  double yard_food_kcal_ = 0.0;
  std::array<core::Grams, kKinds> surplus_ = {};
  std::array<std::array<std::uint32_t, kKinds>, kTurns.size()> yards_keeping_ = {};
  std::array<std::int64_t, kYears> yard_pig_knifed_ = {};
  std::array<std::uint32_t, kKinds> max_yard_adults_ = {};
  std::array<std::uint32_t, kKinds> max_yard_heads_ = {};
  std::array<std::uint64_t, kKinds> over_decided_cap_yard_days_ = {};
  std::array<double, kYears> strada_horse_days_ = {};
  core::Grams frozen_ = 0;
  std::array<std::array<std::int64_t, kYears>, kKinds> born_ = {};
  std::array<core::Grams, kKinds> young_culled_ = {};
  std::array<core::Grams, kKinds> autumn_ = {};
  core::Grams milk_g_ = 0;
  core::Grams hay_g_ = 0;
  std::array<std::array<std::array<std::uint32_t, 2>, kKinds>, kTurns.size()> heads_ = {};
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_HERD_TALLY_H_
