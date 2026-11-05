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
#include "core_common/family_state.h"
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

/// @brief A herd row's inherited mark, 0 in a tree before #1374 has none (a
///        template, so the requires-test is not a hard error).
template <typename Row>
unsigned InheritedMarkOf(const Row& row) {
  if constexpr (requires { row.inherited; }) {
    return static_cast<unsigned>(row.inherited);
  } else {
    return 0U;
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
  static constexpr std::size_t kHorse = 3;
  static constexpr std::array<const char*, kKinds> kKindKeys = {
      "cow", "sheep", "pig", "horse", "goat"};
  /// The kinds the per-kind lines above leave out, printed by their births
  /// only (0.37.364's pair: births_y1y6 +10.2 a village, of which the five
  /// kinds above held +1.3 and the rest was in no line).
  static constexpr std::size_t kOtherKinds = 3;
  static constexpr std::array<const char*, kOtherKinds> kOtherKindKeys = {
      "chicken", "duck", "mink"};
  /// The decided yard ceilings in adult heads, econ's Y2 (boss [8]: the 1
  /// October start yards decide - a pig yard of two, three goats; the design
  /// text fixed in 4979b9652); sheep and horse stand in no yard.
  static constexpr std::array<std::uint32_t, kKinds> kDecidedCap = {1, 0xFFFF, 2, 0xFFFF, 3};
  /// The turns whose adult heads are printed.
  static constexpr std::array<std::uint32_t, 3> kTurns = {3, 5, 6};

  explicit HerdTally(const core::ITableSet& tables) {
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      cattle_yard_type_ = types->FindRowByKey("cattle_yard");
    }
    if (const core::ITable* const kinds = tables.FindTable("livestock")) {
      for (std::size_t index = 0; index < kKinds; ++index) {
        kind_rows_[index] = kinds->FindRowByKey(kKindKeys[index]);
      }
      for (std::size_t index = 0; index < kOtherKinds; ++index) {
        other_kind_rows_[index] = kinds->FindRowByKey(kOtherKindKeys[index]);
      }
      // The horse's per-head foal rate, for the foals the roof forgoes.
      const std::uint32_t rate = kinds->FindColumn("births_per_adult_head");
      if (rate != core::kNoTableColumn && kind_rows_[kHorse] != core::kNoTableRow) {
        horse_rate_ = kinds->CellReal(kind_rows_[kHorse], rate).value_or(0.0F);
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      milk_row_ = resources->FindRowByKey("milk");
      // The yard's food in kcal (econ's beside-gate, boss [14]): milk, meat
      // and eggs by resources.csv's own kcal_per_gram.
      const std::uint32_t kcal = resources->FindColumn("kcal_per_gram");
      // #1505's milk check: every resource with a kcal, for the yards' own food
      // against what the families ate (household-farming §1: the yards cover 59 %).
      for (std::uint32_t row = 0; kcal != core::kNoTableColumn && row < resources->RowCount();
           ++row) {
        const float per_gram = resources->CellReal(row, kcal).value_or(0.0F);
        if (per_gram > 0.0F) {
          kcal_rows_.emplace_back(row, per_gram);
        }
      }
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
    CountStalls(world, year);
    CountCowFlows(world, year);
    for (const core::SimEvent& event : world.step_events) {
      // The horses handed back to the district for points (econ via boss:
      // where the foals above the places go).
      if (event.kind == core::EventKind::kStockHandedOver) {
        const std::uint32_t handed = core::FindRow(world.herds, event.herd);
        if (handed != core::kNoRow && world.herds.rows[handed].kind.value == kind_rows_[kHorse]) {
          horse_handed_[year - 1] += event.amount;
        }
        continue;
      }
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
      for (std::size_t index = 0; index < kOtherKinds; ++index) {
        if (kind == other_kind_rows_[index]) {
          other_born_[index] += event.amount;
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
    if (world.calendar.day % core::kDaysPerYear == core::kDaysPerYear - 1U) {
      CloseCowYear(world, year);
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
    // THE FOALS THE ROOF FORGOES (econ via boss, after 0.93): a billeted
    // kolkhoz horse does not foal (herd_life.cpp, CalvingRoofShare), so in
    // the calving band (farming.csv birth_from..to_month, Mar-May) each
    // billeted head-day is the per-head rate over the band's days unborn.
    // AN ESTIMATE, by the rule's own arithmetic; the stable's foaling window
    // and the hunger gate are not in it.
    if (month >= 2 && month <= 4) {
      for (const core::HerdRow& herd : world.herds.rows) {
        if (herd.household_owned == 0 && herd.kind.value == kind_rows_[kHorse]) {
          horse_billeted_band_days_[year - 1] += herd.billeted_count;
        }
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
    // Y2'S PLACE (#1374's p78: seed 1945, a pig yard at 3 adults for 11
    // days, cap 2): the day a yard first holds more adults of a kind than the
    // decided cap, its rows of that kind with their yesterday.
    std::vector<std::uint32_t> over_now;
    for (const Yard& yard : yards) {
      for (std::size_t index = 0; index < kKinds; ++index) {
        if (yard.adults[index] <= kDecidedCap[index]) {
          continue;
        }
        const std::uint32_t key = (yard.family << 3U) | static_cast<std::uint32_t>(index);
        over_now.push_back(key);
        if (std::ranges::find(over_yesterday_, key) != over_yesterday_.end() ||
            over_log_.size() >= kMixedLogLines) {
          continue;
        }
        std::string line = "y" + std::to_string(year) + "d" +
                           std::to_string(world.calendar.day % core::kDaysPerYear) + " family " +
                           std::to_string(yard.family) + " kind " + kKindKeys[index] + " adults " +
                           std::to_string(yard.adults[index]);
        for (std::uint32_t row = 0; row < world.herds.rows.size(); ++row) {
          const core::HerdRow& herd = world.herds.rows[row];
          if (herd.household_owned == 0 || herd.household.value != yard.family ||
              herd.kind.value != kind_rows_[index]) {
            continue;
          }
          const std::uint32_t id = world.herds.row_ids[row].value;
          line += " [" + std::to_string(id) + ' ' + std::to_string(herd.adult_count) + '/' +
                  std::to_string(herd.juvenile_count) + '/' + std::to_string(herd.newborn_count) +
                  " inh " + std::to_string(InheritedMarkOf(herd));
          const auto was = std::ranges::find_if(
              yesterday_, [id](const HerdYesterday& entry) { return entry.id == id; });
          line += was == yesterday_.end()
                      ? std::string(" NEW")
                      : " was f" + std::to_string(was->family) + " h" + std::to_string(was->heads);
          line += ']';
        }
        over_log_.push_back(line);
      }
    }
    over_yesterday_ = std::move(over_now);
    // THE YARDS' FOOD VARIETY (econ's gate on Y3's trade-off, boss [25]): by
    // the yard's stock today - pig, goat, neither - the family-days whose
    // season's eaten categories (FamilyRow::food_variety_mask) count under
    // kVarietyNorm, and the seasons that END under it (the mask's popcount on
    // the day before it falls: the value the satiety ceiling reads).
    for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
      const std::uint32_t family = world.families.row_ids[row].value;
      std::size_t yard_class = kNeither;
      std::uint32_t stock_mask = 0;
      std::uint32_t goat_adults = 0;
      for (const Yard& yard : yards) {
        if (yard.family == family) {
          yard_class = yard.heads[kPig] > 0 ? kPigYard : (yard.heads[4] > 0 ? kGoatYard : kNeither);
          stock_mask = yard.kinds_mask;
          goat_adults = yard.adults[4];
        }
      }
      const std::uint16_t mask = world.families.rows[row].food_variety_mask;
      const int count = std::popcount(mask);
      ++family_days_[yard_class];
      family_days_below_[yard_class] += count < kVarietyNorm ? 1U : 0U;
      auto last = std::ranges::find_if(
          last_variety_, [family](const FamilyVariety& entry) { return entry.family == family; });
      if (last == last_variety_.end()) {
        last_variety_.push_back(FamilyVariety{.family = family,
                                              .mask = mask,
                                              .yard_class = yard_class,
                                              .stock_mask = stock_mask,
                                              .goat_adults = goat_adults});
        continue;
      }
      if (count < std::popcount(last->mask)) {  // the season turned: yesterday's was its end
        ++seasons_[last->yard_class];
        seasons_below_[last->yard_class] += std::popcount(last->mask) < kVarietyNorm ? 1U : 0U;
        // #1505: the goat yards' seasons by the goats they held (1, 2, 3+ adults).
        if (last->yard_class == kGoatYard) {
          const std::size_t bucket = std::min<std::uint32_t>(last->goat_adults, 3U);
          ++goat_seasons_by_heads_[bucket];
          goat_seasons_below_by_heads_[bucket] +=
              std::popcount(last->mask) < kVarietyNorm ? 1U : 0U;
        }
      }
      // #1505: who takes a stockless yard - the day a yard that kept no stock
      // keeps a kind, that kind, by the month (0-based).
      if (last->stock_mask == 0 && stock_mask != 0) {
        const auto month_now = static_cast<std::uint32_t>(world.calendar.date.month) % 12U;
        for (std::size_t index = 0; index < kKinds; ++index) {
          if ((stock_mask & (1U << index)) != 0) {
            ++yard_takers_[index][month_now];
          }
        }
      }
      last->mask = mask;
      last->yard_class = yard_class;
      last->stock_mask = stock_mask;
      last->goat_adults = goat_adults;
    }
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

  /// @brief #1503's print (a works that opens during the day waited for the
  ///        next morning beside idle hands): at the noon step (hour 13, the
  ///        state of step 12), y1-y3 - the works in kBuilding with labour left,
  ///        not paused, not stopped by winter, and nobody on it, on a day with
  ///        an adult idle and none at a day off (STALL site-days); the idle by
  ///        kNoHorse and the builders, head-days (the work mix); and every
  ///        works' days from entering its labour phase to leaving it.
  void CountStalls(const core::WorldState& world, std::uint32_t year) {
    const core::Tick hour = world.calendar.tick % core::kTicksPerDay;
    const std::uint32_t day = world.calendar.day;
    // Days to finish: by the step, every phase change is seen.
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const std::uint32_t id = world.units.row_ids[row].value;
      const bool building = unit.construction.phase == core::ConstructionPhase::kBuilding &&
                            unit.construction.labor_days_remaining > 0.0F;
      auto open = std::ranges::find_if(building_since_,
                                       [id](const auto& entry) { return entry.first == id; });
      if (building && open == building_since_.end()) {
        building_since_.emplace_back(id, day);
      } else if (!building && open != building_since_.end()) {
        if (year <= kStallYears) {
          finish_days_ += static_cast<double>(day - open->second);
          ++finished_works_;
        }
        building_since_.erase(open);
      }
    }
    if (hour != 12 || year > kStallYears) {
      return;
    }
    std::uint32_t idle = 0;
    bool day_off = false;
    std::array<std::uint32_t, kIdleReasons> idle_by_reason = {};
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.work.kind == core::WorkKind::kNone &&
          person.idle_reason != core::IdleReason::kIdleReasonCount) {
        ++idle;
        const auto reason = static_cast<std::size_t>(person.idle_reason);
        if (reason < kIdleReasons) {
          ++idle_by_reason[reason];
        }
        day_off = day_off || person.idle_reason == core::IdleReason::kDayOff;
        no_horse_idle_head_days_ += person.idle_reason == core::IdleReason::kNoHorse ? 1U : 0U;
      }
      builder_head_days_ += person.work.kind == core::WorkKind::kConstruction ? 1U : 0U;
    }
    if (idle == 0 || day_off) {
      return;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.construction.phase != core::ConstructionPhase::kBuilding ||
          !(unit.construction.labor_days_remaining > 0.0F) || unit.paused != 0 || unit.dead != 0 ||
          (world.calendar.season == core::Season::kWinter && unit.construction.winter_works == 0)) {
        continue;
      }
      const core::UnitId id = world.units.row_ids[row];
      const bool crewed = std::ranges::any_of(world.residents.rows, [id](const auto& person) {
        return person.work.kind == core::WorkKind::kConstruction &&
               person.work.unit.value == id.value;
      });
      stall_site_days_ += crewed ? 0U : 1U;
      // THE STALL'S IDLE BY REASON (boss [4]: «not placeable» as a count): the
      // idle adults beside each stalled site-day, by IdleReason.
      if (!crewed) {
        for (std::size_t reason = 0; reason < kIdleReasons; ++reason) {
          stall_idle_by_reason_[reason] += idle_by_reason[reason];
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
    // #1505's milk check: the yards' own food (their herds' produce and their
    // gardens) against everything the families ate, in kcal.
    for (const auto& [row, kcal_per_gram] : kcal_rows_) {
      const auto per_gram = static_cast<double>(kcal_per_gram);
      yard_own_kcal_ += (static_cast<double>(At(closed.yard_produce, row)) +
                         static_cast<double>(At(closed.plot_harvest, row))) *
                        per_gram;
      eaten_kcal_ += static_cast<double>(At(closed.eaten, row)) * per_gram;
      if (row == milk_row_) {
        yard_milk_kcal_ += static_cast<double>(At(closed.yard_produce, row)) * per_gram;
      }
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

  /// @brief The kolkhoz cows handed to the district so far in `year`
  ///        (1-based, up to kYears; 0 outside), for a day's difference.
  std::int64_t CowsHandedInYear(std::uint32_t year) const {
    return year >= 1 && year <= kYears ? cow_years_[year - 1].handed : 0;
  }

  /// @brief #1570's line: the kolkhoz cows' flows a year, y1-kYears -
  ///        «COW1570 seed S y1 adults_close A heads_close H born B young_culled
  ///        Y handed D surplus_slaughtered S autumn_slaughtered U died X froze F
  ///        yards Y yard_places P | y2 ...». yards / yard_places are the cattle
  ///        yards standing at level >= 1 and their head places at the close.
  void PrintCowFlows(std::ostream& out, std::uint64_t seed) const {
    out << "population_curve: COW1570 seed " << seed;
    for (std::uint32_t year = 0; year < kYears; ++year) {
      const CowYear& y = cow_years_[year];
      out << (year == 0 ? " y" : " | y") << year + 1 << " adults_close " << y.adults_close
          << " heads_close " << y.heads_close << " born " << y.born << " young_culled "
          << y.young_culled << " handed " << y.handed << " surplus_slaughtered "
          << y.surplus_slaughtered << " autumn_slaughtered " << y.autumn_slaughtered << " died "
          << y.died << " froze " << y.froze << " yards " << y.yards << " yard_places "
          << y.yard_places;
    }
    out << '\n';
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
    // #1503 (y1-y3, noon): the stalled works, the work mix, days to finish.
    out << " S_stall_site_days_y1y3 " << stall_site_days_ << " S_no_horse_idle_head_days_y1y3 "
        << no_horse_idle_head_days_ << " S_builder_head_days_y1y3 " << builder_head_days_
        << " S_works_finished_y1y3 " << finished_works_ << " S_days_to_finish_mean_y1y3 "
        << (finished_works_ > 0 ? finish_days_ / static_cast<double>(finished_works_) : 0.0);
    // The idle beside the stalled site-days, head-days by IdleReason (boss [4]).
    static constexpr std::array<const char*, 12> kReasonNames = {"no_open_work",
                                                                 "day_off",
                                                                 "work_covered",
                                                                 "no_horse",
                                                                 "crew_cap",
                                                                 "road",
                                                                 "horse_lock",
                                                                 "no_day_left",
                                                                 "resting",
                                                                 "unexplained",
                                                                 "rain",
                                                                 "funeral"};
    for (std::size_t reason = 0; reason < kReasonNames.size(); ++reason) {
      out << " S_stall_idle_" << kReasonNames[reason] << ' ' << stall_idle_by_reason_[reason];
    }
    // econ's variety gate: per yard class, family-days and seasons under 5.
    const std::array<const char*, 3> classes = {"pig", "goat", "none"};
    std::uint64_t all_days = 0;
    std::uint64_t all_below = 0;
    std::uint64_t all_seasons = 0;
    std::uint64_t all_seasons_below = 0;
    for (std::size_t c = 0; c < classes.size(); ++c) {
      out << " V_" << classes[c] << "_family_days " << family_days_[c] << " V_" << classes[c]
          << "_days_below5 " << family_days_below_[c] << " V_" << classes[c]
          << "_days_below5_share "
          << (family_days_[c] > 0 ? static_cast<double>(family_days_below_[c]) /
                                        static_cast<double>(family_days_[c])
                                  : 0.0)
          << " V_" << classes[c] << "_seasons " << seasons_[c] << " V_" << classes[c]
          << "_seasons_below5 " << seasons_below_[c] << " V_" << classes[c]
          << "_seasons_below5_share "
          << (seasons_[c] > 0
                  ? static_cast<double>(seasons_below_[c]) / static_cast<double>(seasons_[c])
                  : 0.0);
      all_days += family_days_[c];
      all_below += family_days_below_[c];
      all_seasons += seasons_[c];
      all_seasons_below += seasons_below_[c];
    }
    out << " V_all_days_below5_share "
        << (all_days > 0 ? static_cast<double>(all_below) / static_cast<double>(all_days) : 0.0)
        << " V_all_seasons_below5_share "
        << (all_seasons > 0
                ? static_cast<double>(all_seasons_below) / static_cast<double>(all_seasons)
                : 0.0);
    // #1505 (boss [3]): the goat yards' seasons ending under 5 by the goats
    // they held; who takes a stockless yard, by kind and month; and econ's milk
    // check - the yards' own food over what the families ate, y1-y6 (the
    // design's yard cover is 59 %, household-farming §1). GROUND of the cover:
    // kcal of yard_produce + plot_harvest over kcal of `eaten`; the plot's hay
    // has no kcal and is not in it.
    for (std::size_t bucket = 1; bucket < goat_seasons_by_heads_.size(); ++bucket) {
      const std::uint32_t seasons = goat_seasons_by_heads_[bucket];
      out << " G_goat" << bucket << (bucket == 3 ? "plus" : "") << "_seasons " << seasons
          << " G_goat" << bucket << (bucket == 3 ? "plus" : "") << "_below5_share "
          << (seasons > 0 ? static_cast<double>(goat_seasons_below_by_heads_[bucket]) /
                                static_cast<double>(seasons)
                          : 0.0);
    }
    for (const std::size_t index : {std::size_t{0}, kPig, std::size_t{4}}) {
      std::uint32_t taken = 0;
      for (const std::uint32_t count : yard_takers_[index]) {
        taken += count;
      }
      out << " T_" << kKindKeys[index] << "_took_stockless_y1y6 " << taken;
    }
    out << " T_by_month";
    for (std::uint32_t month = 0; month < 12; ++month) {
      out << " m" << month << ':' << yard_takers_[kPig][month] << '/' << yard_takers_[4][month];
    }
    out << " T_end M_yard_cover_share " << (eaten_kcal_ > 0.0 ? yard_own_kcal_ / eaten_kcal_ : 0.0)
        << " M_yard_milk_share " << (eaten_kcal_ > 0.0 ? yard_milk_kcal_ / eaten_kcal_ : 0.0);
    // Where the horse's foals above the places go, by year (econ via boss).
    constexpr double kBandDays = 3.0 * core::kDaysPerMonth;
    double forgone = 0.0;
    std::int64_t handed = 0;
    for (std::uint32_t year = 0; year < kYears; ++year) {
      const double unborn = static_cast<double>(horse_billeted_band_days_[year]) *
                            static_cast<double>(horse_rate_) / kBandDays;
      forgone += unborn;
      handed += horse_handed_[year];
      out << " HF_y" << year + 1 << "_born " << born_[kHorse][year] << " HF_y" << year + 1
          << "_handed " << horse_handed_[year] << " HF_y" << year + 1 << "_billeted_band_hd "
          << horse_billeted_band_days_[year] << " HF_y" << year + 1 << "_unborn_est " << unborn;
    }
    out << " HF_handed_y1y6 " << handed << " HF_unborn_est_y1y6 " << forgone;
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
    for (std::size_t index = 0; index < kOtherKinds; ++index) {
      out << ' ' << kOtherKindKeys[index] << "_born_y1y6 " << other_born_[index];
    }
    if (years_closed_ != kYears) {
      out << " — " << years_closed_ << " OF " << kYears << " YEARS CLOSED (NOT MEASURED)";
    }
    out << '\n';
    for (const std::string& line : mixed_log_) {
      out << "population_curve: Y1PLACE seed " << seed << ' ' << line << '\n';
    }
    for (const std::string& line : over_log_) {
      out << "population_curve: Y2PLACE seed " << seed << ' ' << line << '\n';
    }
  }

 private:
  static core::Grams At(const core::ResourceAmounts& column, std::uint32_t row) {
    return row < column.size() ? column[row] : 0;
  }

  /// #1570: a year of the kolkhoz cows' flows.
  struct CowYear {
    std::uint64_t adults_close = 0;
    std::uint64_t heads_close = 0;
    std::int64_t born = 0;
    std::int64_t young_culled = 0;
    std::int64_t handed = 0;
    std::int64_t surplus_slaughtered = 0;
    std::int64_t autumn_slaughtered = 0;
    std::int64_t died = 0;
    std::int64_t froze = 0;
    std::uint32_t yards = 0;
    std::uint32_t yard_places = 0;  ///< The standing cattle yards' levels, summed.
  };

  /// The step's events of a kolkhoz cow herd, by kind of flow.
  void CountCowFlows(const core::WorldState& world, std::uint32_t year) {
    const std::uint32_t cow = kind_rows_[0];
    if (cow == core::kNoTableRow) {
      return;
    }
    CowYear& y = cow_years_[year - 1];
    for (const core::SimEvent& event : world.step_events) {
      const std::uint32_t row = core::FindRow(world.herds, event.herd);
      if (row == core::kNoRow || world.herds.rows[row].household_owned != 0 ||
          world.herds.rows[row].kind.value != cow) {
        continue;
      }
      switch (event.kind) {
        case core::EventKind::kHerdBorn:
          y.born += event.amount;
          break;
        case core::EventKind::kHerdYoungCulled:
          y.young_culled += event.amount;
          break;
        case core::EventKind::kStockHandedOver:
          y.handed += event.amount;
          break;
        case core::EventKind::kHerdSurplusSlaughtered:
          y.surplus_slaughtered += event.amount;
          break;
        case core::EventKind::kHerdAutumnSlaughter:
          y.autumn_slaughtered += event.amount;
          break;
        case core::EventKind::kHerdDied:
          y.died += event.amount;
          break;
        case core::EventKind::kHerdFroze:
          y.froze += event.amount;
          break;
        default:
          break;
      }
    }
  }

  /// The year's close: the kolkhoz cows standing and the cattle yards.
  void CloseCowYear(const core::WorldState& world, std::uint32_t year) {
    const std::uint32_t cow = kind_rows_[0];
    CowYear& y = cow_years_[year - 1];
    y.adults_close = 0;
    y.heads_close = 0;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0 && herd.kind.value == cow) {
        y.adults_close += herd.adult_count;
        y.heads_close +=
            static_cast<std::uint64_t>(herd.adult_count) + herd.juvenile_count + herd.newborn_count;
      }
    }
    y.yards = 0;
    y.yard_places = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (cattle_yard_type_ != core::kNoTableRow && unit.type.value == cattle_yard_type_ &&
          unit.level >= 1 && unit.dead == 0) {
        ++y.yards;
        y.yard_places += unit.level;
      }
    }
  }

  std::array<CowYear, kYears> cow_years_{};
  std::uint32_t cattle_yard_type_ = core::kNoTableRow;

  std::array<std::uint32_t, kKinds> kind_rows_ = {core::kNoTableRow,
                                                  core::kNoTableRow,
                                                  core::kNoTableRow,
                                                  core::kNoTableRow,
                                                  core::kNoTableRow};
  std::array<std::uint32_t, kOtherKinds> other_kind_rows_ = {
      core::kNoTableRow, core::kNoTableRow, core::kNoTableRow};
  std::array<std::int64_t, kOtherKinds> other_born_ = {};
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
  std::vector<std::uint32_t> over_yesterday_;
  std::vector<std::string> over_log_;
  static constexpr std::uint32_t kStallYears = 3;  ///< #1503's print, y1-y3
  static constexpr std::size_t kIdleReasons = 16;  ///< above IdleReason's count; an index guard
  std::array<std::uint64_t, kIdleReasons> stall_idle_by_reason_ = {};
  std::vector<std::pair<std::uint32_t, std::uint32_t>> building_since_;  ///< unit id, day
  std::uint64_t stall_site_days_ = 0;
  std::uint64_t no_horse_idle_head_days_ = 0;
  std::uint64_t builder_head_days_ = 0;
  std::uint64_t finished_works_ = 0;
  double finish_days_ = 0.0;
  static constexpr std::size_t kPigYard = 0;
  static constexpr std::size_t kGoatYard = 1;
  static constexpr std::size_t kNeither = 2;
  static constexpr int kVarietyNorm = 5;  ///< econ: the Epoch I category norm

  struct FamilyVariety {
    std::uint32_t family = 0;
    std::uint16_t mask = 0;
    std::size_t yard_class = 2;
    std::uint32_t stock_mask = 0;   ///< the stock kinds (kKindKeys bits) it kept yesterday
    std::uint32_t goat_adults = 0;  ///< its goat adults yesterday
  };

  std::array<std::array<std::uint32_t, 12>, kKinds> yard_takers_ = {};  ///< #1505, by kind, month
  std::array<std::uint32_t, 4> goat_seasons_by_heads_ = {};             ///< #1505, 0..3+ adults
  std::array<std::uint32_t, 4> goat_seasons_below_by_heads_ = {};
  double yard_own_kcal_ = 0.0;  ///< milk check: the yards' own food (yard produce + garden)
  double yard_milk_kcal_ = 0.0;
  double eaten_kcal_ = 0.0;                                 ///< everything the families ate
  std::vector<std::pair<std::uint32_t, float>> kcal_rows_;  ///< every resource with kcal

  std::vector<FamilyVariety> last_variety_;
  std::array<std::uint64_t, 3> family_days_ = {};
  std::array<std::uint64_t, 3> family_days_below_ = {};
  std::array<std::uint64_t, 3> seasons_ = {};
  std::array<std::uint64_t, 3> seasons_below_ = {};
  float horse_rate_ = 0.0F;
  std::array<std::int64_t, kYears> horse_handed_ = {};
  std::array<std::uint64_t, kYears> horse_billeted_band_days_ = {};
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
