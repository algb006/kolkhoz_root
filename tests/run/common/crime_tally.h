/// @file
/// @brief #1566's instrument: crime left unfought - which Epoch I ending's
/// trigger fires, and in what year; what drink costs. One line a village-year
/// (years 1..kYears), read at the year's close, and one line of first years.
/// @threading SINGLE_THREADED
/// Test-side code, driven once a day from the thread that owns the simulation.
///
/// econ's prints (econ-boss-1566-crime-unfought [1] §4), by the core's own doors:
///   * «Под суд», failed plans: PlanState::failed_years_in_a_row against the
///     campaign's plan_failed_years_to_trial (kPlanTrialDue's condition; the
///     trial itself is the presentation's);
///   * «Под суд», herd hunger: the closed year's herd_deaths_hunger against the
///     kolkhoz's heads at the year's close, the share and whether it reached
///     kHerdHungerTrialShare (epochs §8: 14 %);
///   * «Правление сгорело»: in Epoch I a fire is always put out (event_state.h,
///     kFireBroke), so the ending cannot fire: printed as such;
///   * «Село кончилось»: residents at the year's close - the core holds no
///     threshold for it, printed as such;
///   * drink: the adult men's alcoholism mean and the shares at >= 20/40/60,
///     the distillers (NightTrade::kDistiller) living, samogon brewed (ml),
///     sugar stolen (g);
///   * its price: worker-days of adult men at work, weighted by the SoberFactor
///     loss as alcoholism.h's scale reads (1.0 to 50, falling to 0.8 at 60) -
///     RECOMPUTED here from the scale, not the labor system's own number;
///   * the plan: the closed year's verdict.

#ifndef TESTS_RUN_COMMON_CRIME_TALLY_H_
#define TESTS_RUN_COMMON_CRIME_TALLY_H_

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/resident_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"

namespace run {

class CrimeTally {
 public:
  static constexpr std::uint32_t kYears = 16;
  /// epochs §8: a year whose herd hunger deaths reach this share of the herd.
  static constexpr double kHerdHungerTrialShare = 0.14;
  /// The adult man's biological age (life-cycle design): drink is counted on him.
  static constexpr float kAdultYears = 16.0F;
  /// #1569's candidate thresholds for «Село кончилось» (boss; the value is
  /// econ's to propose): the year-turn residents against each.
  static constexpr std::uint32_t kBelowLow = 40;
  static constexpr std::uint32_t kBelowHigh = 50;

  CrimeTally(const core::ITableSet& tables, std::uint64_t seed) : seed_(seed) {
    if (const core::ITable* const life = tables.FindTable("life")) {
      const std::uint32_t row = life->FindRowByKey("life_speedup");
      const std::uint32_t value = life->FindColumn("value");
      if (row != core::kNoTableRow && value != core::kNoTableColumn) {
        speedup_ = life->CellReal(row, value).value_or(speedup_);
      }
    }
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      sugar_ = resources->FindRowByKey("sugar");
    }
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      church_store_ = types->FindRowByKey("church_store");
      clamp_ = types->FindRowByKey("clamp");
    }
    // THE DISTILLER'S RAISER AS THE RUN READS IT (#1568 (d); boss [42]): the
    // tables' value, not alcoholism.h's no-tables default.
    if (const core::ITable* const params = tables.FindTable("world_params")) {
      const std::uint32_t row = params->FindRowByKey("alcohol_gain_with_distiller");
      const std::uint32_t value = params->FindColumn("value");
      if (row != core::kNoTableRow && value != core::kNoTableColumn) {
        distiller_raiser_ = params->CellReal(row, value).value_or(-1.0F);
      }
    }
    if (const core::ITable* const campaign = tables.FindTable("campaign")) {
      const std::uint32_t row = campaign->FindRowByKey("plan_failed_years_to_trial");
      const std::uint32_t value = campaign->FindColumn("value");
      if (row != core::kNoTableRow && value != core::kNoTableColumn) {
        trial_after_ = static_cast<std::uint32_t>(campaign->CellInteger(row, value).value_or(0));
      }
    }
  }

  /// @brief One closed day: the drink's price, and on the year's last day
  ///        the year's line.
  /// @brief Every step: #1568 link 2's split, night theft against collusion
  ///        (boss [39]), by kFoodStolen's `family` (1 = collusion). Checked at
  ///        the year's close against the ledger's `stolen` of the same kinds:
  ///        SPLIT_UNREAD when they part (the first form read the day's last
  ///        step only and missed every theft, 232 of 232 village-years).
  void CountStep(const core::WorldState& world) {
    for (const core::SimEvent& event : world.step_events) {
      if (event.kind == core::EventKind::kFoodStolen) {
        (event.family.value == 1 ? stolen_collusion_g_ : stolen_night_g_) += event.amount;
      }
    }
  }

  void CountDay(const core::WorldState& world) {
    const std::uint32_t year = (world.calendar.day / core::kDaysPerYear) + 1U;
    if (year > kYears) {
      return;
    }
    for (const core::ResidentRow& person : world.residents.rows) {
      if (person.sex != core::Sex::kMale || person.work.kind == core::WorkKind::kNone ||
          core::BiologicalAgeYears(speedup_, person.birth_day, world.calendar.day) < kAdultYears) {
        continue;
      }
      ++worked_;
      sober_loss_ += SoberLoss(person.alcoholism);
    }
    if (world.calendar.day % core::kDaysPerYear != core::kDaysPerYear - 1U) {
      return;
    }
    CloseYear(world, year);
  }

  /// @brief The village's lines: one a year read, then the first years.
  void Print(std::ostream& out, std::uint64_t seed) const {
    out << lines_.str();
    out << "population_curve: CRIME1566 seed " << seed << " first_trial_due_year "
        << (trial_year_ ? std::to_string(*trial_year_) : std::string("none"))
        << " first_herd_hunger_14pct_year "
        << (hunger_year_ ? std::to_string(*hunger_year_) : std::string("none"))
        << " (failed plans reach " << trial_after_ << ", read on the year's last day)"
        << " max_failed_in_a_row " << max_failed_
        << " board_burned: NOT MEASURABLE (Epoch I fires are always put out, event_state.h)"
        << " alcohol_gain_with_distiller "
        << (distiller_raiser_ < 0.0F ? std::string("NOT IN THE TABLES")
                                     : std::to_string(distiller_raiser_))
        << " first_audit_trial_due_year "
        << (audit_trial_year_ ? std::to_string(*audit_trial_year_) : std::string("none"))
        << " village_ended: NOT MEASURABLE (no trigger in the core, #1569)"
        << " min_year_turn_residents " << min_residents_ << " first_year_below_50 "
        << (below50_year_ ? std::to_string(*below50_year_) : std::string("none"))
        << " first_year_below_40 "
        << (below40_year_ ? std::to_string(*below40_year_) : std::string("none")) << '\n';
  }

 private:
  /// alcoholism.h's scale: output 1.0 to 50, falling to 0.8 at 60.
  /// The year's link 2 theft «до приёмки» (#1568 (a)), every kind.
  static core::Grams BeforeAct(const core::YearLedger& book) {
    core::Grams total = 0;
    for (const core::Grams grams : book.stolen_before_acceptance) {
      total += grams;
    }
    return total;
  }

  static double SoberLoss(float alcoholism) {
    constexpr float kFrom = 50.0F;
    constexpr float kTo = 60.0F;
    constexpr double kLossAtTo = 0.2;
    const float over = std::clamp((alcoholism - kFrom) / (kTo - kFrom), 0.0F, 1.0F);
    return static_cast<double>(over) * kLossAtTo;
  }

  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    std::uint32_t men = 0;
    double drink = 0.0;
    std::uint32_t over20 = 0;
    std::uint32_t over40 = 0;
    std::uint32_t over60 = 0;
    std::uint32_t distillers = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      distillers += person.night_trade == core::NightTrade::kDistiller ? 1U : 0U;
      if (person.sex != core::Sex::kMale ||
          core::BiologicalAgeYears(speedup_, person.birth_day, world.calendar.day) < kAdultYears) {
        continue;
      }
      ++men;
      drink += static_cast<double>(person.alcoholism);
      over20 += person.alcoholism >= 20.0F ? 1U : 0U;
      over40 += person.alcoholism >= 40.0F ? 1U : 0U;
      over60 += person.alcoholism >= 60.0F ? 1U : 0U;
    }
    std::uint64_t heads = 0;
    for (const core::HerdRow& herd : world.herds.rows) {
      if (herd.household_owned == 0) {
        heads += herd.adult_count + herd.juvenile_count + herd.newborn_count;
      }
    }
    // The year's book is the CURRENT one on its last day (it closes at the turn).
    const core::YearLedger& book = world.ledger.current;
    const std::uint32_t hunger = book.herd_deaths_hunger;
    const double hunger_share =
        heads + hunger == 0 ? 0.0
                            : static_cast<double>(hunger) / static_cast<double>(heads + hunger);
    if (hunger_share >= kHerdHungerTrialShare && !hunger_year_) {
      hunger_year_ = year;
    }
    max_failed_ = std::max<std::uint32_t>(max_failed_, world.plan.failed_years_in_a_row);
    // The trial's CONDITION as kPlanTrialDue raises it: the count reaches the
    // campaign's limit. The year is the one whose close the count is read at -
    // on its last day the closing year's verdict is not in yet, so a year's
    // failure shows on the next year's line (said in the print's key).
    if (trial_after_ > 0 && world.plan.failed_years_in_a_row >= trial_after_ && !trial_year_) {
      trial_year_ = year;
    }
    const core::Grams sugar =
        sugar_ != core::kNoTableRow && sugar_ < book.stolen.size() ? book.stolen[sugar_] : 0;
    const auto share = [men](std::uint32_t count) {
      return men == 0 ? 0.0 : static_cast<double>(count) / static_cast<double>(men);
    };
    // #1569's two candidate thresholds (boss: the year-turn population against
    // 40 and 50; the ending's producer is host's script, the core prints).
    const auto residents = static_cast<std::uint32_t>(world.residents.rows.size());
    min_residents_ = std::min(min_residents_, residents);
    if (residents < kBelowLow && !below40_year_) {
      below40_year_ = year;
    }
    if (residents < kBelowHigh && !below50_year_) {
      below50_year_ = year;
    }
    lines_ << "population_curve: CRIME1566Y seed " << seed_ << " year " << year << " residents "
           << world.residents.rows.size() << " men " << men << " drink_mean "
           << (men == 0 ? 0.0 : drink / men) << " share_ge20 " << share(over20) << " share_ge40 "
           << share(over40) << " share_ge60 " << share(over60) << " distillers " << distillers
           << " samogon_brewed_ml " << book.samogon_brewed_ml << " sugar_stolen_g " << sugar
           << " sober_loss_man_days " << sober_loss_ << " men_worked_days " << worked_
           << " failed_in_a_row " << static_cast<unsigned>(world.plan.failed_years_in_a_row)
           << " plan_verdict " << static_cast<unsigned>(world.plan.last_verdict)
           << " herd_hunger_deaths " << hunger << " herd_hunger_share " << hunger_share;
    // #1568's links (econ's spec §5 prints): truancy, theft by split, the book
    // against the real stock, the audits and the write-offs, the drunk fires.
    core::Grams food_stolen = 0;
    for (std::size_t resource = 0; resource < book.stolen.size(); ++resource) {
      food_stolen += resource == sugar_ ? 0 : book.stolen[resource];
    }
    core::Grams gap = 0;
    core::Grams real = 0;
    // THE GAP BY STORE (boss [41]): the church's store, the clamps, the rest.
    core::Grams gap_church = 0;
    core::Grams gap_clamp = 0;
    core::Grams gap_other = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      core::Grams here = 0;
      for (const core::Grams grams : unit.book_gap) {
        here += grams;
      }
      gap += here;
      if (unit.type.value == church_store_) {
        gap_church += here;
      } else if (unit.type.value == clamp_) {
        gap_clamp += here;
      } else {
        gap_other += here;
      }
      if (!unit.book_gap.empty()) {
        for (std::size_t resource = 0; resource < unit.book_gap.size(); ++resource) {
          real += resource < unit.stock.size() ? unit.stock[resource] : 0;
        }
      }
    }
    core::Grams written = 0;
    for (const core::Grams grams : book.written_off) {
      written += grams;
    }
    // LINK 2'S OWN GRAMS, read alone (boss [41]): the night theft and the
    // collusion, checked against the book's gap growth of the year (its
    // write-offs added back). The ledger's `stolen` of every kind but sugar
    // also carries the distillers' raw grain (night_trade's own theft), so it
    // is printed under its own name and never compared.
    const core::Grams split = stolen_night_g_ + stolen_collusion_g_;
    const core::Grams gap_grown = gap - last_gap_ + written;
    last_gap_ = gap;
    lines_ << " truant_man_days " << book.truant_man_days << " ledger_stolen_not_sugar_kg "
           << food_stolen / 1000 << " link2_by_night_kg " << stolen_night_g_ / 1000
           << " link2_by_collusion_kg " << stolen_collusion_g_ / 1000
           << (split == gap_grown ? "" : " LINK2_SPLIT_UNREAD") << " book_gap_kg " << gap / 1000
           << " gap_church_kg " << gap_church / 1000 << " gap_clamps_kg " << gap_clamp / 1000
           << " gap_other_stores_kg " << gap_other / 1000 << " real_in_gapped_stores_kg "
           << real / 1000 << " audits " << book.audits << " audit_remarks " << book.audit_remarks
           << " audit_shortage_permille " << world.audit.shortage_permille << " audit_trial_due "
           << static_cast<unsigned>(world.audit.trial_due) << " written_off_kg " << written / 1000
           << " drunk_fires " << book.drunk_fires << " books_accepted "
           << static_cast<unsigned>(world.audit.books_accepted) << " stolen_before_acceptance_kg "
           << BeforeAct(book) / 1000 << '\n';
    if (world.audit.trial_due != 0 && !audit_trial_year_) {
      audit_trial_year_ = year;
    }
    stolen_night_g_ = 0;
    stolen_collusion_g_ = 0;
    sober_loss_ = 0.0;
    worked_ = 0;
  }

  std::uint64_t seed_ = 0;
  float speedup_ = 4.0F;
  std::uint32_t sugar_ = core::kNoTableRow;
  std::uint32_t trial_after_ = 0;
  std::optional<std::uint32_t> trial_year_;
  std::optional<std::uint32_t> hunger_year_;
  std::optional<std::uint32_t> audit_trial_year_;  ///< #1568: kAuditTrialDue's year
  float distiller_raiser_ = -1.0F;
  std::uint32_t church_store_ = core::kNoTableRow;
  std::uint32_t clamp_ = core::kNoTableRow;
  core::Grams last_gap_ = 0;
  core::Grams stolen_night_g_ = 0;
  core::Grams stolen_collusion_g_ = 0;
  std::optional<std::uint32_t> below40_year_;
  std::optional<std::uint32_t> below50_year_;
  std::uint32_t min_residents_ = UINT32_MAX;
  std::uint32_t max_failed_ = 0;
  double sober_loss_ = 0.0;
  std::uint64_t worked_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_CRIME_TALLY_H_
