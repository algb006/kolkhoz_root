/// @file
/// @brief #1644, the run chairman's smokehouse (econ, econ-boss-1644-smokehouse
/// [1]-[2]; boss accepted): the canon never built or staffed it, so «salted
/// made 0» in every arm and the meat and fish spoiled unmeasured.
/// @threading SINGLE_THREADED — between steps, on the sim thread.
///
/// THE RULE (STUB econ):
///   * WHEN: the meat and fish spoiled in the last 12 game months (rolling, by
///     the book's daily rises, the turn's closed tail taken) reach 1 t;
///   * WHAT: the smokehouse marked on the food yard (the core finds its parent
///     from the position), started; a food_master appointed at the yard (the
///     smokehouse's work goes to its parent's post holders, labor_system.cpp
///     PutOnModuleWork) - an adult with no post. None free: the day counts as
///     «no hand» (the design's «post floors» have no home in the core: named);
///   * NO YARD STANDING: the day counts as «no yard» - the food yard is
///     FixturePolicy's to put up;
///   * the order «salt first while salt is above the table's reserve, smoke the
///     rest» is the CORE's (processing_shops.cpp), not this rule's.
/// OFF IN THE CANON (0.37.375, boss seq 24): 0.37.374 had it on (seq 17), and
/// the canon then lost 11 residents a village by y16 (-3.2 SE, births the
/// door) - so the canon is back on 373's world until the cost is named and
/// cured, and --smokehouse is the arm that turns it on.
/// Prints SMOKE1644 a village-year (K1-K7) and once a village.

#ifndef TESTS_RUN_COMMON_SMOKEHOUSE_POLICY_H_
#define TESTS_RUN_COMMON_SMOKEHOUSE_POLICY_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/processing_catalog.h"
#include "core_catalog/world_conventions.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/state_table_ops.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_sim/step.h"
#include "core_tables/tables.h"

namespace run {

class SmokehousePolicy {
 public:
  static constexpr core::Grams kTriggerGrams = 1'000'000;  ///< econ: 1 t a rolling year
  static constexpr std::uint32_t kYears = 16;

  explicit SmokehousePolicy(const core::ITableSet& tables) {
    const auto row_of = [&tables](const char* table, const char* key) {
      const core::ITable* const t = tables.FindTable(table);
      return t != nullptr ? t->FindRowByKey(key) : core::kNoTableRow;
    };
    yard_ = row_of("unit_types", "food_yard");
    smokehouse_ = row_of("unit_types", "smokehouse");
    master_ = row_of("professions", "food_master");
    meat_ = row_of("resources", "meat");
    fish_ = row_of("resources", "fish");
    salt_ = row_of("resources", "salt");
    firewood_ = row_of("resources", "firewood");
    salted_meat_ = row_of("resources", "salted_meat");
    salted_fish_ = row_of("resources", "salted_fish");
    smoked_meat_ = row_of("resources", "smoked_meat");
    smoked_fish_ = row_of("resources", "smoked_fish");
    barrel_ = row_of("resources", "barrel");
    board_ = row_of("resources", "board");
    workshops_ = row_of("unit_types", "workshops");
    food_store_ = row_of("unit_types", "food_store");
    house_ = row_of("unit_types", "wooden_house");
    utility_yard_ = row_of("unit_types", "utility_yard");
    craftsman_ = row_of("professions", "farm_craftsman");
    life_speedup_ = core::LifeSpeedupOr(tables, life_speedup_);
    if (const core::ITable* const world = tables.FindTable("world_params")) {
      const std::uint32_t column = world->FindColumn("value");
      const auto read = [world, column](const char* key, double& into) {
        const std::uint32_t row = world->FindRowByKey(key);
        if (row != core::kNoTableRow && column != core::kNoTableColumn) {
          if (const std::optional<float> value = world->CellReal(row, column)) {
            into = static_cast<double>(*value);
          }
        }
      };
      read("salt_norm_g_per_eater_day", norm_g_);
      read("salt_table_reserve_months", reserve_months_);
    }
    // THE SALT A GRAM OF MEAT OR FISH TAKES (#1644 (b)): the salting recipes'
    // salt over their main input, per batch, from the core's own catalogue.
    core::ProcessingCatalog catalog;
    std::string error;
    if (salt_ != core::kNoTableRow && core::ParseProcessingCatalog(tables, catalog, error)) {
      barrel_grams_ = catalog.barrel_grams;
      for (const core::ProcessingRecipe& recipe : catalog.recipes) {
        if (recipe.inputs.size() < 2 || recipe.inputs.front().grams <= 0) {
          continue;
        }
        for (const core::ProcessingAmount& input : recipe.inputs) {
          if (input.resource.value != salt_) {
            continue;
          }
          const double per_gram =
              static_cast<double>(input.grams) / static_cast<double>(recipe.inputs.front().grams);
          if (recipe.inputs.front().resource.value == meat_) {
            salt_per_meat_ = per_gram;
          } else if (recipe.inputs.front().resource.value == fish_) {
            salt_per_fish_ = per_gram;
          }
        }
      }
    }
  }

  /// @brief The salt the smokehouse wants above the table's reserve (#1644
  ///        (b), boss seq 2): while the shop stands and the arm is on, the
  ///        salt to salt the stores' meat and fish now held and the stores'
  ///        rot of the last game month; 0 otherwise. Read by the salt buyer.
  [[nodiscard]] core::Grams SaltExtraNeed() const { return salt_extra_need_g_; }

  void Enable(bool on) { on_ = on; }

  /// @brief K5's decomposition (boss seq 24): the arm without the cooper
  ///        (no workshops, no farm_craftsman for them), and the arm without
  ///        the food stores (none with the smokehouse, none on «no room»).
  /// @brief ARM d6 (econ seq 29, boss seq 30): the smokehouse from y3, the
  ///        second food store only while the housing lamp shows no queue.
  void FromYear3(bool on) { from_y3_ = on; }

  void Parts(bool cooper, bool stores, bool master, bool salt) {
    cooper_on_ = cooper;
    stores_on_ = stores;
    master_on_ = master;  // arm (4), boss seq 26: no food_master appointed
    salt_on_ = salt;      // arm (5), boss seq 26: no salt bought above the reserve
  }

  [[nodiscard]] bool On() const { return on_; }

  /// @brief The fixture difference, BEFORE the run measures.
  void Declare(std::string_view run_name) const {
    if (!on_) {
      return;  // the canon (off since 0.37.375)
    }
    {
      std::cout << run_name
                << ": ARM — --smokehouse (#1644; the canon has it off since 0.37.375): at 1 t of "
                   "meat and fish "
                   "spoiled in a rolling "
                   "year the run's chairman marks the smokehouse on the food yard and appoints a "
                   "food_master there; once it stands, the workshops on the utility yard and a "
                   "farm_craftsman there (the cooper, §8а «Под засолку»); a food_store at the "
                   "yard with the smokehouse, and one more on the shop's «no room»; the salt buyer "
                   "adds the salting's salt above "
                   "the table's reserve\n";
    }
  }

  /// @brief One day: the books' rises, the trigger, at most one order. Once a day.
  void RunDay(core::ISimulation& simulation) {
    const core::WorldState& world = simulation.CompletedState();
    CountDay(world);
    CountStops(simulation, world);
    salt_extra_need_g_ = on_ ? SaltForTheStores(world) : 0;
    salt_extra_max_g_ = std::max(salt_extra_max_g_, salt_extra_need_g_);
    if (!on_ || !triggered_) {
      return;
    }
    // ARM d6 (econ seq 29, boss seq 30): nothing marked before y3 - after the
    // act of acceptance; y1-y2 spoilage is the take-over years'.
    if (from_y3_ && world.calendar.day < 2U * core::kDaysPerYear) {
      return;
    }
    if (cooldown_ > 0) {
      --cooldown_;
      return;
    }
    core::OrderRow order;
    if (!NextOrder(world, order) && !NextStoreOrder(world, order) &&
        !(barrel_triggered_ && NextCooperOrder(world, order))) {
      return;
    }
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    cooldown_ = 1;
  }

  /// @brief The village's lines: one a year, then once.
  void Print(std::ostream& out, std::uint64_t seed) const {
    std::istringstream lines(lines_.str());
    std::string line;
    while (std::getline(lines, line)) {
      out << "population_curve: SMOKE1644 seed " << seed << ' ' << line << '\n';
    }
    out << "population_curve: SMOKE1644 seed " << seed << " arm " << (on_ ? 1 : 0)
        << " trigger_day " << Opt(trigger_day_) << " smokehouse_marked_day " << Opt(marked_day_)
        << " smokehouse_built_day " << Opt(built_day_) << " master_day " << Opt(master_day_)
        << " no_hand_days " << no_hand_days_ << " no_yard_days " << no_yard_days_
        << " barrel_trigger_day " << Opt(barrel_trigger_day_) << " workshops_marked_day "
        << Opt(workshops_marked_day_) << " workshops_built_day " << Opt(workshops_built_day_)
        << " cooper_day " << Opt(cooper_day_) << " no_utility_yard_days " << no_utility_yard_days_
        << " no_cooper_hand_days " << no_cooper_hand_days_ << " barrel_short_day "
        << Opt(barrel_short_day_) << " room_short_day " << Opt(room_short_day_)
        << " store_marked_day " << Opt(store_marked_day_) << " store_built_day "
        << Opt(store_built_day_) << " stores_asked " << stores_asked_ << " stores_built "
        << stores_built_ << " store_held_for_housing_days " << store_held_for_housing_days_
        << " holders" << (holders_.str().empty() ? " -" : holders_.str()) << '\n';
  }

 private:
  static std::string Opt(const std::optional<core::SimDay>& day) {
    return day ? std::to_string(*day) : std::string("never");
  }

  static core::Grams Of(const core::ResourceAmounts& amounts, std::uint32_t resource) {
    return resource < amounts.size() ? amounts[resource] : 0;
  }

  core::Grams Held(const core::WorldState& world, std::uint32_t resource) const {
    core::Grams held = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      held += Of(unit.stock, resource);
    }
    return held;
  }

  std::uint32_t FindOfType(const core::WorldState& world, std::uint32_t type) const {
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      if (world.units.rows[row].type.value == type) {
        return row;
      }
    }
    return core::kNoRow;
  }

  /// The day's rise of a book column, the turn's closed tail taken (#1580).
  core::Grams Rise(const core::WorldState& world,
                   core::Grams now_sum,
                   core::Grams closed_sum,
                   core::Grams& last) const {
    const bool turn = world.calendar.day > 0 && world.calendar.day % core::kDaysPerYear == 0;
    const core::Grams today = turn ? now_sum + (closed_sum > last ? closed_sum - last : 0)
                                   : (now_sum >= last ? now_sum - last : now_sum);
    last = now_sum;
    return today;
  }

  void CountDay(const core::WorldState& world) {
    const core::SimDay day = world.calendar.day;
    const std::uint32_t year = (day / core::kDaysPerYear) + 1U;
    if (day % core::kDaysPerYear == 0 && year >= 2 && year - 1U <= kYears) {
      CloseYear(world, year - 1U);
    }
    const core::YearLedger& book = world.ledger.current;
    const core::YearLedger& closed = world.ledger.closed;
    const core::Grams spoiled = Rise(world,
                                     Of(book.spoiled, meat_) + Of(book.spoiled, fish_),
                                     Of(closed.spoiled, meat_) + Of(closed.spoiled, fish_),
                                     last_spoiled_);
    const core::Grams salt_in =
        Rise(world, Of(book.processed, salt_), Of(closed.processed, salt_), last_salt_in_);
    // THE ROLLING YEAR, by game month.
    const core::Grams store_meat =
        Rise(world, StoreRot(book, meat_), StoreRot(closed, meat_), last_store_rot_[0]);
    const core::Grams store_fish =
        Rise(world, StoreRot(book, fish_), StoreRot(closed, fish_), last_store_rot_[1]);
    const std::uint32_t month = day / core::kDaysPerMonth;
    if (month != month_) {
      month_ = month;
      months_[month % months_.size()] = 0;
      store_rot_last_month_ = store_rot_month_;
      store_rot_month_ = {0, 0};
    }
    store_rot_month_[0] += store_meat;
    store_rot_month_[1] += store_fish;
    months_[month % months_.size()] += spoiled;
    core::Grams rolling = 0;
    for (const core::Grams grams : months_) {
      rolling += grams;
    }
    if (!triggered_ && rolling >= kTriggerGrams) {
      triggered_ = true;
      trigger_day_ = day;
    }
    // K3: a day the shops took salt while the store's salt stood under the
    // table's reserve (residents x norm x the month's days x the months).
    const double reserve = norm_g_ * static_cast<double>(world.residents.rows.size()) *
                           core::kDaysPerMonth * reserve_months_;
    // THAT RAW COUNT READS THE WRONG MOMENT (k1e: 345 days, deficits to 333 kg):
    // the shops settle at the day's last tick, and the month's salt issue, the
    // distribution and the ration take from the stores at the next day's
    // boundary - after the shop, before this read. So the table eating its
    // reserve read as a shop under it. K3 proper is the stock AT THE SHOP: this
    // read's stock plus what the boundary handed out of the stores since
    // (issued + ration + advance of salt), against the reserve at the previous
    // read's residents (the shop's). A day a district cart with salt arrived
    // also lands after the shop and would hide a dip: such a day is printed as
    // not measured, never as green.
    const double held_salt = static_cast<double>(Held(world, salt_));
    if (salt_in > 0 && held_salt < reserve) {
      ++under_reserve_days_;
    }
    const core::Grams handed_out =
        Rise(world, Of(book.issued, salt_), Of(closed.issued, salt_), last_salt_issued_) +
        Rise(world, Of(book.ration, salt_), Of(closed.ration, salt_), last_salt_ration_) +
        Rise(world, Of(book.advance, salt_), Of(closed.advance, salt_), last_salt_advance_);
    core::Grams in_carts = 0;
    for (const core::LimitDeliveryRow& cart : world.limit_deliveries.rows) {
      in_carts += Of(cart.goods, salt_);
    }
    const bool cart_arrived = in_carts < last_salt_in_carts_;
    last_salt_in_carts_ = in_carts;
    if (salt_in > 0 && last_reserve_g_ > 0.0) {
      const double at_shop = held_salt + static_cast<double>(handed_out);
      if (cart_arrived) {
        ++k3_unmeasured_days_;
      } else if (at_shop + 1.0 < last_reserve_g_) {
        ++k3_red_days_;
        k3_red_max_g_ = std::max(k3_red_max_g_, last_reserve_g_ - at_shop);
      }
    }
    last_reserve_g_ = reserve;
    const core::Grams firewood = Held(world, firewood_);
    if (!firewood_low_ || firewood < *firewood_low_) {
      firewood_low_ = firewood;
    }
    const std::uint32_t house = FindOfType(world, smokehouse_);
    if (house != core::kNoRow && world.units.rows[house].level > 0 && !built_day_) {
      built_day_ = day;
    }
    // WHERE THE MEAT LIES AND WHO WORKS IT (#1644's first read: the arm salted
    // 0.2-1.1 t a year and spoiled 96 % of the base): the stores' meat and fish
    // the shop can take, the yards' pantries it cannot; the shop's asked days;
    // the days a resident worked in it; whether a food_master holds the yard.
    ++days_;
    // THE BIRTHS' MULTIPLIER'S INPUT (decision 106: satisfaction scales the
    // draw; the arms lost births drawn, not stopped ones): the families'
    // satisfaction and its two live components, and the lodged families.
    for (const core::FamilyRow& family : world.families.rows) {
      satisfaction_sum_ += static_cast<double>(family.satisfaction);
      satiety_comp_sum_ += static_cast<double>(family.component_satiety);
      rest_comp_sum_ += static_cast<double>(family.component_rest);
      lodged_family_days_ += family.lodged_in.value != core::kInvalidEntityIdValue ? 1U : 0U;
      ++family_days_;
    }
    store_meat_sum_ += Of(StockOf(world), meat_) + Of(StockOf(world), fish_);
    for (const core::FamilyRow& family : world.families.rows) {
      pantry_meat_sum_ += Of(family.pantry, meat_) + Of(family.pantry, fish_);
    }
    if (house != core::kNoRow) {
      const core::UnitRow& shop = world.units.rows[house];
      demand_days_ += shop.production_days_written > 0.0F ? 1U : 0U;
      const core::UnitId shop_id = world.units.row_ids[house];
      bool worked = false;
      bool held = false;
      for (const core::ResidentRow& person : world.residents.rows) {
        worked = worked || person.work.unit.value == shop_id.value;
        held = held || person.post.profession.value == master_;
      }
      worked_days_ += worked ? 1U : 0U;
      master_held_days_ += held ? 1U : 0U;
    }
  }

  /// THE SHOP'S OWN WORD WHY IT STANDS (kProcessingStopped on the smokehouse:
  /// short of a resource, no room for one, or too far), counted by resource a
  /// year - the second read of #1644 (the shop asked no work from y10 with
  /// ~200 kg of meat in the stores).
  void CountStops(const core::ISimulation& simulation, const core::WorldState& world) {
    const std::uint32_t house = FindOfType(world, smokehouse_);
    if (house == core::kNoRow || world.units.rows[house].level == 0) {
      return;
    }
    // THE COOPER AHEAD OF THE WALL (boss seq 9, (i)): the workshops and their
    // cooper go up when the smokehouse stands - k1e built them after the
    // turn's wear had already left the shop without barrels (y9-y15). The
    // cooper still works by §8а «Под засолку», only while barrels are short.
    if (!barrel_triggered_) {
      barrel_triggered_ = true;
      barrel_trigger_day_ = world.calendar.day;
    }
    std::vector<core::Alarm> alarms;
    simulation.CollectAlarms(alarms);
    // d6: the housing lamp's queue today (a second food store waits on it).
    housing_short_today_ = std::ranges::any_of(alarms, [](const core::Alarm& alarm) {
      return alarm.kind == core::AlarmKind::kHousingShort;
    });
    const core::UnitId shop = world.units.row_ids[house];
    for (const core::Alarm& alarm : alarms) {
      if (alarm.kind == core::AlarmKind::kProcessingStopped && alarm.unit.value == shop.value) {
        const char* why = alarm.stop_reason == core::ProcessingStopReason::kNoRoom    ? "room"
                          : alarm.stop_reason == core::ProcessingStopReason::kShortOf ? "short"
                                                                                      : "far";
        ++stops_[std::string(why) + "_r" + std::to_string(alarm.resource.value)];
        if (alarm.stop_reason == core::ProcessingStopReason::kShortOf &&
            alarm.resource.value == barrel_ && !barrel_short_day_) {
          barrel_short_day_ = world.calendar.day;
        }
        // (ii): the shop's output finds no room - one more food store.
        if (alarm.stop_reason == core::ProcessingStopReason::kNoRoom) {
          room_wanted_ = true;
          if (!room_short_day_) {
            room_short_day_ = world.calendar.day;
          }
        }
      }
    }
    // THE COOPER'S OWN WORD WHY IT STANDS (k1h: a village made 0-8 barrels a
    // year y13-y16 with the smokehouse short of them): «cooper_short_rN» etc.
    const std::uint32_t workshops = FindOfType(world, workshops_);
    if (workshops != core::kNoRow && world.units.rows[workshops].level > 0) {
      const core::UnitId cooper = world.units.row_ids[workshops];
      for (const core::Alarm& alarm : alarms) {
        if (alarm.kind == core::AlarmKind::kProcessingStopped && alarm.unit.value == cooper.value) {
          const char* why = alarm.stop_reason == core::ProcessingStopReason::kNoRoom    ? "room"
                            : alarm.stop_reason == core::ProcessingStopReason::kShortOf ? "short"
                                                                                        : "far";
          ++stops_[std::string("cooper_") + why + "_r" + std::to_string(alarm.resource.value)];
        }
      }
    }
  }

  /// Every store's stock summed, by resource.
  core::ResourceAmounts StockOf(const core::WorldState& world) const {
    core::ResourceAmounts sum;
    for (const core::UnitRow& unit : world.units.rows) {
      if (sum.size() < unit.stock.size()) {
        sum.resize(unit.stock.size(), 0);
      }
      for (std::size_t resource = 0; resource < unit.stock.size(); ++resource) {
        sum[resource] += unit.stock[resource];
      }
    }
    return sum;
  }

  bool NextOrder(const core::WorldState& world, core::OrderRow& order) {
    const std::uint32_t yard = FindOfType(world, yard_);
    if (yard == core::kNoRow || world.units.rows[yard].level == 0) {
      ++no_yard_days_;
      return false;
    }
    const std::uint32_t house = FindOfType(world, smokehouse_);
    if (house == core::kNoRow) {
      order.kind = core::OrderKind::kBuildUnit;
      order.unit_type = core::UnitTypeId{static_cast<std::uint16_t>(smokehouse_)};
      order.position = world.units.rows[yard].position;
      if (!marked_day_) {
        marked_day_ = world.calendar.day;
        // (iv), boss seq 15: the food store is asked WITH the smokehouse - in
        // k1h/k1i all 32 villages had «no room» in y2, and a store asked on
        // it stood only in y3.
        room_wanted_ = true;
      }
      return true;
    }
    const core::UnitRow& site = world.units.rows[house];
    if (site.level == 0) {
      if (site.construction.phase != core::ConstructionPhase::kMarked) {
        return false;  // delivering or building
      }
      order.kind = core::OrderKind::kStartBuild;
      order.unit = world.units.row_ids[house];
      return true;
    }
    const core::UnitId yard_id = world.units.row_ids[yard];
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& person = world.residents.rows[row];
      if (person.post.profession.value == master_ && person.post.unit.value == yard_id.value) {
        NoteHolder(world, row, "master");
        return false;  // held
      }
    }
    if (!master_on_) {
      return false;  // the decomposition's arm (4): the shop stands unmanned
    }
    const std::uint32_t candidate = NextCandidate(world);
    if (candidate == core::kNoRow) {
      ++no_hand_days_;
      return false;
    }
    order.kind = core::OrderKind::kAppoint;
    order.resident = world.residents.row_ids[candidate];
    order.unit = yard_id;
    order.profession = core::ProfessionId{static_cast<std::uint16_t>(master_)};
    if (!master_day_) {
      master_day_ = world.calendar.day;
    }
    return true;
  }

  /// THE COOPER (§8а «Под засолку», boss seq 6): the workshops on the utility
  /// yard, then a farm_craftsman appointed there for them - one more than the
  /// yard held when the workshops stood, and again whenever it falls under.
  /// THE FOOD STORE ON «NO ROOM» (boss seq 9, (ii)): k1e's smokehouse stood
  /// in y2 with its salted meat finding no room - those goods keep only in the
  /// food yard's food_store (resource_stores.csv), whose one level holds 60 t.
  /// So on the shop's «no room» one more food_store goes up at the yard; a
  /// raised site is seen through before another is asked.
  bool NextStoreOrder(const core::WorldState& world, core::OrderRow& order) {
    if (!stores_on_) {
      return false;  // the decomposition's arm (2)
    }
    const std::uint32_t yard = FindOfType(world, yard_);
    if (yard == core::kNoRow || world.units.rows[yard].level == 0 ||
        food_store_ == core::kNoTableRow) {
      return false;
    }
    // A food_store site that appears after the rule's mark is the rule's.
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      const std::uint32_t id = world.units.row_ids[row].value;
      if (unit.type.value != food_store_ ||
          std::ranges::find(known_stores_, id) != known_stores_.end()) {
        continue;
      }
      known_stores_.push_back(id);
      if (store_mark_pending_ && unit.level == 0) {
        own_store_sites_.push_back(id);
        store_mark_pending_ = false;
      }
    }
    // ONLY ITS OWN SITE (k1h): the canon raises a food_store of its own, and a
    // rule that waited on that site asked its store a year late - y2's «no
    // room» was answered in y3 in 13 villages. So the rule sees through the
    // site it marked, not the canon's.
    for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
      const core::UnitRow& unit = world.units.rows[row];
      if (unit.type.value != food_store_ || unit.level > 0 ||
          std::ranges::find(own_store_sites_, world.units.row_ids[row].value) ==
              own_store_sites_.end()) {
        continue;
      }
      if (unit.construction.phase != core::ConstructionPhase::kMarked) {
        return false;  // delivering or building
      }
      order.kind = core::OrderKind::kStartBuild;
      order.unit = world.units.row_ids[row];
      return true;
    }
    std::uint32_t stores = 0;
    for (const core::UnitRow& unit : world.units.rows) {
      stores += unit.type.value == food_store_ && unit.level > 0 ? 1U : 0U;
    }
    if (!stores_at_start_) {
      stores_at_start_ = stores;  // the start's food stores are not the rule's
    }
    if (stores > *stores_at_start_ + stores_built_) {
      stores_built_ = stores - *stores_at_start_;
      if (!store_built_day_) {
        store_built_day_ = world.calendar.day;
      }
    }
    // ARM d6: a store on «no room» only while the housing lamp shows no queue
    // (the with-smokehouse store is asked at its mark, before this).
    if (from_y3_ && housing_short_today_ && stores_asked_ > 0) {
      ++store_held_for_housing_days_;
      return false;
    }
    if (!room_wanted_) {
      return false;
    }
    room_wanted_ = false;
    order.kind = core::OrderKind::kBuildUnit;
    order.unit_type = core::UnitTypeId{static_cast<std::uint16_t>(food_store_)};
    order.position = world.units.rows[yard].position;
    store_mark_pending_ = true;
    ++stores_asked_;
    if (!store_marked_day_) {
      store_marked_day_ = world.calendar.day;
    }
    return true;
  }

  bool NextCooperOrder(const core::WorldState& world, core::OrderRow& order) {
    if (!cooper_on_) {
      return false;  // the decomposition's arm (1)
    }
    const std::uint32_t yard = FindOfType(world, utility_yard_);
    if (yard == core::kNoRow || world.units.rows[yard].level == 0) {
      ++no_utility_yard_days_;
      return false;
    }
    const std::uint32_t shop = FindOfType(world, workshops_);
    if (shop == core::kNoRow) {
      order.kind = core::OrderKind::kBuildUnit;
      order.unit_type = core::UnitTypeId{static_cast<std::uint16_t>(workshops_)};
      order.position = world.units.rows[yard].position;
      if (!workshops_marked_day_) {
        workshops_marked_day_ = world.calendar.day;
      }
      return true;
    }
    const core::UnitRow& site = world.units.rows[shop];
    if (site.level == 0) {
      if (site.construction.phase != core::ConstructionPhase::kMarked) {
        return false;  // delivering or building
      }
      order.kind = core::OrderKind::kStartBuild;
      order.unit = world.units.row_ids[shop];
      return true;
    }
    const core::UnitId yard_id = world.units.row_ids[yard];
    std::uint32_t held = 0;
    for (const core::ResidentRow& person : world.residents.rows) {
      held += person.post.profession.value == craftsman_ && person.post.unit.value == yard_id.value
                  ? 1U
                  : 0U;
    }
    if (!craftsmen_wanted_) {
      if (!workshops_built_day_) {
        workshops_built_day_ = world.calendar.day;
      }
      craftsmen_wanted_ = held + 1U;
    }
    if (held >= *craftsmen_wanted_) {
      return false;
    }
    const std::uint32_t candidate = NextCandidate(world);
    if (candidate == core::kNoRow) {
      ++no_cooper_hand_days_;
      return false;
    }
    order.kind = core::OrderKind::kAppoint;
    order.resident = world.residents.row_ids[candidate];
    order.unit = yard_id;
    order.profession = core::ProfessionId{static_cast<std::uint16_t>(craftsman_)};
    if (!cooper_day_) {
      cooper_day_ = world.calendar.day;
    }
    return true;
  }

  /// THE SALTING'S SALT (#1644 (b)): while the smokehouse stands built, the
  /// salt its recipes take for the stores' meat and fish now held and for the
  /// stores' rot of the last game month (what it would have salted).
  core::Grams SaltForTheStores(const core::WorldState& world) const {
    const std::uint32_t house = FindOfType(world, smokehouse_);
    if (!salt_on_ || house == core::kNoRow || world.units.rows[house].level == 0) {
      return 0;  // arm (5): no salt above the reserve - the shop takes what the table leaves
    }
    const core::ResourceAmounts stock = StockOf(world);
    const double meat = static_cast<double>(Of(stock, meat_) + store_rot_last_month_[0]);
    const double fish = static_cast<double>(Of(stock, fish_) + store_rot_last_month_[1]);
    return static_cast<core::Grams>(
        std::llround((meat * salt_per_meat_) + (fish * salt_per_fish_)));
  }

  /// WHO HOLDS THE POST (boss seq 26: «a young unmarried adult taken off the
  /// marriage path is my first suspect»): each new holder once, as found the
  /// day after the appointment - age, sex, married or not.
  void NoteHolder(const core::WorldState& world, std::uint32_t row, const char* post) {
    const std::uint32_t id = world.residents.row_ids[row].value;
    if (std::ranges::find(holders_seen_, id) != holders_seen_.end()) {
      return;
    }
    holders_seen_.push_back(id);
    const core::ResidentRow& person = world.residents.rows[row];
    const float age = core::BiologicalAgeYears(life_speedup_, person.birth_day, world.calendar.day);
    holders_ << ' ' << post << ":y" << (world.calendar.day / core::kDaysPerYear) + 1U << ":a"
             << static_cast<int>(age) << ':' << (person.sex == core::Sex::kFemale ? 'f' : 'm')
             << ':' << (person.spouse.value != core::kInvalidEntityIdValue ? "wed" : "single");
  }

  /// Boards gone into buildings of `type` in a book (built_in_by_type).
  core::Grams BoardsInto(const core::YearLedger& book, std::uint32_t type) const {
    core::Grams grams = 0;
    for (const core::BuiltInRow& row : book.built_in_by_type) {
      if (row.type.value == type && row.resource.value == board_) {
        grams += row.grams;
      }
    }
    return grams;
  }

  /// The stores' rot of `resource` in a book: all of it but the pantries'.
  static core::Grams StoreRot(const core::YearLedger& book, std::uint32_t resource) {
    return Of(book.spoiled, resource) - Of(book.spoiled_in_pantries, resource);
  }

  /// An adult with no post, a different one each time the last did not take.
  std::uint32_t NextCandidate(const core::WorldState& world) {
    const std::uint32_t skip = candidate_++;
    std::uint32_t seen = 0;
    for (std::uint32_t row = 0; row < world.residents.rows.size(); ++row) {
      const core::ResidentRow& resident = world.residents.rows[row];
      if (resident.post.profession.value != core::kInvalidDefIdValue) {
        continue;
      }
      if (core::BiologicalAgeYears(life_speedup_, resident.birth_day, world.calendar.day) <
          kAdultYears) {
        continue;
      }
      if (seen++ < skip % (world.residents.rows.size() + 1U)) {
        continue;
      }
      return row;
    }
    candidate_ = 0;
    return core::kNoRow;
  }

  void CloseYear(const core::WorldState& world, std::uint32_t year) {
    const core::YearLedger& book = world.ledger.closed;
    lines_ << "year " << year << " arm " << (on_ ? 1 : 0) << " salted_made_kg "
           << (Of(book.made, salted_meat_) + Of(book.made, salted_fish_)) / 1000
           << " smoked_made_kg "
           << (Of(book.made, smoked_meat_) + Of(book.made, smoked_fish_)) / 1000
           << " meat_fish_spoiled_kg " << (Of(book.spoiled, meat_) + Of(book.spoiled, fish_)) / 1000
           << " salt_to_barrels_kg " << Of(book.processed, salt_) / 1000 << " firewood_to_shops_kg "
           << Of(book.processed, firewood_) / 1000 << " firewood_lowest_kg "
           << (firewood_low_ ? *firewood_low_ / 1000 : -1) << " days_salting_under_reserve "
           << under_reserve_days_ << " k3_red_days " << k3_red_days_ << " k3_red_max_kg "
           << static_cast<std::int64_t>(k3_red_max_g_ / 1000.0) << " k3_unmeasured_days "
           << k3_unmeasured_days_
           // K1' (econ seq 13): what ARRIVED in the stores - the kolkhoz herds'
           // meat (and fish) in herd_produce, the only writer of meat into a
           // store (herd_life.cpp DeliverProduce; the yards' goes to
           // yard_produce, fish reaches only the pantries).
           // K5's DOOR (boss seq 24; 374's canon lost births and weddings):
           // the births' draws and stops (#1205), the weddings, and the boards
           // gone into houses against all boards built in.
           << " satisfaction_x100 " << MeanX100(satisfaction_sum_) << " satiety_comp_x100 "
           << MeanX100(satiety_comp_sum_) << " rest_comp_x100 " << MeanX100(rest_comp_sum_)
           << " lodged_family_days " << lodged_family_days_ << " family_days " << family_days_
           << " births " << book.births << " births_drawn " << book.births_drawn
           << " births_stopped_health " << book.births_stopped_health << " births_stopped_away "
           << book.births_stopped_away << " births_stopped_satiety " << book.births_stopped_satiety
           << " weddings " << book.weddings << " boards_into_houses_kg "
           << BoardsInto(book, house_) / 1000 << " boards_built_in_kg "
           << Of(book.built_in, board_) / 1000 << " meatfish_store_inflow_kg "
           << (Of(book.herd_produce, meat_) + Of(book.herd_produce, fish_)) / 1000
           << " meatfish_pantry_spoiled_kg "
           << (Of(book.spoiled_in_pantries, meat_) + Of(book.spoiled_in_pantries, fish_)) / 1000
           << " meatfish_store_spoiled_kg "
           << (Of(book.spoiled, meat_) + Of(book.spoiled, fish_) -
               Of(book.spoiled_in_pantries, meat_) - Of(book.spoiled_in_pantries, fish_)) /
                  1000
           << " salt_extra_max_kg " << salt_extra_max_g_ / 1000 << " barrels_made "
           << (barrel_ != core::kNoTableRow && barrel_grams_ > 0
                   ? Of(book.made, barrel_) / barrel_grams_
                   : -1)
           << " boards_to_barrels_kg " << Of(book.processed, board_) / 1000
           << " store_meatfish_mean_kg " << (days_ > 0 ? store_meat_sum_ / days_ / 1000 : 0)
           << " pantry_meatfish_mean_kg " << (days_ > 0 ? pantry_meat_sum_ / days_ / 1000 : 0)
           << " shop_asked_days " << demand_days_ << " shop_worked_days " << worked_days_
           << " master_held_days " << master_held_days_ << " days " << days_ << " stops";
    for (const auto& [why, count] : stops_) {
      lines_ << ' ' << why << ':' << count;
    }
    lines_ << (stops_.empty() ? " -" : "") << '\n';
    stops_.clear();
    under_reserve_days_ = 0;
    k3_red_days_ = 0;
    k3_red_max_g_ = 0.0;
    k3_unmeasured_days_ = 0;
    salt_extra_max_g_ = 0;
    firewood_low_.reset();
    days_ = 0;
    store_meat_sum_ = 0;
    pantry_meat_sum_ = 0;
    satisfaction_sum_ = 0.0;
    satiety_comp_sum_ = 0.0;
    rest_comp_sum_ = 0.0;
    lodged_family_days_ = 0;
    family_days_ = 0;
    demand_days_ = 0;
    worked_days_ = 0;
    master_held_days_ = 0;
  }

  static constexpr float kAdultYears = 16.0F;
  bool on_ = false;  // off again in 0.37.375 (boss seq 24: K5 red on population)
  bool triggered_ = false;
  std::uint32_t cooldown_ = 0;
  std::uint32_t candidate_ = 0;
  std::uint32_t yard_ = core::kNoTableRow;
  std::uint32_t smokehouse_ = core::kNoTableRow;
  std::uint32_t master_ = core::kNoTableRow;
  std::uint32_t meat_ = core::kNoTableRow;
  std::uint32_t fish_ = core::kNoTableRow;
  std::uint32_t salt_ = core::kNoTableRow;
  std::uint32_t firewood_ = core::kNoTableRow;
  std::uint32_t salted_meat_ = core::kNoTableRow;
  std::uint32_t salted_fish_ = core::kNoTableRow;
  std::uint32_t smoked_meat_ = core::kNoTableRow;
  std::uint32_t smoked_fish_ = core::kNoTableRow;
  float life_speedup_ = 4.0F;
  double norm_g_ = 125.0;
  double reserve_months_ = 3.0;
  std::array<core::Grams, 12> months_{};
  std::uint32_t month_ = 0;
  core::Grams last_spoiled_ = 0;
  core::Grams last_salt_in_ = 0;
  std::uint32_t under_reserve_days_ = 0;
  std::uint32_t barrel_ = core::kNoTableRow;
  std::uint32_t board_ = core::kNoTableRow;
  std::uint32_t workshops_ = core::kNoTableRow;
  std::uint32_t utility_yard_ = core::kNoTableRow;
  std::uint32_t craftsman_ = core::kNoTableRow;
  core::Grams barrel_grams_ = 0;
  double salt_per_meat_ = 0.0;
  double salt_per_fish_ = 0.0;
  core::Grams salt_extra_need_g_ = 0;
  core::Grams salt_extra_max_g_ = 0;
  std::array<core::Grams, 2> last_store_rot_{0, 0};
  std::array<core::Grams, 2> store_rot_month_{0, 0};
  std::array<core::Grams, 2> store_rot_last_month_{0, 0};
  bool barrel_triggered_ = false;
  std::optional<core::SimDay> barrel_trigger_day_;
  std::optional<core::SimDay> workshops_marked_day_;
  std::optional<core::SimDay> workshops_built_day_;
  std::optional<core::SimDay> cooper_day_;
  std::optional<std::uint32_t> craftsmen_wanted_;
  std::uint32_t no_utility_yard_days_ = 0;
  std::uint32_t no_cooper_hand_days_ = 0;
  std::uint32_t food_store_ = core::kNoTableRow;
  std::uint32_t house_ = core::kNoTableRow;
  bool cooper_on_ = true;
  bool stores_on_ = true;
  bool master_on_ = true;
  bool from_y3_ = false;
  bool housing_short_today_ = false;
  std::uint32_t store_held_for_housing_days_ = 0;
  bool salt_on_ = true;
  std::vector<std::uint32_t> holders_seen_;
  std::ostringstream holders_;
  bool room_wanted_ = false;
  std::uint32_t stores_built_ = 0;
  std::optional<std::uint32_t> stores_at_start_;
  std::vector<std::uint32_t> known_stores_;
  std::vector<std::uint32_t> own_store_sites_;
  bool store_mark_pending_ = false;
  std::uint32_t stores_asked_ = 0;
  std::optional<core::SimDay> barrel_short_day_;
  std::optional<core::SimDay> room_short_day_;
  std::optional<core::SimDay> store_marked_day_;
  std::optional<core::SimDay> store_built_day_;
  std::uint32_t k3_red_days_ = 0;
  double k3_red_max_g_ = 0.0;
  std::uint32_t k3_unmeasured_days_ = 0;
  double last_reserve_g_ = 0.0;
  core::Grams last_salt_issued_ = 0;
  core::Grams last_salt_ration_ = 0;
  core::Grams last_salt_advance_ = 0;
  core::Grams last_salt_in_carts_ = 0;
  std::int64_t days_ = 0;
  double satisfaction_sum_ = 0.0;
  double satiety_comp_sum_ = 0.0;
  double rest_comp_sum_ = 0.0;
  std::uint32_t lodged_family_days_ = 0;
  std::uint32_t family_days_ = 0;

  /// A family-day mean × 100, as an integer (the year lines carry integers).
  std::int64_t MeanX100(double sum) const {
    return family_days_ > 0 ? std::llround(100.0 * sum / static_cast<double>(family_days_)) : -1;
  }

  core::Grams store_meat_sum_ = 0;
  core::Grams pantry_meat_sum_ = 0;
  std::uint32_t demand_days_ = 0;
  std::uint32_t worked_days_ = 0;
  std::uint32_t master_held_days_ = 0;
  std::map<std::string, std::uint32_t> stops_;  ///< «room_rN» / «short_rN» / «far_rN» days
  std::optional<core::Grams> firewood_low_;
  std::optional<core::SimDay> trigger_day_;
  std::optional<core::SimDay> marked_day_;
  std::optional<core::SimDay> built_day_;
  std::optional<core::SimDay> master_day_;
  std::uint32_t no_hand_days_ = 0;
  std::uint32_t no_yard_days_ = 0;
  std::ostringstream lines_;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_SMOKEHOUSE_POLICY_H_
