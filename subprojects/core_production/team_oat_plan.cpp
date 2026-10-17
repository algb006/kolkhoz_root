// The oat lamp's thin year by the field plan (team_oat_plan.h; #1040).

#include "team_oat_plan.h"

#include <cmath>
#include <cstdint>
#include <map>

#include "core_common/day_off.h"
#include "core_common/world_state.h"
#include "herd_system.h"
#include "plan_alarms.h"
#include "production_config.h"
#include "stock_ops.h"

namespace core {

ResourceId TeamWorkFeed(const ProductionConfig& config) {
  if (config.horse_kind.value == kInvalidDefIdValue) {
    return ResourceId{};
  }
  for (const FeedLinkDef& link : config.feed_links) {
    if (link.kind.value == config.horse_kind.value && link.work_only != 0) {
      return link.resource;
    }
  }
  return ResourceId{};
}

Grams TeamWorkFeedGrams(const ProductionConfig& config, float units) {
  const ResourceId feed = TeamWorkFeed(config);
  if (feed.value == kInvalidDefIdValue || feed.value >= config.feed_values.size() ||
      !(config.feed_values[feed.value] > 0.0F) || !(units > 0.0F)) {
    return 0;
  }
  // The feed's own value, not the link's reserve-lowered one: the oat is the
  // horse's staple work feed (feed_links.csv reserve 0), and WatchTheTeam
  // prices the short in the same value.
  return KilogramsToGrams(units / config.feed_values[feed.value]);
}

bool IsFieldWorkDay(const ProductionConfig& config, const WorldState& world) {
  if (IsDayOffIn(world, world.calendar.day)) {
    return false;
  }
  const auto month = static_cast<std::uint8_t>(world.calendar.date.month);
  for (const FieldRow& field : world.fields.rows) {
    const bool preparing = field.phase == FieldPhase::kPlowing ||
                           field.phase == FieldPhase::kHarrowing ||
                           field.phase == FieldPhase::kSowing;
    if (field.kind != LandKind::kArable || !preparing || !(field.work_days_remaining > 0.0F)) {
      continue;
    }
    // THE AUTUMN FURROW is its own season: the field was reaped, and the
    // plough turns the stubble for next spring (field_work.cpp).
    if (field.autumn_furrowing != 0 && field.phase != FieldPhase::kSowing) {
      return true;
    }
    if (field.crop.value >= config.crops.size()) {
      continue;
    }
    const CropDef& crop = config.crops[field.crop.value];
    if (month >= crop.sow_from_month && month <= crop.sow_to_month) {
      return true;
    }
  }
  return false;
}

namespace {

/// The start estimate of a year with no closed request: the kolkhoz horses'
/// work ration at the full share, a day, times the year's working days.
Grams FirstYearTeamNeed(const ProductionConfig& config, const WorldState& current) {
  if (config.horse_kind.value >= config.livestock.size()) {
    return 0;
  }
  const auto month = static_cast<std::uint8_t>(current.calendar.date.month);
  float need_day = 0.0F;
  for (const HerdRow& herd : current.herds.rows) {
    if (herd.household_owned == 0 && herd.kind.value == config.horse_kind.value) {
      need_day += FeedNeedUnits(config, config.livestock[herd.kind.value], herd, month, false);
    }
  }
  return TeamWorkFeedGrams(config,
                           need_day * config.farming.traction_full_ration_share *
                               config.farming.team_need_first_year_working_days);
}

}  // namespace

void ReadTheTeamsOatPlan(const ProductionConfig& config, WorldState& current) {
  TractionWatch& watch = current.traction_watch;
  watch.oat_short_by_plan = 0;
  watch.by_plan_stop_given = 0;
  watch.plan_oat_ha = 0.0F;
  watch.plan_oat_expected = 0;
  watch.plan_team_need = 0;
  const ResourceId oat = TeamWorkFeed(config);
  if (oat.value == kInvalidDefIdValue) {
    return;  // no horse or no work feed in the tables: no lamp to read for
  }
  float yield_kg_per_ha = 0.0F;
  for (const CropDef& crop : config.crops) {
    if (crop.resource.value == oat.value) {
      yield_kg_per_ha = crop.yield_kg_per_ha;
      break;
    }
  }
  watch.plan_oat_ha = PlannedChainHectares(config, current, oat, 0, current.calendar.day);
  watch.plan_oat_expected = KilogramsToGrams(watch.plan_oat_ha * yield_kg_per_ha);
  const Grams requested = current.ledger.current.team_work_oat_requested;
  watch.plan_team_need = requested > 0 ? requested : FirstYearTeamNeed(config, current);
  if (watch.plan_team_need <= 0) {
    return;  // no team, no need: the year cannot be thin for it
  }
  const ResourceAmounts allowance = HerdFeedAllowance(config, current);
  const Grams on_hand = oat.value < allowance.size() ? allowance[oat.value] : 0;
  const double year_oat =
      static_cast<double>(watch.plan_oat_expected) + static_cast<double>(on_hand > 0 ? on_hand : 0);
  const double threshold = static_cast<double>(watch.plan_team_need) *
                           static_cast<double>(config.farming.oat_short_plan_margin);
  watch.oat_short_by_plan = year_oat < threshold ? 1U : 0U;
}

LogisticsTaskId TaskHoldingMostTeams(const WorldState& world) {
  std::map<std::uint32_t, std::uint32_t> carts_by_task;
  if (world.groom_plan.day != world.calendar.day) {
    return LogisticsTaskId{};  // a plan of another day is no plan
  }
  for (const CartPlan& cart : world.groom_plan.carts) {
    if (cart.on_foot) {
      continue;  // no horse in the traces
    }
    // A cart counts once for the first task it carts today.
    for (const CartLeg& leg : cart.legs) {
      if (leg.task.value != kInvalidEntityIdValue) {
        ++carts_by_task[leg.task.value];
        break;
      }
    }
  }
  LogisticsTaskId best;
  std::uint32_t most = 0;
  for (const auto& [task, carts] : carts_by_task) {
    if (carts > most) {
      most = carts;
      best = LogisticsTaskId{.value = task};
    }
  }
  return best;
}

}  // namespace core
