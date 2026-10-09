// Felling (timber_felling.h).

#include "timber_felling.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core_catalog/timber_catalog.h"
#include "core_common/calendar.h"
#include "core_common/emit_event.h"
#include "core_common/event_state.h"
#include "core_common/home_reach.h"
#include "core_common/state_table_ops.h"
#include "core_common/timber_state.h"
#include "timber_planting.h"

namespace core {
namespace {

/// The stand's catalogue row, or nullptr for a stand whose table row the
/// build does not carry (a save from a different bake).
const TimberStandDef* DefOf(const ProductionConfig& config, const TimberStandRow& stand) {
  return stand.table_row < config.timber.stands.size() ? &config.timber.stands[stand.table_row]
                                                       : nullptr;
}

/// The adult horses of the kolkhoz, the pool a ride is given from — the same
/// count as labor's (labor_system.cpp, DraughtHorses) and the sowing lamp's
/// (production_alarms.cpp, DraughtTeam).
std::uint32_t DraughtHorsesOf(const ProductionConfig& config, const WorldState& world) {
  std::uint32_t horses = 0;
  for (const HerdRow& herd : world.herds.rows) {
    if (config.horse_kind.value != kInvalidDefIdValue &&
        herd.kind.value == config.horse_kind.value) {
      horses += herd.adult_count;
    }
  }
  return horses;
}

}  // namespace

OrderRefusal MarkFelling(const ProductionConfig& config,
                         WorldState& current,
                         const OrderRow& order) {
  const std::uint32_t row = FindRow(current.stands, order.stand);
  if (row == kNoRow) {
    return OrderRefusal::kNoSuchSubject;
  }
  // AS MANY FELLINGS AT ONCE AS THE CHAIRMAN MARKS (the human's word of
  // 2026-09-14, "убрать и здесь"; time design §11, 056f531). Until then a
  // second felling was refused while timber stood marked anywhere, after
  // "новую порубку назначить нельзя, пока идёт эта". One mark a stand still
  // holds: a stand's marked volume is one number.
  TimberStandRow& stand = current.stands.rows[row];
  if (stand.marked_m3 > 0.0F) {
    return OrderRefusal::kConflictsWithActive;
  }
  const float unmarked = stand.stock_m3 - stand.marked_m3;
  if (!(order.volume_m3 > 0.0F) || order.volume_m3 > unmarked) {
    return OrderRefusal::kRuleForbids;
  }
  stand.marked_m3 = order.volume_m3;
  stand.work_days_remaining = order.volume_m3 * config.timber.felling_days_per_m3;
  return OrderRefusal::kNone;
}

void FellFinishedStands(const ProductionConfig& config, WorldState& current) {
  for (TimberStandRow& stand : current.stands.rows) {
    if (!(stand.marked_m3 > 0.0F) || stand.work_days_remaining > 0.0F) {
      continue;
    }
    const float felled = std::min(stand.marked_m3, stand.stock_m3);
    // A PLANTING has no table row: its hectares and its species' log share
    // stand in (timber_planting.h, save 82). A grove replanted keeps its old
    // table row, so the kind is asked first.
    TimberStandDef planted;
    const TimberStandDef* const def =
        PlantedStandDef(config, stand, planted)
            ? &planted
            : (stand.kind == TimberStandKind::kPlanted ? nullptr : DefOf(config, stand));
    if (def != nullptr) {
      stand.load_grams += LogGramsFromVolume(config.timber, *def, felled);
    }
    // The firewood the same trees give has no holder yet (STUB, timber
    // design §8a): it is felled and not laid down.
    stand.stock_m3 = std::max(stand.stock_m3 - felled, 0.0F);
    stand.marked_m3 = 0.0F;
    stand.work_days_remaining = 0.0F;
  }
}

void ReleaseUnreachableMarks(const ProductionConfig& config, WorldState& current) {
  const auto limit = static_cast<std::uint32_t>(config.timber.mark_release_days);
  const ReachRule rule{.walk_speed_kmh = config.walk_speed_kmh,
                       .ride_speed_kmh = config.harness_speed_kmh,
                       .travel_limit_hours = config.travel_limit_hours,
                       .min_usable_hours = config.min_usable_hours};
  const std::uint32_t horses = DraughtHorsesOf(config, current);
  for (std::uint32_t row = 0; row < current.stands.rows.size(); ++row) {
    TimberStandRow& stand = current.stands.rows[row];
    const bool marked = stand.marked_m3 > 0.0F && stand.work_days_remaining > 0.0F;
    if (!marked || FellingCanBeMannedToday(current, stand.position, rule, horses)) {
      stand.unreached_days = 0;
      continue;
    }
    ++stand.unreached_days;
    if (limit == 0 || stand.unreached_days < limit) {
      continue;
    }
    // WHAT WAS FELLED STAYS FELLED: the crew's share of the mark, by the work
    // done of the work written, leaves the stock and lies as logs — exactly
    // as a finished felling's does (FellFinishedStands). The rest was never
    // cut and is the stand's stock again, unmarked.
    const float written = stand.marked_m3 * config.timber.felling_days_per_m3;
    const float done_share =
        written > 0.0F ? std::clamp(1.0F - (stand.work_days_remaining / written), 0.0F, 1.0F)
                       : 0.0F;
    const float felled = std::min(stand.marked_m3 * done_share, stand.stock_m3);
    const float unfelled = stand.marked_m3 - felled;
    TimberStandDef planted;
    const TimberStandDef* const def =
        PlantedStandDef(config, stand, planted)
            ? &planted
            : (stand.kind == TimberStandKind::kPlanted ? nullptr : DefOf(config, stand));
    if (def != nullptr && felled > 0.0F) {
      stand.load_grams += LogGramsFromVolume(config.timber, *def, felled);
    }
    stand.stock_m3 = std::max(stand.stock_m3 - felled, 0.0F);
    stand.marked_m3 = 0.0F;
    stand.work_days_remaining = 0.0F;
    stand.unreached_days = 0;
    constexpr float kLitresPerM3 = 1000.0F;
    SimEvent& event = EmitEvent(current, EventKind::kFellingMarkReleased, EventSeverity::kNotable);
    event.stand = current.stands.row_ids[row];
    event.amount = static_cast<std::int64_t>(std::lround(unfelled * kLitresPerM3));
  }
}

void GrowOldForest(const ProductionConfig& config, WorldState& current) {
  for (TimberStandRow& stand : current.stands.rows) {
    if (stand.kind != TimberStandKind::kForestOld) {
      continue;
    }
    const TimberStandDef* const def = DefOf(config, stand);
    if (def == nullptr) {
      continue;
    }
    // A CEILING, NOT A LEDGER OF YEARS. Keeping each year's trunks apart to
    // let the oldest vanish would be a list per stand for a rule the design
    // states in one line; at a steady fall the stock simply never exceeds
    // what that many years drop, and a felling below the ceiling makes room
    // for next year's. What is marked is never taken back by the ceiling.
    const float ceiling = std::max(OldForestCeilingM3(config.timber, *def), stand.marked_m3);
    stand.stock_m3 = std::min(stand.stock_m3 + YearlyOldTrunksM3(config.timber, *def), ceiling);
  }
}

void CollectTimberAlarms(const ProductionConfig& config,
                         const WorldState& world,
                         std::vector<Alarm>& alarms) {
  for (std::uint32_t row = 0; row < world.stands.rows.size(); ++row) {
    const TimberStandRow& stand = world.stands.rows[row];
    // THE FELLING RIDES, THE PLANTING WALKS (labor_state.h, RidesOut): the
    // planters carry spades and saplings, not logs, and go on foot. A zone
    // beyond the walking road got its job and nobody to take it, and the
    // chairman was not told (named at 0.34.35; boss seq 21: «да»).
    const bool felling = stand.marked_m3 > 0.0F && stand.work_days_remaining > 0.0F;
    const bool planting = stand.kind == TimberStandKind::kPlanted &&
                          stand.planted_day == kNeverPlanted && stand.work_days_remaining > 0.0F;
    if (!felling && !planting) {
      continue;  // nothing asked of anybody here
    }
    // A FELLING IS ASKED BY THE LOG CART'S ROAD, not the team's (0.36.29;
    // boss [69]): the logs go out by it, and the accountant offers no felling
    // the log cart cannot reach (labor_system.cpp). Off the roads the cart
    // weighs 2.5 to the team's 1.5 (road_route.h), so its road is the longer
    // and covers the brigade's own ride too.
    const float speed = felling ? config.harness_speed_kmh : config.walk_speed_kmh;
    float road = NearestHomeTravelHours(
        world, stand.position, speed, felling ? TravelMode::kLogCart : TravelMode::kWalk);
    if (road < 0.0F) {
      continue;  // nobody lives anywhere: every alarm of the village says so already
    }
    // THE ACCOUNTANT'S QUESTION, as kSiteUnreachable asks it (construction):
    // too long a road for him, or too little of the day left after it. Of a
    // felling it is asked of the LOG CART; of a planting nothing is carried
    // out, and the hands' own road below is the whole of it.
    const bool too_long = road > config.travel_limit_hours;
    const bool no_day_left = world.weather.daylight_hours - (2.0F * road) < config.min_usable_hours;
    const bool cart_fails = felling && (too_long || no_day_left);
    // AND WHETHER A HAND CAN BE SENT THERE AT ALL (0.37.208; home_reach.h,
    // HandReachToday — the offering's and the placement's own question): on
    // foot, or by a ride while the kolkhoz has a horse. Until then a felling
    // was asked of the log cart alone, while its fellers walk, and a planting
    // of the walk alone, while a planter past the walk rides (labor_state.h,
    // TakesThePeoplesCart): the first lamp stayed dark over a stand no hand
    // was ever sent to, the second burned over a zone the cart served.
    const ReachRule rule{.walk_speed_kmh = config.walk_speed_kmh,
                         .ride_speed_kmh = config.harness_speed_kmh,
                         .travel_limit_hours = config.travel_limit_hours,
                         .min_usable_hours = config.min_usable_hours};
    const HandReach reach = HandReachToday(world, stand.position, rule);
    const bool no_hand = reach == HandReach::kNone ||
                         (reach == HandReach::kByRide && DraughtHorsesOf(config, world) == 0);
    if (!cart_fails && no_hand && felling) {
      // The hours named are then the hands' ride, not the log cart's.
      road = NearestHomeTravelHours(
          world, stand.position, config.harness_speed_kmh, TravelMode::kTeam);
    }
    if (cart_fails || no_hand) {
      Alarm alarm;
      alarm.kind = felling ? AlarmKind::kFellingUnreachable : AlarmKind::kPlantingUnreachable;
      alarm.stand = world.stands.row_ids[row];
      alarm.amount = static_cast<std::int64_t>(road);
      alarms.push_back(alarm);
    }
  }
}

}  // namespace core
