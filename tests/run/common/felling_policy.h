/// @file
/// @brief The run's chairman fells the nearest stand when the village has
/// fewer logs than one granary takes.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN FELLS. On the approved layout the store sites stood on logs
/// and nothing else: the 690 start logs were gone by year 7..14, and 47 of the
/// 55 store-class failed plan years of nine seeds had a granary site waiting
/// for logs (plan_shortfall, THE STORE SITES, 2026-09-13). The core had no
/// channel of logs at all; now it has felling (timber design §8a), and a run
/// has no chairman to mark a stand. So the run plays him, exactly as it plays
/// the building chairman (fixture_policy.h) and the yard (yard_policy.h).
///
/// ONE SIGNAL, ONE ANSWER (boss, parcel 181): fewer logs than one granary →
/// mark the nearest stand that still has timber for one granary's logs. It
/// is the answer a chairman gives to a site short of logs, and nothing
/// smarter: no planning a year ahead, no choosing a stand by its haul.
/// THE FIXTURE DIFFERS FROM THE START CANON, AND IT SAYS SO (Declare).

#ifndef TESTS_RUN_COMMON_FELLING_POLICY_H_
#define TESTS_RUN_COMMON_FELLING_POLICY_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/home_reach.h"
#include "core_common/order_state.h"
#include "core_common/road_route.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"
#include "rise_watch.h"
#include "village_middle.h"

namespace run {

class FellingPolicy {
 public:
  explicit FellingPolicy(const core::ITableSet& tables) {
    std::string error;
    ready_ = core::ParseTimberCatalog(tables, catalog_, error) &&
             catalog_.log_resource.value != core::kInvalidDefIdValue && catalog_.log_grams > 0 &&
             !catalog_.stands.empty();
    granary_logs_ = GranaryLogs(tables);
    logs_by_type_ = FirstLevelLogs(tables);
    ReadFuel(tables);
    if (const core::ITable* const types = tables.FindTable("unit_types")) {
      house_type_ = types->FindRowByKey("wooden_house");
    }
    ReadReachRule(tables);
  }

  /// @brief The fixture difference, in words, for the run to print BEFORE it
  /// measures anything.
  static void Declare(const char* run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman FELLS the nearest "
                 "stand when the village has fewer logs than one granary, the waiting sites and "
                 "the next three houses take (timber design §8a; boss, 2026-09-13; ahead of the "
                 "queue since 10 October 2026)\n";
  }

  /// @brief Counts the step the chairman's yard waits to take (rise_watch.h).
  void SetRiseWatch(RiseWatch watch) { rise_watch_ = std::move(watch); }

  /// @brief THE FELLING AHEAD OF THE QUEUE (econ's ruling passed by boss,
  /// core-boss-c2-site-supply-2026-10-09 after [87]; ExtractionPolicy::
  /// KeepAhead is the same rule for the pit): the logs of so many NEXT houses
  /// are kept in hand beside the granary's worth, the open sites' and the
  /// saw's. On 0.37.212, nine villages, years 1-5, a marked house stood short
  /// of logs on 439 site-days; the felling began when the site already waited.
  /// @param houses How many next houses' logs to keep; 0 — as before.
  void KeepAhead(std::uint32_t houses) { houses_ahead_ = houses; }

  /// @brief THE WINTER STOCK (econ's lever for B3, boss 24 Nov; the 2 Oct rule: the bot plays
  ///        well): from November to February the logs kept in hand cover the whole next
  ///        building season - last year's logs built into walls and taken in by the shops (the
  ///        closed book's built_in and processed) - so the mud weeks need no felling and no log
  ///        carting. Off: the felling follows today's sites, as before.
  void StockForTheSeason(bool on) { season_stock_ = on; }

  /// @brief THE SEASON'S STAND ORDER (econ's #2072 lever, boss 24 Nov; econ/manual/proposals/
  ///        winter-near-band-2072-2026-11-24.md): in March-October the mark goes to the
  ///        FARTHEST stand a feller can be sent to today, keeping the near band - the only one
  ///        a winter day's light reaches on foot - for November-February, when the nearest is
  ///        marked as before. Off: the nearest all year.
  void SeasonStandOrder(bool on) { season_order_ = on; }

  /// @brief One day of the chairman's attention. Call once a day.
  /// @param saw_log_grams The logs the saw needs for the boards the sites still
  ///        lack (SawmillPolicy::LogsForMissingBoards); 0 in a run with no saw.
  void RunDay(core::ISimulation& simulation, core::Grams saw_log_grams = 0) {
    if (!ready_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    WatchTheSeason(world);  // econ's P and M2 (#2072 season order), before any early return
    // ONE STAND AT A TIME — WHILE ITS FELLING MOVES (boss, the resume thread,
    // 9 October 2026). A mark nobody has felled at for kStalledDays days is no
    // reason to fell nothing: the next stand is marked. Until 0.37.208 any
    // mark held every other felling, and one mark no hand could be sent to
    // (the last 0.085 man-day of stand 37, village 1936: 312 days) stopped
    // the village's logs, its houses and its residents from year ten on.
    // Counted every day, before the cooldown, so the count is of days.
    const bool held = AMovingMarkStands(world);
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    // THE STOCK'S OWN PATH, COUNTED (boss 24 Nov, the null pair): a stock winter day ends held
    // by a moving mark, or (where the season raised what is wanted) met by the logs in hand -
    // the lying logs count - or marked.
    const bool stock_winter = season_stock_ && InStockMonths(world);
    if (held) {
      stock_held_days_ += stock_winter ? 1U : 0U;
      return;
    }
    stock_free_days_ += stock_winter ? 1U : 0U;
    // #2261 FUEL FELLING FIRST, IN ITS SEASON (econ, STUB): September to February, while the
    // village's firewood (stores + buffers + the fuel logs not yet split) is under the coming
    // winter's burn - last winter's at this house count - the crew fells for fuel.
    if (FuelMark(simulation, world)) {
      return;
    }
    const core::Grams logs = LogsInHand(world);
    // AND THE LOGS THE SITES STILL WAIT FOR (2026-09-14). One granary's worth
    // was the whole signal while granaries were the only sites; since the
    // houses came off the stub, three house sites at 35 logs each waited on a
    // village holding 35, and felling never began because 35 was "enough" by
    // a granary's measure — nine houses in twelve years on seed 1929.
    core::Grams owed = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      // Marked and waiting for the recipe too: a start needs the whole recipe
      // in the village now (construction design §6), so a site waits marked.
      const bool waits = unit.construction.phase == core::ConstructionPhase::kDelivering ||
                         unit.construction.phase == core::ConstructionPhase::kMarked;
      if (unit.level != 0 || !waits || unit.type.value >= logs_by_type_.size()) {
        continue;
      }
      const core::Grams needed =
          static_cast<core::Grams>(logs_by_type_[unit.type.value]) * catalog_.log_grams;
      const core::Grams there = catalog_.log_resource.value < unit.stock.size()
                                    ? unit.stock[catalog_.log_resource.value]
                                    : 0;
      owed += needed > there ? needed - there : 0;
    }
    // And the logs of the step the chairman's yard waits to take (rise_watch.h):
    // the recipe of a level above the first is not in logs_by_type_, so it is
    // read off the core's own shortfall of that step.
    const std::uint32_t rising = rise_watch_ ? rise_watch_(world) : core::kNoRow;
    if (rising < world.units.rows.size()) {
      for (const core::MaterialShortfall& line :
           simulation.MaterialsShortFor(world.units.row_ids[rising])) {
        if (line.resource.value == catalog_.log_resource.value) {
          owed += line.needed;
        }
      }
    }
    // And the next houses' logs, kept ahead (KeepAhead).
    const core::Grams ahead = house_type_ < logs_by_type_.size()
                                  ? static_cast<core::Grams>(logs_by_type_[house_type_]) *
                                        static_cast<core::Grams>(houses_ahead_) * catalog_.log_grams
                                  : 0;
    core::Grams wanted = (static_cast<core::Grams>(granary_logs_) * catalog_.log_grams) + owed +
                         saw_log_grams + ahead;
    // The winter stock: November to February, a whole season's logs in hand (StockForTheSeason).
    bool lifted = false;
    if (stock_winter) {
      const core::ResourceId log = catalog_.log_resource;
      const auto at = [&log](const core::ResourceAmounts& amounts) {
        return log.value < amounts.size() ? amounts[log.value] : core::Grams{0};
      };
      const core::Grams season =
          at(world.ledger.closed.built_in) + at(world.ledger.closed.processed);
      season_target_max_ = std::max(season_target_max_, season);
      season_want_ = std::max(season_want_, season);
      lifted = season > wanted;
      season_aimed_ = season_aimed_ || lifted;
      wanted = std::max(wanted, season);
    }
    stock_lifted_days_ += lifted ? 1U : 0U;
    if (logs >= wanted) {
      stock_met_days_ += lifted ? 1U : 0U;
      return;
    }
    // ONE FELLING, SIZED TO THE WHOLE SHORTFALL — the sites' logs and the
    // saw's together (boss, parcel 281). "Одна активная задача на тип работ"
    // counts markings, not volume: a good chairman marks a grove for the
    // year's need at once and keeps the crew the tools and the people allow,
    // rather than felling sixty-five times by a granary's worth.
    const auto short_logs =
        static_cast<std::uint32_t>((wanted - logs + catalog_.log_grams - 1) / catalog_.log_grams);
    core::OrderRow order;
    if (!NearestMark(simulation, short_logs > granary_logs_ ? short_logs : granary_logs_, order)) {
      stock_no_stand_days_ += lifted ? 1U : 0U;  // no stand a feller can be sent to today
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ++marked_;
    stock_marked_ += lifted ? 1U : 0U;
    cooldown_ = kCooldownDays;
  }

  /// @brief What the policy did, for the run to print at the end.
  void Report(const char* run, const core::WorldState& world) const {
    float felled_left = 0.0F;
    float started = 0.0F;
    for (const core::TimberStandRow& stand : world.stands.rows) {
      if (stand.kind == core::TimberStandKind::kForestOld ||
          stand.table_row >= catalog_.stands.size()) {
        continue;
      }
      felled_left += stand.stock_m3;
      started += core::StartStockM3(catalog_, catalog_.stands[stand.table_row]);
    }
    std::cout << run << ": the run's chairman marked " << marked_
              << " fellings; groves and belts hold " << std::lround(felled_left) << " of "
              << std::lround(started) << " m3";
    if (season_stock_) {
      // The winter stock's own ground: the largest season it aimed at (0 says it never aimed).
      std::cout << "; winter stock ON, largest season target "
                << season_target_max_ / catalog_.log_grams << " logs"
                << (season_target_max_ == 0 ? " WINTER_STOCK_NEVER_AIMED" : "");
      // Its path (boss 24 Nov): the stock winter days held by a moving mark, free of one; of
      // the free, the days the season raised the want, met by the logs in hand, and marked.
      std::cout << "; stock days held_by_moving_mark " << stock_held_days_ << " free "
                << stock_free_days_ << " lifted " << stock_lifted_days_ << " met_in_hand "
                << stock_met_days_ << " no_mannable_stand " << stock_no_stand_days_ << " marked "
                << stock_marked_
                << (stock_lifted_days_ > 0 && stock_marked_ == 0 ? " STOCK_NEVER_MARKED" : "")
                << (stock_held_days_ + stock_free_days_ == 0 ? " STOCK_DAYS_NOT_COUNTED" : "");
      // econ's M2: the stock seasons aimed, and met by 1 March.
      std::cout << "; seasons aimed " << seasons_aimed_ << " met_by_1_march " << seasons_met_;
    }
    std::cout << "; season stand order " << (season_order_ ? "ON" : "off");
    // econ's P: the logs a hand reaches on foot in the first November day's light, by year.
    std::cout << "; P winter_foot_logs(year:logs@daylight_h)";
    if (!rule_read_) {
      std::cout << " NOT MEASURED (reach rule tables not read)";
    }
    for (const WinterFoot& foot : winter_foot_) {
      std::cout << ' ' << foot.year << ':' << std::lround(foot.logs) << '@' << foot.daylight;
    }
    std::cout << '\n';
  }

 private:
  static constexpr std::uint32_t kCooldownDays = 2;

  /// The logs a first-level granary takes (unit_level_cost.csv), read so the
  /// signal moves with the table.
  static std::uint32_t GranaryLogs(const core::ITableSet& tables) {
    constexpr std::uint32_t kFallback = 60;
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    if (costs == nullptr) {
      return kFallback;
    }
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      if (costs->CellText(row, unit_col) == "granary" && costs->CellText(row, level_col) == "1" &&
          costs->CellText(row, resource_col) == "log") {
        const std::optional<float> amount = costs->CellReal(row, amount_col);
        return amount.has_value() && *amount > 0.0F ? static_cast<std::uint32_t>(*amount)
                                                    : kFallback;
      }
    }
    return kFallback;
  }

  /// The logs the first level of every unit type takes, by unit_types row
  /// (unit_level_cost.csv); zero for a type that takes none.
  static std::vector<std::uint32_t> FirstLevelLogs(const core::ITableSet& tables) {
    std::vector<std::uint32_t> logs;
    const core::ITable* const costs = tables.FindTable("unit_level_cost");
    const core::ITable* const types = tables.FindTable("unit_types");
    if (costs == nullptr || types == nullptr) {
      return logs;
    }
    logs.assign(types->RowCount(), 0U);
    const std::uint32_t unit_col = costs->FindColumn("unit");
    const std::uint32_t level_col = costs->FindColumn("level");
    const std::uint32_t resource_col = costs->FindColumn("resource");
    const std::uint32_t amount_col = costs->FindColumn("amount");
    for (std::uint32_t row = 0; row < costs->RowCount(); ++row) {
      if (costs->CellText(row, level_col) != "1" || costs->CellText(row, resource_col) != "log") {
        continue;
      }
      const std::uint32_t type_row = types->FindRowByKey(costs->CellText(row, unit_col));
      const std::optional<float> amount = costs->CellReal(row, amount_col);
      if (type_row < logs.size() && amount.has_value() && *amount > 0.0F) {
        logs[type_row] = static_cast<std::uint32_t>(*amount);
      }
    }
    return logs;
  }

  /// The village's centre, as the building chairman reckons it
  /// (village_middle.h; the mean of every unit until 0.37.111).
  static core::Vec2 Centre(const core::WorldState& world) { return VillageMiddle(world); }

  /// Whether some stand is marked and its felling has moved within the last
  /// kStalledDays days — the mark that holds the next one. Call once a day:
  /// it counts, for every marked stand, the days its work has not moved.
  bool AMovingMarkStands(const core::WorldState& world) {
    const std::size_t stands = world.stands.rows.size();
    seen_work_.resize(stands, 0.0F);
    still_days_.resize(stands, 0U);
    bool moving = false;
    for (std::size_t row = 0; row < stands; ++row) {
      const core::TimberStandRow& stand = world.stands.rows[row];
      if (!(stand.marked_m3 > 0.0F)) {
        seen_work_[row] = 0.0F;
        still_days_[row] = 0U;
        continue;
      }
      still_days_[row] = stand.work_days_remaining == seen_work_[row] ? still_days_[row] + 1U : 0U;
      seen_work_[row] = stand.work_days_remaining;
      moving = moving || still_days_[row] < kStalledDays;
    }
    return moving;
  }

  /// #2261: the firewood resource, its grams a m³, and the splitting's m³ a log
  /// (production_io.csv firewood_splitting: 1 log in, 0.33 m³ out).
  void ReadFuel(const core::ITableSet& tables) {
    if (const core::ITable* const resources = tables.FindTable("resources")) {
      const std::uint32_t row = resources->FindRowByKey("firewood");
      const std::uint32_t kg = resources->FindColumn("kg_per_unit");
      if (row != core::kNoTableRow && kg != core::kNoTableColumn) {
        firewood_ = core::ResourceId{static_cast<std::uint16_t>(row)};
        firewood_grams_per_m3_ =
            static_cast<double>(resources->CellReal(row, kg).value_or(0.0F)) * 1000.0;
      }
    }
    if (const core::ITable* const io = tables.FindTable("production_io")) {
      const std::uint32_t production = io->FindColumn("production");
      const std::uint32_t resource = io->FindColumn("resource");
      const std::uint32_t amount = io->FindColumn("amount");
      for (std::uint32_t row = 0;
           row < io->RowCount() && production != core::kNoTableColumn &&
           resource != core::kNoTableColumn && amount != core::kNoTableColumn;
           ++row) {
        if (io->CellText(row, production) == "firewood_splitting" &&
            io->CellText(row, resource) == "firewood") {
          m3_per_log_ = static_cast<double>(io->CellReal(row, amount).value_or(0.0F));
        }
      }
    }
  }

  /// The fuel mark of the day, if one is due: tracks the heating year's burn (want = burnt +
  /// short, HeatingState's saved heating-year record, the year turning in September), and from
  /// September to February marks the nearest stand for the logs the shortfall asks.
  bool FuelMark(core::ISimulation& simulation, const core::WorldState& world) {
    if (!(firewood_grams_per_m3_ > 0.0) || !(m3_per_log_ > 0.0)) {
      return false;
    }
    // The core's own heating-year record (HeatingState, saved - save_continue caught this bot
    // reading the unsaved probe tallies): last year's burn and dwellings, this year's so far.
    const core::HeatingState& heating = world.heating;
    const auto month = static_cast<std::uint32_t>(world.calendar.date.month);
    std::uint32_t houses = 0;
    for (const core::FamilyRow& family : world.families.rows) {
      houses += family.house.value != core::kInvalidEntityIdValue ? 1U : 0U;
    }
    const bool season = month >= static_cast<std::uint32_t>(core::Month::kSeptember) ||
                        month <= static_cast<std::uint32_t>(core::Month::kFebruary);
    if (!season || heating.last_season_houses == 0 || !(heating.last_season_want_m3 > 0.0F)) {
      return false;  // out of season, or no heating year closed yet (the start's wood: year 1)
    }
    const double target = static_cast<double>(heating.last_season_want_m3) *
                          static_cast<double>(houses) /
                          static_cast<double>(heating.last_season_houses);
    double on_hand = 0.0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.dead != 0) {
        continue;
      }
      on_hand += static_cast<double>(unit.heat_buffer_m3);
      if (firewood_.value < unit.stock.size()) {
        on_hand += static_cast<double>(unit.stock[firewood_.value]) / firewood_grams_per_m3_;
      }
    }
    on_hand += static_cast<double>(world.heating.fuel_log_grams) /
               static_cast<double>(catalog_.log_grams) * m3_per_log_;
    // What the season still burns: the target less what this heating year burnt already.
    const double still = target - static_cast<double>(heating.season_want_m3);
    const double short_m3 = still - on_hand;
    if (!(short_m3 > 0.0)) {
      return false;
    }
    const auto logs = static_cast<std::uint32_t>(std::ceil(short_m3 / m3_per_log_));
    core::OrderRow order;
    if (!NearestMark(simulation, logs, order)) {
      return false;
    }
    order.kind = core::OrderKind::kMarkFuelFelling;
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    ++fuel_marked_;
    cooldown_ = kCooldownDays;
    return true;
  }

  /// @param logs How many logs the mark is for: one granary's, or what the
  ///        village and its sites are short of when that is more.
  bool NearestMark(const core::ISimulation& simulation,
                   std::uint32_t logs,
                   core::OrderRow& order) const {
    const core::WorldState& world = simulation.CompletedState();
    const core::Vec2 centre = Centre(world);
    // The season's order (SeasonStandOrder): the farthest in the light season, else the nearest.
    const bool farthest = season_order_ && !InStockMonths(world);
    std::uint32_t best = core::kNoRow;
    float best_distance = 0.0F;
    float best_volume = 0.0F;
    for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
      const core::TimberStandRow& stand = world.stands.rows[row];
      // A GROWN PLANTING IS FELLED TOO, at its species' log share
      // (timber_planting.h, PlantedStandDef). It has no row of the stands
      // table, and until the planting policy this loop skipped every stand
      // without one — a zone grown in the run would have stood uncut for ever.
      const float share = LogShareOf(stand);
      // A stand still marked takes no second mark (timber_felling.cpp,
      // MarkFelling: kConflictsWithActive) — a stalled one among them.
      if (!(share > 0.0F) || !(stand.stock_m3 > 0.0F) || stand.marked_m3 > 0.0F) {
        continue;
      }
      // Enough of the stand for the logs asked, or what it has left.
      const float wanted_m3 = static_cast<float>(logs) * catalog_.log_m3 / share;
      const float volume = wanted_m3 < stand.stock_m3 ? wanted_m3 : stand.stock_m3;
      // A MARK THAT YIELDS NOT ONE LOG IS NOT A MARK: an old-forest square
      // holds a few cubic metres of trunks, and marking its crumbs would fell
      // nothing and be marked again the next day, for ever.
      if (volume * share < catalog_.log_m3) {
        continue;
      }
      // ONLY WHERE A FELLER CAN BE SENT TODAY — THE CORE'S OWN ANSWER
      // (ISimulation::FellingCanBeManned, the accountant's offering itself;
      // 0.37.208), as the building chairman asks MaterialsShortFor before a
      // start. Until then this policy measured the LOG CART's ride against
      // the road limit by a copy of the core's arithmetic (boss, parcel 308;
      // 0.36.12, 0.36.29) — the weaker question: the fellers walk, and a
      // stand the cart reached and no hand did was marked for nobody.
      if (!simulation.FellingCanBeManned(stand.position)) {
        continue;
      }
      const float dx = stand.position.x - centre.x;
      const float dy = stand.position.y - centre.y;
      const float distance = std::sqrt((dx * dx) + (dy * dy));
      if (best == core::kNoRow ||
          (farthest ? distance > best_distance : distance < best_distance)) {
        best = row;
        best_distance = distance;
        best_volume = volume;
      }
    }
    if (best == core::kNoRow) {
      return false;
    }
    order.kind = core::OrderKind::kMarkFelling;
    order.stand = world.stands.row_ids[best];
    order.volume_m3 = best_volume;
    return true;
  }

  core::TimberCatalog catalog_;

  /// A mark whose work has not moved for this many days holds no other
  /// felling (boss: N = 8, the core's own deadline's number).
  static constexpr std::uint32_t kStalledDays = 8;

  /// By stand row: the work left as last seen, and the days it has not moved.
  std::vector<float> seen_work_;
  std::vector<std::uint32_t> still_days_;

  bool ready_ = false;
  // #2261 fuel felling.
  core::ResourceId firewood_;
  double firewood_grams_per_m3_ = 0.0;
  double m3_per_log_ = 0.0;
  std::uint32_t fuel_marked_ = 0;

  std::uint32_t granary_logs_ = 60;

  /// Logs a first level takes, by unit_types row: what a site still waits for.
  std::vector<std::uint32_t> logs_by_type_;

  /// The next houses whose logs are kept ahead (KeepAhead). THREE BY DEFAULT,
  /// in every run that fells: the house policy's three sites at once
  /// (house_policy.h) — the next batch he will open.
  std::uint32_t houses_ahead_ = 3;
  bool season_stock_ = false;
  core::Grams season_target_max_ = 0;
  /// The stock's path, counted (Report): stock winter days held by a moving mark and free of
  /// one; of the free, lifted by the season, met in hand, without a mannable stand; its marks.
  std::uint32_t stock_held_days_ = 0;
  std::uint32_t stock_free_days_ = 0;
  std::uint32_t stock_lifted_days_ = 0;
  std::uint32_t stock_met_days_ = 0;
  std::uint32_t stock_no_stand_days_ = 0;
  std::uint32_t stock_marked_ = 0;

  /// November to February, the stock's months (month 0 = January).
  static bool InStockMonths(const core::WorldState& world) {
    const std::uint32_t month = (world.calendar.day % core::kDaysPerYear) / core::kDaysPerMonth;
    return month >= 10U || month <= 1U;
  }

  /// The log share of a stand's volume: a grown planting at its species', a table stand at its
  /// row's, the old forest's trunks at the old share.
  float LogShareOf(const core::TimberStandRow& stand) const {
    if (stand.kind == core::TimberStandKind::kPlanted) {
      return stand.species.value < catalog_.species.size()
                 ? catalog_.species[stand.species.value].log_share
                 : 0.0F;
    }
    if (stand.table_row < catalog_.stands.size()) {
      const core::TimberStandDef& def = catalog_.stands[stand.table_row];
      return stand.kind == core::TimberStandKind::kForestOld
                 ? def.log_share * catalog_.old_log_share_factor
                 : def.log_share;
    }
    return 0.0F;
  }

  /// LOGS IN HAND, and the logs already felled and lying count as in hand: a
  /// chairman who has a heap in the grove does not fell another for want of
  /// carts.
  ///
  /// A SITE'S LOGS ARE NOT IN HAND. They were delivered to their own building
  /// and nothing else can take them (construction_system.cpp). Counting them
  /// stopped the village for twenty years on 2026-09-13: a granary site held
  /// its sixty logs and waited for boards, the stores had none, this policy
  /// saw sixty in hand and never felled, and the sawmill had nothing to saw.
  core::Grams LogsInHand(const core::WorldState& world) const {
    core::Grams logs = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level == 0) {
        continue;
      }
      logs += catalog_.log_resource.value < unit.stock.size()
                  ? unit.stock[catalog_.log_resource.value]
                  : 0;
    }
    for (const core::TimberStandRow& stand : world.stands.rows) {
      logs += stand.load_grams;
    }
    return logs;
  }

  /// econ's P (#2072 season order): the logs standing where a hand reaches ON FOOT in today's
  /// light and the log cart within the road limit (home_reach.h's questions, the rule read off
  /// transport.csv and labor.csv). Negative when the rule's tables were not read.
  double WinterFootLogs(const core::WorldState& world) const {
    if (!rule_read_) {
      return -1.0;
    }
    double logs = 0.0;
    for (const core::TimberStandRow& stand : world.stands.rows) {
      const float share = LogShareOf(stand);
      if (!(share > 0.0F) || !(stand.stock_m3 > 0.0F)) {
        continue;
      }
      const float walk = core::NearestHomeTravelHours(
          world, stand.position, rule_.walk_speed_kmh, core::TravelMode::kWalk);
      const float cart = core::NearestHomeTravelHours(
          world, stand.position, rule_.ride_speed_kmh, core::TravelMode::kLogCart);
      if (walk < 0.0F || cart < 0.0F || cart > rule_.travel_limit_hours ||
          !core::RoadLeavesAWorkingDay(walk,
                                       world.weather.daylight_hours,
                                       rule_.travel_limit_hours,
                                       rule_.min_usable_hours)) {
        continue;
      }
      logs += static_cast<double>(stand.stock_m3 * share / catalog_.log_m3);
    }
    return logs;
  }

  /// The season's ledger, every day before anything returns: P on each year's first
  /// November day; on the first March day a stock season closes - aimed (the season ever
  /// raised the want), and met (the logs in hand cover the season's want by 1 March).
  void WatchTheSeason(const core::WorldState& world) {
    const std::uint32_t day_of_year = world.calendar.day % core::kDaysPerYear;
    if (day_of_year % core::kDaysPerMonth != 0) {
      return;
    }
    const std::uint32_t month = day_of_year / core::kDaysPerMonth;
    constexpr std::uint32_t kNovember = 10;
    constexpr std::uint32_t kMarch = 2;
    if (month == kNovember) {
      winter_foot_.push_back(WinterFoot{.year = (world.calendar.day / core::kDaysPerYear) + 1U,
                                        .logs = WinterFootLogs(world),
                                        .daylight = world.weather.daylight_hours});
    }
    if (month == kMarch && season_stock_) {
      if (season_aimed_) {
        ++seasons_aimed_;
        seasons_met_ += LogsInHand(world) >= season_want_ ? 1U : 0U;
      }
      season_aimed_ = false;
      season_want_ = 0;
    }
  }

  /// Reads the reach rule P asks with (transport.csv, labor.csv - the labour model's own).
  void ReadReachRule(const core::ITableSet& tables) {
    const auto real = [&tables](const char* table, const char* key, const char* column) {
      std::optional<float> value;
      if (const core::ITable* const found = tables.FindTable(table)) {
        value = found->CellReal(found->FindRowByKey(key), found->FindColumn(column));
      }
      return value;
    };
    const std::optional<float> walk = real("transport", "pedestrian", "speed_kmh");
    const std::optional<float> ride = real("transport", "horse_trot", "speed_kmh");
    const std::optional<float> limit = real("labor", "travel_limit_hours", "value");
    const std::optional<float> usable = real("labor", "min_usable_hours", "value");
    rule_read_ = walk && ride && limit && usable;
    if (rule_read_) {
      rule_ = core::ReachRule{.walk_speed_kmh = *walk,
                              .ride_speed_kmh = *ride,
                              .travel_limit_hours = *limit,
                              .min_usable_hours = *usable};
    }
  }

  struct WinterFoot {
    std::uint32_t year = 0;
    double logs = 0.0;
    float daylight = 0.0F;
  };

  bool season_order_ = false;
  bool rule_read_ = false;
  core::ReachRule rule_;
  std::vector<WinterFoot> winter_foot_;
  bool season_aimed_ = false;
  core::Grams season_want_ = 0;
  std::uint32_t seasons_aimed_ = 0;
  std::uint32_t seasons_met_ = 0;

  /// unit_types.csv row of the wooden house; past logs_by_type_ when absent.
  std::uint32_t house_type_ = core::kNoTableRow;

  RiseWatch rise_watch_;

  std::uint32_t cooldown_ = 0;

  std::uint32_t marked_ = 0;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_FELLING_POLICY_H_
