/// @file
/// @brief Where the logs went, year by year: felled, carted onto building
/// sites by the kind of site, sawn, spoiled, and what lies where at the turn.
/// @threading SINGLE_THREADED
/// Test-side instrument, read from the thread that owns the simulation.
///
/// WHY. The Epoch II diagnosis (boss, boss-core-epoch1-2 seq 25): every
/// upgrade the run's chairman ordered in nine villages was refused for want
/// of materials (240 of 240), the social objects stood as sites for want of
/// them, and the houses' shortfall is logs first. Whether the forest is cut
/// too little or the logs go elsewhere is a question the ledger cannot
/// answer — it books neither the felling nor what a site is given.
///
/// WHAT THE NUMBERS ARE, AND WHAT THEY ARE NOT:
///   * FELLED is the day-to-day RISE of the logs lying on the stands. A stand
///     felled and carted within one day shows only what was left at the day's
///     end, so this is a floor on the felling, not the felling.
///   * TO SITES is the day-to-day RISE of logs in the stock of a unit under
///     construction (delivering or building). A site given logs and finished
///     within one day shows nothing; a site's logs are counted once, when
///     they arrive, and never again. Classified by what the site is: a house,
///     one of the era's social objects, an upgrade (the target level above
///     one), or anything else the farm raises.
///   * SPOILED is the closing year's ledger column — the book's own figure.
///     SAWN IS NOT MEASURED: the sawmill takes its logs out of the stores and
///     books no ledger column (unit_production.cpp), so the logs it cut into
///     boards are in none of these columns, and the sheet says so rather than
///     print a nought that would read as "nothing was sawn".
///   * LYING is a snapshot at the year's turn: on the stands, in the stores
///     (every unit not under construction), and on sites.
/// The columns do not sum to the felling: carting losses, the start stock and
/// the same-day moves above stand between them.

#ifndef TESTS_RUN_COMMON_TIMBER_FLOW_TALLY_H_
#define TESTS_RUN_COMMON_TIMBER_FLOW_TALLY_H_

#include <cstdint>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/era_readiness.h"

namespace run {

class TimberFlowTally {
 public:
  /// One closed year of the logs, tonnes.
  struct Year {
    double felled = 0.0;
    double to_houses = 0.0;
    double to_social = 0.0;
    double to_upgrades = 0.0;
    double to_other = 0.0;
    double spoiled = 0.0;
    double lying_on_stands = 0.0;
    double in_stores = 0.0;
    double on_sites = 0.0;
  };

  explicit TimberFlowTally(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      const std::uint32_t row = resources->FindRowByKey("log");
      log_ = row == core::kNoTableRow ? kNoLog : row;
    }
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      const std::uint32_t row = types->FindRowByKey("wooden_house");
      house_ = row == core::kNoTableRow ? kNoType : row;
    }
    for (const core::UnitTypeId id :
         core::ReadReadinessCatalog(tables, core::Epoch::kOne).social_objects) {
      social_.push_back(id.value);
    }
  }

  /// @brief One day's reading. Call once a day, after the day's steps.
  void CountDay(const core::WorldState& world) {
    if (log_ == kNoLog) {
      return;
    }
    for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
      const std::uint32_t id = world.stands.row_ids[row].value;
      const core::Grams load = world.stands.rows[row].load_grams;
      const auto seen = stand_load_.find(id);
      const core::Grams before = seen == stand_load_.end() ? 0 : seen->second;
      if (load > before) {
        current_.felled += Tonnes(load - before);
      }
      stand_load_[id] = load;
    }
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const std::uint32_t id = world.units.row_ids[row].value;
      const core::Grams logs = log_ < unit.stock.size() ? unit.stock[log_] : 0;
      const bool site = unit.construction.phase == core::ConstructionPhase::kDelivering ||
                        unit.construction.phase == core::ConstructionPhase::kBuilding;
      const auto seen = site_logs_.find(id);
      const core::Grams before = seen == site_logs_.end() ? 0 : seen->second;
      if (site && logs > before) {
        const double arrived = Tonnes(logs - before);
        if (unit.type.value == house_) {
          current_.to_houses += arrived;
        } else if (IsSocial(unit.type.value)) {
          current_.to_social += arrived;
        } else if (unit.construction.target_level >= 2) {
          current_.to_upgrades += arrived;
        } else {
          current_.to_other += arrived;
        }
      }
      site_logs_[id] = site ? logs : 0;
    }
    // THE TURN: the ledger has rotated, `closed` is the year just ended.
    if (world.calendar.day != 0 && world.calendar.day % core::kDaysPerYear == 0 &&
        world.calendar.day != last_turn_day_) {
      last_turn_day_ = world.calendar.day;
      current_.spoiled = Tonnes(At(world.ledger.closed.spoiled));
      for (const core::TimberStandRow& stand : world.stands.rows) {
        current_.lying_on_stands += Tonnes(stand.load_grams);
      }
      for (const core::UnitRow& unit : world.units.rows) {
        const core::Grams logs = log_ < unit.stock.size() ? unit.stock[log_] : 0;
        const bool site = unit.construction.phase != core::ConstructionPhase::kNone;
        (site ? current_.on_sites : current_.in_stores) += Tonnes(logs);
      }
      years_.push_back(current_);
      current_ = Year{};
    }
  }

  const std::vector<Year>& years() const { return years_; }

  /// @brief Prints the year-by-year sheet, with what the numbers are not.
  static void PrintYears(std::string_view run_name, const std::vector<Year>& years) {
    std::cout << run_name
              << ": logs by year, tonnes — felled is a FLOOR (a stand felled and carted in a "
                 "day shows only the rest), to-sites counts arrivals only, spoiled is the "
                 "ledger's; SAWN IS NOT MEASURED (the sawmill books no column); the columns do "
                 "not sum to the felling\n"
              << "  year  felled  houses  social  upgrades  other  spoiled   stands  stores  "
                 "sites\n";
    for (std::size_t index = 0; index < years.size(); ++index) {
      const Year& y = years[index];
      std::cout << "  " << (index + 1) << "  " << y.felled << "  " << y.to_houses << "  "
                << y.to_social << "  " << y.to_upgrades << "  " << y.to_other << "  " << y.spoiled
                << "  " << y.lying_on_stands << "  " << y.in_stores << "  " << y.on_sites << '\n';
    }
    std::cout << "  " << years.size() << " years closed\n";
  }

 private:
  static constexpr std::uint32_t kNoLog = 0xFFFFFFFFU;
  static constexpr std::uint32_t kNoType = 0xFFFFFFFFU;

  static double Tonnes(core::Grams grams) { return static_cast<double>(grams) / 1.0e6; }

  core::Grams At(const core::ResourceAmounts& amounts) const {
    return log_ < amounts.size() ? amounts[log_] : 0;
  }

  bool IsSocial(std::uint32_t type) const {
    for (const std::uint32_t id : social_) {
      if (id == type) {
        return true;
      }
    }
    return false;
  }

  std::uint32_t log_ = kNoLog;
  std::uint32_t house_ = kNoType;
  std::vector<std::uint32_t> social_;
  std::unordered_map<std::uint32_t, core::Grams> stand_load_;
  std::unordered_map<std::uint32_t, core::Grams> site_logs_;
  std::int64_t last_turn_day_ = -1;
  Year current_;
  std::vector<Year> years_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_TIMBER_FLOW_TALLY_H_
