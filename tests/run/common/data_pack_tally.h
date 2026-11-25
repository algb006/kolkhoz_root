/// @file
/// @brief DATAPACK2157 (#2157's data pack for econ's review of store theft, grain at home and
///        the start store; boss 25 Nov): prints only, years 1-3, the world unchanged.
///        - THEFTN: each night with link 2's thefts (food_theft.cpp RunFoodTheft), per thief from
///          the core's own kFoodStolen events (TakeFood: one a kind, `resident` the thief, `unit`
///          the store, `family` 1 = the storekeeper's collusion) - the per-thief kg is exact; and
///          per night the thieves, the men 16+, the men who qualify (male, alcoholism at or over
///          `food_theft_from_alcoholism`, 16+ biological years - the core's kThiefFromYears) and
///          the ledger's `stolen` that step (it also books the distillers' raw, night_trade.cpp).
///        - FAMBAL: per family and month - the pantry's kcal at its start and end, the need (the
///          deltas of FamilyRow::season_need_kcal), the meal (the pantry's delta at hour 23) and
///          the plot (its delta at hour 22: world.cpp FoldPantryFlows' own split), the issue (the
///          village's `issued` of the step split by each family's trudodni_redeemed rise: the
///          core keeps no per-family issue), ration events, advance given; thief families marked.
///        - SAMO: per month the distillers, NightTheftTally::stolen_this_month, `samogon_paid`.
///        - ISSUE: per month the ledger's `issued` by resource, and grain a family a day.
///        - PLOT: per family and year the plot (the hour-22 deltas) beside the ledger's.
///        - WOOD: the family's firewood is NOT MEASURED (the core burns none: residents_system.cpp
///          «cold is not simulated in phase 1 (STUB)»); the village's firewood stock instead.
///        Test-side: the world is unchanged; not for release.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation: CountStep after every step,
/// CountDay once a day.

#ifndef TESTS_RUN_COMMON_DATA_PACK_TALLY_H_
#define TESTS_RUN_COMMON_DATA_PACK_TALLY_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core_catalog/world_conventions.h"
#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/family_state.h"
#include "core_common/ledger_state.h"
#include "core_common/night_trade_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class DataPackTally {
 public:
  explicit DataPackTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      const std::uint32_t kcal = resources->FindColumn("kcal_per_gram");
      for (std::uint32_t row = 0; row < resources->RowCount(); ++row) {
        names_.emplace_back(resources->CellText(row, 0));
        const float value =
            kcal == core::kNoTableColumn ? 0.0F : resources->CellReal(row, kcal).value_or(0.0F);
        kcal_per_gram_.push_back(static_cast<double>(value));
      }
      for (const std::string_view key : kGrainKeys) {
        const std::uint32_t row = resources->FindRowByKey(key);
        if (row != core::kNoTableRow) {
          grain_.push_back(row);
        }
      }
      firewood_ = resources->FindRowByKey("firewood");
    }
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      for (std::uint32_t row = 0; row < types->RowCount(); ++row) {
        type_names_.emplace_back(types->CellText(row, 0));
      }
    }
    if (const core::ITable* const world = tables.FindTable("world_params")) {
      const std::uint32_t column = world->FindColumn("value");
      const std::uint32_t row = world->FindRowByKey("food_theft_from_alcoholism");
      if (row != core::kNoTableRow && column != core::kNoTableColumn) {
        thief_from_alcoholism_ = world->CellReal(row, column).value_or(thief_from_alcoholism_);
      }
    }
    life_speedup_ = core::LifeSpeedupOr(tables, life_speedup_);
  }

  /// @brief After EVERY step: the night's thefts by their events, the pantries' hourly flows and
  ///        the ledger's deltas. A run that never calls it prints NOT MEASURED, not zeros.
  void CountStep(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    const std::uint32_t hour = core::HourFromTick(world.calendar.tick);
    const bool rotated = initialised_ && world.ledger.current.year != book_year_;
    const bool inside = YearOf(day) <= kYears;
    ++steps_;
    const std::uint32_t month = day / core::kDaysPerMonth;
    // THE LEDGER'S STEP: at the year's first tick the book rotates after the step's flows, so
    // that step's flows are in `closed` (world.cpp RotateLedger).
    const core::YearLedger& book = rotated ? world.ledger.closed : world.ledger.current;
    std::array<core::ResourceAmounts, kFlowCount> delta;
    for (std::size_t flow = 0; flow < kFlowCount; ++flow) {
      delta[flow] = Delta(Flow(book, flow), prev_flow_[flow]);
      prev_flow_[flow] = Flow(world.ledger.current, flow);
    }
    book_year_ = world.ledger.current.year;
    const bool counted = initialised_ && inside;
    initialised_ = true;
    if (counted) {
      Month& village = months_[month];
      for (std::size_t flow = 0; flow < kFlowCount; ++flow) {
        AddInto(village.flows[flow], delta[flow]);
      }
      const std::uint32_t own_index =
          (static_cast<std::uint32_t>(core::DateFromDay(day).year) * core::kMonthsPerYear) +
          (day / core::kDaysPerMonth) % core::kMonthsPerYear;
      if (world.night_theft.month_index == own_index) {
        village.night_theft_g = world.night_theft.stolen_this_month;
      }
      CountThefts(world, delta[kStolen], month);
      CountFamilies(world, hour, month, delta[kIssued], delta[kRation]);
    }
    SnapFamilies(world);
  }

  /// @brief Once a day: members, the qualifying men, the distillers, the families, the firewood.
  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    if (YearOf(day) > kYears) {
      return;
    }
    ++days_;
    const std::uint32_t month = day / core::kDaysPerMonth;
    Month& village = months_[month];
    village.distillers = 0;
    village.men16 = 0;
    village.qualifying = 0;
    std::map<std::uint32_t, std::uint32_t> members;
    for (const core::ResidentRow& person : world.residents.rows) {
      ++members[person.family.value];
      village.distillers += person.night_trade == core::NightTrade::kDistiller ? 1U : 0U;
      if (!IsMan16(world, person)) {
        continue;
      }
      ++village.men16;
      if (person.alcoholism >= thief_from_alcoholism_) {
        ++village.qualifying;
        families_[{person.family.value, month}].thief_family = true;
      }
    }
    for (const auto& [family, count] : members) {
      families_[{family, month}].members = count;
    }
    village.alcoholism = world.night_theft.settlement_alcoholism;
    village.family_days += static_cast<std::uint32_t>(world.families.rows.size());
    village.days += 1U;
    Year& year = years_[YearOf(day)];
    const core::Grams wood = StockOf(world, firewood_);
    if (!year.wood_seen) {
      year.wood_start = wood;
      year.wood_seen = true;
    }
    year.wood_end = wood;
  }

  void Print(std::ostream& out, std::uint64_t seed) const {
    const std::string head = "population_curve: ";
    if (steps_ == 0 || days_ == 0) {
      out << head << "DATAPACK2157 seed " << seed
          << " NOT_MEASURED ground=CountStep_or_CountDay_never_called steps " << steps_ << " days "
          << days_ << '\n';
      return;
    }
    PrintThefts(out, seed);
    PrintFamilies(out, seed);
    PrintMonths(out, seed);
    PrintPlots(out, seed);
    for (const auto& [year, row] : years_) {
      out << head << "WOOD seed " << seed << " year " << year
          << " ground=family_firewood_not_simulated(residents_system.cpp:cold_STUB,no_heating_burn)"
             ";"
             "village=sum_of_units_stock_firewood_kg_at_CountDay"
          << " family_firewood NOT_MEASURED village_firewood_kg_start " << Kg(row.wood_start)
          << " village_firewood_kg_end " << Kg(row.wood_end) << '\n';
    }
    out << head << "DATAPACK2157 seed " << seed << " ground=columns_the_core_keeps_no_record_of"
        << " NOT_MEASURED FAMBAL.ration_kcal_per_family(village_ration_only;kRationIssued_has_no_"
           "amount) FAMBAL.issue_kcal_split_exact_for_indebted(split_by_redeemed_trudodni;"
           "a_repaying_family's_handed_share<1_is_overstated) FAMBAL.samogon_paid_per_family"
           "(ledger_village_only) WOOD.family_firewood(no_heating_in_core) THEFTN.thief_pantry_"
           "gain(stolen_food_leaves_the_world:ledger_stolen) steps "
        << steps_ << " days " << days_ << '\n';
  }

 private:
  static constexpr std::uint32_t kYears = 3;
  static constexpr float kThiefFromYears = 16.0F;  // food_theft.cpp kThiefFromYears
  static constexpr std::array<std::string_view, 5> kGrainKeys = {
      "rye", "wheat", "barley", "oat", "buckwheat"};
  static constexpr double kGramsPerKg = 1000.0;

  enum FlowIndex : std::size_t {
    kIssued = 0,
    kRation,
    kAdvance,
    kStolen,
    kSamogon,
    kPlot,
    kFlowCount,
  };

  struct Thief {
    core::ResidentId resident;
    core::UnitId unit;
    std::string store_type;
    core::Grams grams = 0;
    bool collusion = false;
    float alcoholism = 0.0F;
    float age = 0.0F;
    std::uint32_t family = 0;
  };

  struct Night {
    core::SimDay day = 0;
    std::vector<Thief> thieves;
    core::Grams ledger_stolen = 0;
    std::uint32_t men16 = 0;
    std::uint32_t qualifying = 0;
  };

  struct Month {
    std::array<core::ResourceAmounts, kFlowCount> flows;
    core::Grams night_theft_g = 0;
    std::uint32_t distillers = 0;
    std::uint32_t men16 = 0;
    std::uint32_t qualifying = 0;
    float alcoholism = 0.0F;
    std::uint32_t family_days = 0;
    std::uint32_t days = 0;
  };

  struct FamilyMonth {
    bool seen = false;
    std::uint32_t members = 0;
    double start_kcal = 0.0;
    double end_kcal = 0.0;
    double need_kcal = 0.0;
    double eaten_kcal = 0.0;
    double plot_kcal = 0.0;
    double plot_g = 0.0;
    double issue_kcal = 0.0;
    double issue_g = 0.0;
    std::uint32_t issue_steps = 0;
    std::uint32_t repaying_steps = 0;
    std::uint32_t ration_events = 0;
    core::Grams advance_g = 0;
    bool thief_family = false;
    core::Grams thief_g = 0;
  };

  struct FamilySnap {
    core::ResourceAmounts pantry;
    float need = 0.0F;
    core::TrudodniHundredths redeemed = 0;
    core::Grams advance_debt = 0;
    core::Grams advance_given = 0;
  };

  struct Year {
    bool wood_seen = false;
    core::Grams wood_start = 0;
    core::Grams wood_end = 0;
  };

  static std::uint32_t YearOf(core::SimDay day) { return (day / core::kDaysPerYear) + 1U; }

  static double Kg(core::Grams grams) { return static_cast<double>(grams) / kGramsPerKg; }

  static core::Grams At(const core::ResourceAmounts& amounts, std::size_t at) {
    return at < amounts.size() ? amounts[at] : 0;
  }

  static const core::ResourceAmounts& Flow(const core::YearLedger& book, std::size_t flow) {
    switch (flow) {
      case kIssued:
        return book.issued;
      case kRation:
        return book.ration;
      case kAdvance:
        return book.advance;
      case kStolen:
        return book.stolen;
      case kSamogon:
        return book.samogon_paid;
      default:
        return book.plot_harvest;
    }
  }

  static core::ResourceAmounts Delta(const core::ResourceAmounts& now,
                                     const core::ResourceAmounts& was) {
    const std::size_t width = now.size() > was.size() ? now.size() : was.size();
    core::ResourceAmounts out(width, 0);
    for (std::size_t at = 0; at < width; ++at) {
      out[at] = At(now, at) - At(was, at);
    }
    return out;
  }

  static void AddInto(core::ResourceAmounts& into, const core::ResourceAmounts& add) {
    if (into.size() < add.size()) {
      into.resize(add.size(), 0);
    }
    for (std::size_t at = 0; at < add.size(); ++at) {
      into[at] += add[at];
    }
  }

  static core::Grams Sum(const core::ResourceAmounts& amounts) {
    core::Grams total = 0;
    for (const core::Grams grams : amounts) {
      total += grams;
    }
    return total;
  }

  static core::Grams StockOf(const core::WorldState& world, std::uint32_t resource) {
    core::Grams total = 0;
    if (resource == core::kNoTableRow) {
      return total;
    }
    for (const core::UnitRow& unit : world.units.rows) {
      total += At(unit.stock, resource);
    }
    return total;
  }

  double Kcal(const core::ResourceAmounts& grams) const {
    double kcal = 0.0;
    for (std::size_t at = 0; at < grams.size() && at < kcal_per_gram_.size(); ++at) {
      kcal += static_cast<double>(grams[at]) * kcal_per_gram_[at];
    }
    return kcal;
  }

  bool IsMan16(const core::WorldState& world, const core::ResidentRow& person) const {
    return person.sex == core::Sex::kMale &&
           core::BiologicalAgeYears(life_speedup_, person.birth_day, world.calendar.day) >=
               kThiefFromYears;
  }

  std::string ResourceName(std::size_t at) const {
    return at < names_.size() ? std::string(names_[at]) : "r" + std::to_string(at);
  }

  void CountThefts(const core::WorldState& world,
                   const core::ResourceAmounts& stolen,
                   std::uint32_t month) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, Thief> by_thief;
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind != core::EventKind::kFoodStolen) {
        continue;
      }
      Thief& thief = by_thief[{event.resident.value, event.unit.value}];
      thief.resident = event.resident;
      thief.unit = event.unit;
      thief.grams += event.amount;
      thief.collusion = event.family.value == 1U;
    }
    if (by_thief.empty()) {
      return;
    }
    Night night;
    night.day = world.calendar.day;
    night.ledger_stolen = Sum(stolen);
    for (const core::ResidentRow& person : world.residents.rows) {
      if (IsMan16(world, person)) {
        ++night.men16;
        night.qualifying += person.alcoholism >= thief_from_alcoholism_ ? 1U : 0U;
      }
    }
    for (auto& [key, thief] : by_thief) {
      const std::uint32_t row = core::FindRow(world.residents, thief.resident);
      if (row != core::kNoRow) {
        const core::ResidentRow& person = world.residents.rows[row];
        thief.alcoholism = person.alcoholism;
        thief.age = core::BiologicalAgeYears(life_speedup_, person.birth_day, world.calendar.day);
        thief.family = person.family.value;
        if (!thief.collusion) {
          families_[{thief.family, month}].thief_g += thief.grams;
        }
      }
      const std::uint32_t unit = core::FindRow(world.units, thief.unit);
      if (unit != core::kNoRow) {
        const std::uint16_t type = world.units.rows[unit].type.value;
        thief.store_type = type < type_names_.size() ? std::string(type_names_[type]) : "?";
      }
      night.thieves.push_back(thief);
    }
    nights_.push_back(std::move(night));
  }

  void CountFamilies(const core::WorldState& world,
                     std::uint32_t hour,
                     std::uint32_t month,
                     const core::ResourceAmounts& issued,
                     const core::ResourceAmounts& ration) {
    const bool plot_hour = hour == core::kTicksPerDay - 2U;
    const bool meal_hour = hour == core::kTicksPerDay - 1U;
    const double issued_kcal = Kcal(issued);
    const core::Grams issued_g = Sum(issued);
    // THE ISSUE'S SPLIT: the distribution covers every family at one village-wide share
    // (family_exchange.cpp RunDistribution), so its kcal follow the trudodni each redeemed.
    double redeemed_total = 0.0;
    if (issued_g > 0) {
      for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
        const auto was = prev_families_.find(world.families.row_ids[row].value);
        if (was != prev_families_.end()) {
          const core::TrudodniHundredths rise =
              world.families.rows[row].trudodni_redeemed - was->second.redeemed;
          redeemed_total += rise > 0 ? static_cast<double>(rise) : 0.0;
        }
      }
    }
    std::set<std::uint32_t> rationed;
    if (Sum(ration) > 0) {
      for (const core::SimEvent& event : world.step_events) {
        if (event.kind == core::EventKind::kRationIssued) {
          rationed.insert(event.family.value);
        }
      }
    }
    for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
      const core::FamilyRow& family = world.families.rows[row];
      const std::uint32_t id = world.families.row_ids[row].value;
      FamilyMonth& line = families_[{id, month}];
      const double kcal = Kcal(family.pantry);
      const auto was = prev_families_.find(id);
      if (!line.seen) {
        line.start_kcal = was != prev_families_.end() ? Kcal(was->second.pantry) : kcal;
        line.seen = true;
      }
      line.end_kcal = kcal;
      line.ration_events += rationed.contains(id) ? 1U : 0U;
      if (was == prev_families_.end()) {
        continue;  // formed this step: nothing to diff
      }
      const FamilySnap& snap = was->second;
      line.need_kcal += static_cast<double>(family.season_need_kcal >= snap.need
                                                ? family.season_need_kcal - snap.need
                                                : family.season_need_kcal);
      const core::ResourceAmounts pantry = Delta(family.pantry, snap.pantry);
      if (plot_hour) {
        line.plot_kcal += Kcal(pantry);
        line.plot_g += static_cast<double>(Sum(pantry));
      }
      if (meal_hour) {
        line.eaten_kcal -= Kcal(pantry);
      }
      const core::TrudodniHundredths rise = family.trudodni_redeemed - snap.redeemed;
      if (redeemed_total > 0.0 && rise > 0) {
        const double share = static_cast<double>(rise) / redeemed_total;
        line.issue_kcal += issued_kcal * share;
        line.issue_g += static_cast<double>(issued_g) * share;
        ++line.issue_steps;
        line.repaying_steps += family.advance_debt < snap.advance_debt ? 1U : 0U;
      }
      if (family.advance_given > snap.advance_given) {
        line.advance_g += family.advance_given - snap.advance_given;
      }
    }
  }

  void SnapFamilies(const core::WorldState& world) {
    prev_families_.clear();
    if (YearOf(world.calendar.day) > kYears) {
      return;
    }
    for (std::uint32_t row = 0; row < world.families.rows.size(); ++row) {
      const core::FamilyRow& family = world.families.rows[row];
      prev_families_[world.families.row_ids[row].value] =
          FamilySnap{.pantry = family.pantry,
                     .need = family.season_need_kcal,
                     .redeemed = family.trudodni_redeemed,
                     .advance_debt = family.advance_debt,
                     .advance_given = family.advance_given};
    }
  }

  void PrintThefts(std::ostream& out, std::uint64_t seed) const {
    for (const Night& night : nights_) {
      std::uint32_t thieves = 0;
      std::uint32_t keepers = 0;
      core::Grams events = 0;
      for (const Thief& thief : night.thieves) {
        (thief.collusion ? keepers : thieves) += 1U;
        events += thief.grams;
      }
      const std::uint32_t year = YearOf(night.day);
      const std::uint32_t day = (night.day % core::kDaysPerYear) + 1U;
      out << "population_curve: THEFTN seed " << seed << " kind night year " << year << " day "
          << day << " ground=kFoodStolen_events_of_the_step;ledger_stolen=step_delta(all_kinds,"
          << "incl_distillers_raw);qualifying=male,alcoholism>=" << thief_from_alcoholism_
          << ",age>=16(food_theft.cpp)" << " thieves " << thieves << " keepers " << keepers
          << " kg_events " << Kg(events) << " ledger_stolen_kg " << Kg(night.ledger_stolen)
          << " men16 " << night.men16 << " qualifying " << night.qualifying
          << " qualifying_took_nothing "
          << (night.qualifying > thieves ? night.qualifying - thieves : 0U) << '\n';
      for (const Thief& thief : night.thieves) {
        out << "population_curve: THEFTN seed " << seed << " kind thief year " << year << " day "
            << day << " ground=kFoodStolen_events;kg_exact_per_thief_and_store;"
            << "collusion=storekeeper;age=biological_years" << " resident " << thief.resident.value
            << " store " << thief.unit.value << " store_type " << thief.store_type << " kg "
            << Kg(thief.grams) << " collusion " << (thief.collusion ? 1 : 0) << " alcoholism "
            << thief.alcoholism << " age " << thief.age << " family " << thief.family
            << " night_thieves " << thieves << " men16 " << night.men16 << '\n';
      }
    }
  }

  void PrintFamilies(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [key, line] : families_) {
      if (!line.seen) {
        continue;
      }
      const std::uint32_t month = key.second;
      out << "population_curve: FAMBAL seed " << seed << " year "
          << (month / core::kMonthsPerYear) + 1U << " month " << (month % core::kMonthsPerYear) + 1U
          << " family " << key.first
          << " ground=kcal;pantry=step_snapshots;need=season_need_kcal_deltas;eaten=pantry_delta_"
             "hour23;plot=pantry_delta_hour22(FoldPantryFlows);issue=village_issued_split_by_"
             "redeemed_trudodni;thief_family=a_member_male16+_alcoholism>=threshold_on_a_day"
          << " members " << line.members << " pantry_kcal_start " << line.start_kcal
          << " pantry_kcal_end " << line.end_kcal << " need_kcal " << line.need_kcal
          << " eaten_kcal " << line.eaten_kcal << " plot_kcal " << line.plot_kcal
          << " issue_kcal_split " << line.issue_kcal << " issue_kg_split "
          << line.issue_g / kGramsPerKg << " issue_steps " << line.issue_steps << " repaying_steps "
          << line.repaying_steps << " ration_events " << line.ration_events
          << " ration_kcal NOT_MEASURED advance_given_kg_grain_eq " << Kg(line.advance_g)
          << " thief_family " << (line.thief_family ? 1 : 0) << " thief_kg " << Kg(line.thief_g)
          << '\n';
    }
  }

  void PrintMonths(std::ostream& out, std::uint64_t seed) const {
    for (const auto& [month, row] : months_) {
      const std::uint32_t year = (month / core::kMonthsPerYear) + 1U;
      const std::uint32_t of_year = (month % core::kMonthsPerYear) + 1U;
      out << "population_curve: SAMO seed " << seed << " year " << year << " month " << of_year
          << " ground=distillers=NightTrade::kDistiller_at_month's_last_CountDay;night_theft="
             "NightTheftTally::stolen_this_month_last_step;samogon_paid=ledger_step_deltas"
          << " distillers " << row.distillers << " night_theft_kg " << Kg(row.night_theft_g)
          << " samogon_paid_kg " << Kg(Sum(row.flows[kSamogon])) << " samogon_paid_kcal "
          << Kcal(row.flows[kSamogon]) << " men16 " << row.men16 << " qualifying " << row.qualifying
          << " settlement_alcoholism " << row.alcoholism << '\n';
      core::Grams grain = 0;
      for (const std::uint32_t at : grain_) {
        grain += At(row.flows[kIssued], at);
      }
      const double families =
          row.days > 0 ? static_cast<double>(row.family_days) / static_cast<double>(row.days) : 0.0;
      const double per_family_day =
          families > 0.0 ? Kg(grain) / families / static_cast<double>(core::kDaysPerMonth) : 0.0;
      out << "population_curve: ISSUE seed " << seed << " year " << year << " month " << of_year
          << " ground=ledger_issued_step_deltas_kg;grain=rye,wheat,barley,oat,buckwheat;"
             "per_family_day=grain/mean_families/"
          << core::kDaysPerMonth << "_days" << " families_mean " << families << " grain_issued_kg "
          << Kg(grain) << " grain_kg_per_family_day " << per_family_day << " issued_kg "
          << Kg(Sum(row.flows[kIssued])) << " issued_kcal " << Kcal(row.flows[kIssued])
          << " ration_kg " << Kg(Sum(row.flows[kRation])) << " advance_kg "
          << Kg(Sum(row.flows[kAdvance]));
      for (std::size_t at = 0; at < row.flows[kIssued].size(); ++at) {
        if (row.flows[kIssued][at] != 0) {
          out << " [" << ResourceName(at) << " " << Kg(row.flows[kIssued][at]) << "]";
        }
      }
      out << '\n';
    }
  }

  void PrintPlots(std::ostream& out, std::uint64_t seed) const {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<double, double>> by_year;
    for (const auto& [key, line] : families_) {
      if (!line.seen) {
        continue;
      }
      std::pair<double, double>& sum =
          by_year[{(key.second / core::kMonthsPerYear) + 1U, key.first}];
      sum.first += line.plot_kcal;
      sum.second += line.plot_g;
    }
    std::map<std::uint32_t, std::pair<double, double>> families_sum;
    for (const auto& [key, sum] : by_year) {
      out << "population_curve: PLOT seed " << seed << " kind family year " << key.first
          << " family " << key.second
          << " ground=pantry_delta_hour22(world.cpp_FoldPantryFlows:the_plot_is_the_only_"
             "writer_then);kg_all_resources_incl_hay"
          << " plot_kcal " << sum.first << " plot_kg " << sum.second / kGramsPerKg << '\n';
      families_sum[key.first].first += sum.first;
      families_sum[key.first].second += sum.second;
    }
    std::map<std::uint32_t, core::ResourceAmounts> ledger;
    for (const auto& [month, row] : months_) {
      AddInto(ledger[(month / core::kMonthsPerYear) + 1U], row.flows[kPlot]);
    }
    for (const auto& [year, plot] : ledger) {
      out << "population_curve: PLOT seed " << seed << " kind village year " << year
          << " ground=ledger_plot_harvest_step_deltas_vs_sum_of_families" << " ledger_kg "
          << Kg(Sum(plot)) << " ledger_kcal " << Kcal(plot) << " families_kg "
          << families_sum[year].second / kGramsPerKg << " families_kcal "
          << families_sum[year].first << '\n';
    }
  }

  std::vector<std::string> names_;
  std::vector<double> kcal_per_gram_;
  std::vector<std::uint32_t> grain_;
  std::uint32_t firewood_ = core::kNoTableRow;
  std::vector<std::string> type_names_;
  float thief_from_alcoholism_ = 40.0F;
  float life_speedup_ = 4.0F;
  bool initialised_ = false;
  std::uint16_t book_year_ = 0;
  std::uint64_t steps_ = 0;
  std::uint64_t days_ = 0;
  std::array<core::ResourceAmounts, kFlowCount> prev_flow_;
  std::map<std::uint32_t, FamilySnap> prev_families_;
  std::vector<Night> nights_;
  std::map<std::uint32_t, Month> months_;
  std::map<std::pair<std::uint32_t, std::uint32_t>, FamilyMonth> families_;
  std::map<std::uint32_t, Year> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_DATA_PACK_TALLY_H_
