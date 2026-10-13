/// @file silage_laying.h
/// @brief The autumn's silage laying (livestock design §8 «Закладка: приказ,
///        окно, работа»; contract core-boss-silage-contract-2026-10-13;
///        0.37.248, save 160): the order, the meadows turned for it, the
///        trench sealed and ripe.
///
/// THE WHOLE OF IT, in the order a year meets it:
///   1. THE ORDER (OrderKind::kLaySilage → OrderLaySilage): tonnes of silage
///      wanted of a trench, kept on it as green mass to lay
///      (UnitRow::silage_target). May be given before the window.
///   2. THE MORNING (RunSilageLaying), inside FarmingConfig::
///      silage_from_month..silage_to_month and while the haymaking is not
///      over: while the green mass in the trench and on the heaps bound for it
///      is short of the order, the uncut rest of the meadows NEAREST THE
///      TRENCH is turned to silage (TurnMeadowToSilage) until the grass
///      standing on the turned meadows covers what is short. A meadow whose
///      hay heap is not yet carted waits a day: one heap holds one resource.
///      Once the order is covered by what is in and on the way, the turned
///      meadows' cuts are closed - the rest stands uncut, as the hay's does.
///   3. THE WORK is the meadow's own cut (WorkKind::kHarvest), on the
///      meadow's tier with the laying's window (labor), not stopped by rain
///      (RainStopsFieldWork); the heap is green mass, carted to the trench
///      (field_haul.h, HeapMangerRow) and rotting by its own two days.
///   4. THE EVENING (SettleSilageLaying): past the window, or with the
///      haymaking over, the turned meadows are closed; and a trench whose
///      order is covered or whose window is over, with no cut still turned
///      for it and no heap still bound for it, is SEALED
///      (UnitRow::silage_sealed_day, EventKind::kSilageLaid) and its order
///      cleared.
///   5. RIPE (RunSilageLaying): FarmingConfig::silage_ripen_days after the
///      sealing all the trench's green mass becomes silage at
///      FarmingConfig::silage_per_green, booked as processed / made.
///
/// NOT BUILT, STUBS BY NAME with their return conditions (the design page
/// carries them, boss): grass overgrown inside the window (returns with a
/// window longer than one month); the unsealed trench spoiling; the frost
/// stopping the ripening; the opened trench's faster loss. WITH TWO TRENCHES
/// each order turns its own meadows and each heap goes to its own trench; the
/// outline's area bounds nothing (the order's tonnes are the size).
///
/// @threading SINGLE_THREADED. Production's daily work (phase 4's
/// single-threaded part) and its last tick; orders in the decisions slot.

#ifndef CORE_PRODUCTION_SILAGE_LAYING_H_
#define CORE_PRODUCTION_SILAGE_LAYING_H_

#include <vector>

#include "core_common/calendar.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/unit_state.h"
#include "core_common/world_state.h"
#include "production_config.h"

namespace core {

/// @brief Whether `unit` is a standing silage trench: built, not dead, and
///        the home of green mass (resource_stores.csv).
bool IsSilageTrench(const ProductionConfig& config, const UnitRow& unit);

/// @brief Whether today is inside the laying's window: the month within
///        silage_from_month..silage_to_month.
bool InSilageWindow(const ProductionConfig& config, const WorldState& world);

/// @brief Green mass, grams, lying on meadows' heaps bound for the trench
///        `trench` (FieldRow::silage_trench).
Grams GreenOnTheWay(const ProductionConfig& config, const WorldState& world, UnitId trench);

/// @brief Green mass, grams, the order of the trench at `row` still lacks:
///        its target less what the trench holds and what lies on the heaps
///        bound for it. Never below 0; 0 with no order.
Grams SilageOrderLacks(const ProductionConfig& config, const WorldState& world, std::uint32_t row);

/// @brief Silage a laying sealed or under way is expected to give, and when.
struct SilageLanding {
  SimDay day = 0;    ///< The day it is ripe (absolute).
  Grams silage = 0;  ///< Grams of silage.
};

/// @brief WHAT THE HAY'S FORECAST READS OF THE LAYINGS (herd_forecast.cpp):
///        for a sealed trench - its green mass at the ripening's share, on
///        its ripe day; for a laying under way - the green mass in the trench
///        and on the heaps bound for it, and of the order's unmown rest only
///        what stands on cuts ALREADY TURNED for it (so nothing is promised
///        outside the window, as the hay's cut is promised only inside its
///        months), ripe the ripening's days after the window's last evening.
///        An order standing before the window promises nothing yet.
/// @param[out] landings Appended to; one per trench that has anything coming.
void SilageLandingsAhead(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<SilageLanding>& landings);

/// @brief The laying the hay's lamp may name today (AlarmAdvice::kLaySilage).
struct SilageAdvice {
  UnitId trench;       ///< The trench to order it of; invalid = no advice.
  Grams silage = 0;    ///< What the grass standing uncut would give, grams.
  SimDay ripe_day = 0; ///< The day that silage would be ripe (absolute).
};

/// @brief Whether a laying is a move today, and of how much: the window open
///        or opening within a month and the haymaking not over; a standing
///        trench that is not sealed, holds no green mass and has no order;
///        grass standing uncut on meadows in their cut and not turned. THE
///        TONNES ARE THE LEAST OF THREE: that grass as silage; what the
///        window's days can cart at a day's green carting each
///        (FarmingConfig::silage_cut_ahead_t); and what the herds will eat of
///        silage in a year by their ration's share, less the silage in the
///        stores. Before the window the grass is a ceiling - the hay's scythe
///        may yet take it.
SilageAdvice SilageLayingToAdvise(const ProductionConfig& config, const WorldState& world);

/// @brief Reads a kLaySilage (order_state.h): sets or withdraws the trench's
///        order. Withdrawing closes the cuts turned for it; what is already
///        in the trench or on the way is sealed by the evening as it lies.
/// @return kNone, or the refusal the order's doc lists.
OrderRefusal OrderLaySilage(const ProductionConfig& config,
                            WorldState& current,
                            const OrderRow& order);

/// @brief The morning's half: ripens sealed trenches whose day has come,
///        closes the cuts of orders now covered, and turns meadows for the
///        orders still short. Called once a day after the fields' pass, so
///        the day's lay is on the heaps.
void RunSilageLaying(const ProductionConfig& config, WorldState& current);

/// @brief The evening's half, at the day's last tick after the carting, the
///        rot and the haymaking's closing: drops the trench's name from a
///        meadow whose green heap is gone, closes the turned cuts of a window
///        that is over, and seals the trenches whose laying has ended.
void SettleSilageLaying(const ProductionConfig& config, WorldState& current);

}  // namespace core

#endif  // CORE_PRODUCTION_SILAGE_LAYING_H_
