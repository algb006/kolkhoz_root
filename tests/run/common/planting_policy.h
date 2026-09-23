/// @file
/// @brief The run's chairman plants a zone of pine every spring.
/// @threading SINGLE_THREADED
/// Test-side code, driven from the thread that owns the simulation.
///
/// WHY THE RUN PLANTS (boss, boss-core-epoch1-3 seq 18 and 25): the groves
/// are felled to nothing by year ~20 and, until 0.34.35, the village had no
/// way to grow timber again. Now it has (timber_planting.h), and a run has no
/// chairman to order it — so the run plays him, as it plays the felling.
///
/// ONE ANSWER A YEAR, AND NOTHING SMARTER: on the first day of April, if no
/// zone he ordered is still waiting for its planters, he orders 1.5 ha of
/// pine — on a grove, belt or grown planting felled to nothing if there is
/// one within the planters' walk, else on a new zone on rings round the
/// village. WHERE a zone may stand is the core's to say: a candidate the core
/// refuses (a field, a plot, another stand, off the map) is seen the next day
/// by no new zone having appeared, and the next candidate is tried. The
/// instrument enters by the player's own door.
///
/// THE FIXTURE DIFFERS FROM THE START CANON, AND IT SAYS SO (Declare).

#ifndef TESTS_RUN_COMMON_PLANTING_POLICY_H_
#define TESTS_RUN_COMMON_PLANTING_POLICY_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/timber_state.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "core_tables/tables.h"
#include "core_world/world.h"

namespace run {

class PlantingPolicy {
 public:
  explicit PlantingPolicy(const core::ITableSet& tables) {
    std::string error;
    const bool catalog = core::ParseTimberCatalog(tables, catalog_, error);
    const core::ITable* const species = tables.FindTable("tree_species");
    const std::uint32_t pine = species != nullptr ? species->FindRowByKey("pine") : core::kNoRow;
    ready_ =
        catalog && pine < catalog_.species.size() && catalog_.species[pine].years_to_logs > 0.0F;
    pine_ = core::TreeSpeciesId{static_cast<std::uint16_t>(ready_ ? pine : 0U)};
    walk_hours_per_km_ = static_cast<float>(core::kClockScale) /
                         Cell(tables, "transport", "pedestrian", "speed_kmh", 5.0F);
    walk_limit_hours_ = Cell(tables, "labor", "travel_limit_hours", "value", 4.0F) / 2.0F;
    map_side_m_ = MapSide(tables);
  }

  /// @brief The fixture difference, in words, for the run to print BEFORE it
  /// measures anything.
  static void Declare(const std::string& run) {
    std::cout << run
              << ": FIXTURE DIFFERS FROM THE START CANON — the run's chairman PLANTS 1.5 ha of "
                 "pine every April from the first year, on a grove felled to nothing within "
                 "the planters' walk or on a new zone the core accepts (boss, boss-core-epoch1-3 "
                 "seq 18 and 25)\n";
  }

  /// @brief Switches the policy off for a CONTROL run: the same village, the
  /// same chairman, no planting — the one parameter moved.
  void Disable() { ready_ = false; }

  /// @brief One day of the chairman's attention. Call once a day.
  void RunDay(core::ISimulation& simulation) {
    if (!ready_) {
      return;
    }
    const core::WorldState& world = simulation.CompletedState();
    CountCycles(world);
    const std::uint32_t waiting = UnplantedZones(world);
    // YESTERDAY'S ORDER, READ AT THE SUBJECT: a new zone waiting for its
    // planters is the core's "yes"; no new one is its refusal.
    if (awaiting_) {
      awaiting_ = false;
      if (waiting > waiting_before_) {
        ++ordered_;
        done_year_ = world.calendar.day / core::kDaysPerYear;
        return;
      }
      ++refused_;
      ++candidate_;
      TryOne(simulation, world);
      return;
    }
    const std::uint32_t year = world.calendar.day / core::kDaysPerYear;
    if (world.calendar.day % core::kDaysPerYear < kPlantDayOfYear || year == done_year_) {
      return;
    }
    if (waiting > 0) {
      ++held_back_;  // last year's zone still waits: one zone at a time
      done_year_ = year;
      return;
    }
    candidate_ = 0;
    TryOne(simulation, world);
  }

  /// @brief What the policy did and what grew of it.
  ///
  /// TWO KINDS OF COUNT, AND THE FIRST SUMMARY HAD ONLY ONE (boss,
  /// boss-core-epoch1-4 seq 6): `planted`, `grown` and `grown_m3` read the
  /// stand ROWS alive now, and a row is planted, grown, felled and planted
  /// again — so 20 zones ordered read as 6 planted and 1 grown. The `_ever`
  /// counts are cumulative, one per planting CYCLE (a row's planted_day).
  struct Summary {
    std::uint32_t ordered = 0;       ///< Zones the core accepted.
    std::uint32_t planted = 0;       ///< Rows planted, as they stand NOW.
    std::uint32_t grown = 0;         ///< Rows grown to logs, as they stand NOW.
    float standing_m3 = 0.0F;        ///< Timber standing in plantings now.
    float grown_m3 = 0.0F;           ///< Timber the rows grown NOW held at maturity.
    std::uint32_t planted_ever = 0;  ///< Planting cycles finished by the planters.
    std::uint32_t grown_ever = 0;    ///< Planting cycles that reached maturity.
    float felled_ever_m3 = 0.0F;     ///< Timber felled out of plantings, day over day.
    std::uint32_t refused = 0;       ///< Candidates the core refused.
    std::uint32_t held_back = 0;     ///< Years a zone still waited for planters.
    std::uint32_t no_place = 0;      ///< Years with no candidate left.
  };

  Summary Summarize(const core::WorldState& world) const {
    Summary out{.ordered = ordered_,
                .planted_ever = planted_ever_,
                .grown_ever = grown_ever_,
                .felled_ever_m3 = felled_ever_m3_,
                .refused = refused_,
                .held_back = held_back_,
                .no_place = no_place_};
    for (const core::TimberStandRow& stand : world.stands.rows) {
      if (stand.kind != core::TimberStandKind::kPlanted) {
        continue;
      }
      out.planted += stand.planted_day != core::kNeverPlanted ? 1U : 0U;
      const bool grown =
          stand.matures_day != core::kNeverPlanted && world.calendar.day >= stand.matures_day;
      out.grown += grown ? 1U : 0U;
      out.standing_m3 += stand.stock_m3;
      if (grown && stand.species.value < catalog_.species.size()) {
        out.grown_m3 += stand.planted_area_ha * catalog_.species[stand.species.value].m3_per_ha;
      }
    }
    return out;
  }

  /// @brief The summary, for the run to print.
  void Report(const std::string& run, const core::WorldState& world) const {
    const Summary s = Summarize(world);
    std::cout << run << ": planting — " << s.ordered << " zones ordered; EVER " << s.planted_ever
              << " planted, " << s.grown_ever << " grown, " << std::lround(s.felled_ever_m3)
              << " m3 felled out of them; rows NOW " << s.planted << " planted, " << s.grown
              << " grown (" << std::lround(s.grown_m3) << " m3 at maturity), "
              << std::lround(s.standing_m3) << " m3 standing in plantings; candidates refused "
              << s.refused << ", years held back by a waiting zone " << s.held_back
              << ", years with no place " << s.no_place << "\n";
  }

 private:
  static constexpr std::uint32_t kPlantDayOfYear = 3 * core::kDaysPerMonth;  // 1 April
  static constexpr float kZoneHa = 1.5F;
  static constexpr std::uint32_t kRings = 14;
  static constexpr std::uint32_t kAngles = 16;
  static constexpr float kFirstRingM = 400.0F;
  static constexpr float kRingStepM = 200.0F;
  static constexpr float kUnitClearM = 150.0F;

  /// The cumulative counts, day over day: a row's planting cycle is its
  /// planted_day, counted once when set and once when its matures_day comes;
  /// the timber felled is every fall of a planting row's stock.
  void CountCycles(const core::WorldState& world) {
    for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
      const core::TimberStandRow& stand = world.stands.rows[row];
      const std::uint32_t id = world.stands.row_ids[row].value;
      if (id >= cycles_.size()) {
        cycles_.resize(id + 1U);
      }
      Cycle& seen = cycles_[id];
      if (stand.kind != core::TimberStandKind::kPlanted) {
        seen = Cycle{};
        continue;
      }
      if (stand.planted_day != core::kNeverPlanted && stand.planted_day != seen.planted_day) {
        seen = Cycle{.planted_day = stand.planted_day};
        ++planted_ever_;
      }
      if (!seen.grown && stand.matures_day != core::kNeverPlanted &&
          world.calendar.day >= stand.matures_day) {
        seen.grown = true;
        ++grown_ever_;
      }
      if (seen.grown && stand.stock_m3 < seen.stock_m3) {
        felled_ever_m3_ += seen.stock_m3 - stand.stock_m3;
      }
      seen.stock_m3 = stand.stock_m3;
    }
  }

  /// Stages the candidate `candidate_`; when the list has run out, the year
  /// goes without a zone and says so in the report.
  void TryOne(core::ISimulation& simulation, const core::WorldState& world) {
    core::OrderRow order;
    order.kind = core::OrderKind::kPlantForest;
    order.species = pine_;
    order.area_ha = kZoneHa;
    if (!Candidate(world, order)) {
      ++no_place_;
      done_year_ = world.calendar.day / core::kDaysPerYear;
      return;
    }
    waiting_before_ = UnplantedZones(world);
    simulation.StageOrders(std::span<const core::OrderRow>(&order, 1), {});
    awaiting_ = true;
  }

  /// The `candidate_`-th place within the planters' walk: first the stands
  /// felled to nothing, then the rings. Advances past the out-of-walk ones.
  bool Candidate(const core::WorldState& world, core::OrderRow& order) {
    std::uint32_t index = 0;
    for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
      const core::TimberStandRow& stand = world.stands.rows[row];
      if (!FelledToNothing(world, stand) || WalkHours(world, stand.position) > walk_limit_hours_) {
        continue;
      }
      if (index++ == candidate_) {
        order.stand = world.stands.row_ids[row];
        order.area_ha = std::min(kZoneHa, StandHectares(stand));
        return order.area_ha > 0.0F;
      }
    }
    const core::Vec2 centre = Centre(world);
    for (std::uint32_t ring = 0; ring < kRings; ++ring) {
      const float radius = kFirstRingM + (kRingStepM * static_cast<float>(ring));
      for (std::uint32_t angle = 0; angle < kAngles; ++angle) {
        const float theta = 2.0F * std::numbers::pi_v<float> * static_cast<float>(angle) /
                            static_cast<float>(kAngles);
        const core::Vec2 place{.x = centre.x + (radius * std::cos(theta)),
                               .y = centre.y + (radius * std::sin(theta))};
        const bool on_map = map_side_m_ <= 0.0F || (place.x > 0.0F && place.y > 0.0F &&
                                                    place.x < map_side_m_ && place.y < map_side_m_);
        if (!on_map || WalkHours(world, place) > walk_limit_hours_ || Occupied(world, place)) {
          continue;
        }
        if (index++ == candidate_) {
          order.position = place;
          return true;
        }
      }
    }
    return false;
  }

  /// A grove, belt or grown planting with nothing left, marked or lying —
  /// the ground timber_planting.cpp plants in its own row.
  static bool FelledToNothing(const core::WorldState& world, const core::TimberStandRow& stand) {
    const bool grown_planting = stand.kind == core::TimberStandKind::kPlanted &&
                                stand.matures_day != core::kNeverPlanted &&
                                world.calendar.day >= stand.matures_day;
    const bool fellable = stand.kind == core::TimberStandKind::kGrove ||
                          stand.kind == core::TimberStandKind::kShelterbelt || grown_planting;
    return fellable && !(stand.stock_m3 > 0.0F) && !(stand.marked_m3 > 0.0F) &&
           stand.load_grams <= 0;
  }

  float StandHectares(const core::TimberStandRow& stand) const {
    if (stand.kind == core::TimberStandKind::kPlanted) {
      return stand.planted_area_ha;
    }
    return stand.table_row < catalog_.stands.size() ? catalog_.stands[stand.table_row].area_ha
                                                    : 0.0F;
  }

  /// A coarse first look, so a year of 48 days is not spent one refusal a day
  /// on the fields round the village: a field's or a stand's round contour,
  /// or a unit nearer than kUnitClearM. The CORE still decides (its refusal
  /// is read the next day); this only spares it the obvious.
  bool Occupied(const core::WorldState& world, core::Vec2 place) const {
    const float radius = RadiusOfHectares(kZoneHa);
    for (const core::FieldRow& field : world.fields.rows) {
      if (Distance(place, field.center) < radius + RadiusOfHectares(field.area_ga)) {
        return true;
      }
    }
    for (const core::TimberStandRow& stand : world.stands.rows) {
      if (Distance(place, stand.position) < radius + RadiusOfHectares(StandHectares(stand))) {
        return true;
      }
    }
    for (const core::UnitRow& unit : world.units.rows) {
      if (Distance(place, unit.position) < radius + kUnitClearM) {
        return true;
      }
    }
    return false;
  }

  static float RadiusOfHectares(float hectares) {
    return hectares > 0.0F ? std::sqrt(hectares * 10000.0F / std::numbers::pi_v<float>) : 0.0F;
  }

  static float Distance(core::Vec2 from, core::Vec2 to) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    return std::sqrt((dx * dx) + (dy * dy));
  }

  static std::uint32_t UnplantedZones(const core::WorldState& world) {
    std::uint32_t count = 0;
    for (const core::TimberStandRow& stand : world.stands.rows) {
      count +=
          stand.kind == core::TimberStandKind::kPlanted && stand.planted_day == core::kNeverPlanted
              ? 1U
              : 0U;
    }
    return count;
  }

  /// Game hours on foot, one way, from the nearest lived-in house.
  float WalkHours(const core::WorldState& world, core::Vec2 place) const {
    float best = 1.0e9F;
    for (const core::UnitRow& unit : world.units.rows) {
      if (unit.level == 0 || unit.household.value == core::kInvalidEntityIdValue) {
        continue;
      }
      const float dx_km = (unit.position.x - place.x) / 1000.0F;
      const float dy_km = (unit.position.y - place.y) / 1000.0F;
      best = std::min(best, std::sqrt((dx_km * dx_km) + (dy_km * dy_km)) * walk_hours_per_km_);
    }
    return best;
  }

  /// The village's centre, as the felling reckons it.
  static core::Vec2 Centre(const core::WorldState& world) {
    core::Vec2 sum{.x = 0.0F, .y = 0.0F};
    for (const core::UnitRow& unit : world.units.rows) {
      sum.x += unit.position.x;
      sum.y += unit.position.y;
    }
    const auto count = static_cast<float>(world.units.rows.size());
    return count > 0.0F ? core::Vec2{.x = sum.x / count, .y = sum.y / count} : sum;
  }

  static float Cell(const core::ITableSet& tables,
                    std::string_view table_name,
                    std::string_view key,
                    std::string_view column,
                    float fallback) {
    const core::ITable* const table = tables.FindTable(table_name);
    if (table == nullptr) {
      return fallback;
    }
    const std::optional<float> cell =
        table->CellReal(table->FindRowByKey(key), table->FindColumn(column));
    return cell.has_value() && *cell > 0.0F ? *cell : fallback;
  }

  /// The map's side, metres, from map.csv's `side_m`; 0 when unknown.
  static float MapSide(const core::ITableSet& tables) {
    return Cell(tables, "map", "side_m", "value", 0.0F);
  }

  core::TimberCatalog catalog_;

  core::TreeSpeciesId pine_;

  bool ready_ = false;

  float walk_hours_per_km_ = 2.4F;

  /// Half the accountant's road limit: a zone the planters reach with a
  /// working day left, and not only barely.
  float walk_limit_hours_ = 2.0F;

  float map_side_m_ = 0.0F;

  bool awaiting_ = false;

  std::uint32_t waiting_before_ = 0;

  std::uint32_t candidate_ = 0;

  std::uint32_t done_year_ = 0xFFFFFFFFU;

  std::uint32_t ordered_ = 0;

  std::uint32_t refused_ = 0;

  std::uint32_t held_back_ = 0;

  std::uint32_t no_place_ = 0;

  /// One planting row's cycle as last seen, by the stand's id value.
  struct Cycle {
    std::uint32_t planted_day = core::kNeverPlanted;
    bool grown = false;
    float stock_m3 = 0.0F;
  };

  std::vector<Cycle> cycles_;

  std::uint32_t planted_ever_ = 0;

  std::uint32_t grown_ever_ = 0;

  float felled_ever_m3_ = 0.0F;
};

}  // namespace run

#endif  // TESTS_RUN_COMMON_PLANTING_POLICY_H_
