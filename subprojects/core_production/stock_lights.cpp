// The feed and seed lights (stock_lights.h).

#include "stock_lights.h"

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "herd_system.h"
#include "stock_ops.h"

namespace core {
namespace {

/// The month a wintering forecast asks about: the first one outside the
/// pasture band. Taken from the band rather than written down, so moving the
/// season moves this with it.
///
/// A band covering the WHOLE year has no outside, and then the winter month
/// is a fiction; the parser bounds the two months independently and never
/// checks their order, so that config is legal. The caller must ask whether
/// there is a stall season at all before believing this.
std::uint8_t WinterMonth(const ProductionConfig& config) {
  return static_cast<std::uint8_t>((config.farming.pasture_to_month + 1U) % kMonthsPerYear);
}

/// Is there a stall season at all? A band that runs the whole year means the
/// herd never comes in, and a wintering forecast has nothing to forecast.
/// Days from today to the start of the band [from, to], 0 if today is
/// already inside it. One place, because all four dates are this same
/// question asked of a different band.
///
/// A band with from > to is not a band this module can read — MonthInRange,
/// which the real feeding uses, does not wrap the year either — and the
/// answer is 0 rather than the horizon. Returning the horizon would pin the
/// light yellow for ever with nothing to show for it, and a light that is
/// always yellow is the failure mode the whole mechanism is built to avoid.
/// With 0 the colour rests on the stock alone, which is at least true.
std::int32_t DaysToWindow(const WorldState& world, std::uint8_t from, std::uint8_t to) {
  const auto today = static_cast<std::uint8_t>(world.calendar.date.month);
  if (from > to || MonthInRange(today, from, to)) {
    return 0;
  }
  std::int32_t months = static_cast<std::int32_t>(from) - static_cast<std::int32_t>(today);
  if (months < 0) {
    months += static_cast<std::int32_t>(kMonthsPerYear);
  }
  return months * static_cast<std::int32_t>(kDaysPerMonth);
}

/// The month the nearest sowing campaign opens, or kMonthsPerYear when no
/// crop names a window at all. "Nearest" is by the calendar and forward
/// only: a campaign whose window is open right now is the nearest one.
std::uint8_t NearestSowingMonth(const ProductionConfig& config, const WorldState& world) {
  std::uint8_t best = static_cast<std::uint8_t>(kMonthsPerYear);
  std::int32_t best_days = kStockForecastHorizonDays + 1;
  for (const CropDef& crop : config.crops) {
    if (!(crop.sowing_norm_kg_per_ha > 0.0F) || crop.sow_from_month >= kMonthsPerYear) {
      continue;
    }
    const std::int32_t days = DaysToWindow(world, crop.sow_from_month, crop.sow_to_month);
    // Ties go to the lower month so the answer is a function of the tables
    // and not of their row order.
    if (days < best_days || (days == best_days && crop.sow_from_month < best)) {
      best_days = days;
      best = crop.sow_from_month;
    }
  }
  return best;
}

bool HasStallSeason(const ProductionConfig& config) {
  return !MonthInRange(
      WinterMonth(config), config.farming.pasture_from_month, config.farming.pasture_to_month);
}

}  // namespace

std::int32_t DaysToHarvest(const ProductionConfig& config, const WorldState& world) {
  std::int32_t best = kStockForecastHorizonDays;
  for (const CropDef& crop : config.crops) {
    const std::int32_t days = DaysToWindow(world, crop.harvest_from_month, crop.harvest_to_month);
    best = days < best ? days : best;
  }
  return best;
}

std::int32_t DaysToPasture(const ProductionConfig& config, const WorldState& world) {
  return DaysToWindow(world, config.farming.pasture_from_month, config.farming.pasture_to_month);
}

std::int32_t DaysToSowing(const ProductionConfig& config, const WorldState& world) {
  std::int32_t best = kStockForecastHorizonDays;
  for (const CropDef& crop : config.crops) {
    const std::int32_t days = DaysToWindow(world, crop.sow_from_month, crop.sow_to_month);
    best = days < best ? days : best;
  }
  return best;
}

StockForecast FeedLight(const ProductionConfig& config, const WorldState& world) {
  StockForecast light;
  light.kind = StockKind::kFeed;
  light.days_to_date = DaysToPasture(config, world);

  // The herds this forecast is about, with the winter need of each. PER HERD
  // and not summed, because the real feeding is per herd: every ceiling
  // (FeedLinkDef::max_share) is a share of ONE herd's day, and every feeding
  // order is that herd's kind's order. Summing first and draining after let
  // the pigs' barley cover the cows' hay and turned every ceiling into a
  // share of the whole settlement — which is to say, into nothing. Found by
  // the delivery cycle, and the first tests could not see it because each
  // used a single kind with a single feed.
  struct HerdNeed {
    LivestockKindId kind;
    float units_per_day = 0.0F;
  };

  std::vector<HerdNeed> needs;
  if (HasStallSeason(config)) {
    const std::uint8_t winter = WinterMonth(config);
    for (const HerdRow& herd : world.herds.rows) {
      if (herd.household_owned != 0 || herd.kind.value >= config.livestock.size()) {
        continue;  // a family herd eats from its family's pantry, not the farm's
      }
      const float need = FeedNeedUnits(config, config.livestock[herd.kind.value], herd, winter);
      if (need > 0.0F) {
        needs.push_back(HerdNeed{.kind = herd.kind, .units_per_day = need});
      }
    }
  }
  if (needs.empty()) {
    // No kolkhoz herd, no roster, or a pasture band with no winter in it:
    // nothing eats from the stores, so nothing runs out. An answer, and
    // pointedly not "no data", which belongs to a stock nobody can forecast.
    light.days_of_stock = kStockNeverRunsOut;
    light.light = LightFrom(light.days_of_stock, light.days_to_date, 0, false);
    return light;
  }

  // A copy of the stores in KILOGRAMS per feed resource, drained day by day.
  std::vector<float> held_kg(config.feed_values.size(), 0.0F);
  for (std::uint32_t resource = 0; resource < held_kg.size(); ++resource) {
    if (!(config.feed_values[resource] > 0.0F)) {
      continue;
    }
    const ResourceId id{static_cast<std::uint16_t>(resource)};
    held_kg[resource] =
        static_cast<float>(HeldEverywhere(world, id)) / static_cast<float>(kGramsPerKilogram);
  }

  std::int32_t days = 0;
  bool starving_today = false;
  while (days < kStockForecastHorizonDays) {
    bool all_fed = true;
    for (const HerdNeed& herd : needs) {
      float covered = 0.0F;
      for (const FeedLinkDef& link : config.feed_links) {
        if (covered >= herd.units_per_day) {
          break;
        }
        // The kind filter is the whole point: a feeding order belongs to the
        // kind it names, and a cow is never offered the pigs' barley.
        if (link.kind.value != herd.kind.value || link.resource.value >= held_kg.size()) {
          continue;
        }
        // Work-only feeds are the horse's WAGE, not its keep (boss, Q2): in a
        // wintering forecast nobody is in the traces, so they carry nothing.
        // Leaving them in would let the oats forecast the hay.
        if (link.work_only != 0) {
          continue;
        }
        const float value = config.feed_values[link.resource.value] *
                            (link.reserve != 0 ? config.farming.reserve_feed_factor : 1.0F);
        if (!(value > 0.0F)) {
          continue;
        }
        const float ceiling = herd.units_per_day * link.max_share;
        float take_units = herd.units_per_day - covered;
        take_units = take_units < ceiling ? take_units : ceiling;
        const float available_units = held_kg[link.resource.value] * value;
        take_units = take_units < available_units ? take_units : available_units;
        if (!(take_units > 0.0F)) {
          continue;
        }
        held_kg[link.resource.value] -= take_units / value;
        covered += take_units;
      }
      if (covered + 0.001F < herd.units_per_day) {
        all_fed = false;
        break;
      }
    }
    if (!all_fed) {
      if (days == 0) {
        starving_today = true;  // the stores cannot feed the herds TODAY
      }
      break;
    }
    ++days;
  }
  light.days_of_stock = days;
  light.light = LightFrom(days,
                          light.days_to_date,
                          static_cast<std::int32_t>(config.farming.feed_light_margin_days),
                          starving_today);
  return light;
}

StockForecast SeedLight(const ProductionConfig& config, const WorldState& world) {
  StockForecast light;
  light.kind = StockKind::kSeed;
  light.measure = StockMeasure::kCoverage;

  // THE NEXT SOWING IS A CAMPAIGN ON THE CALENDAR, not an index in the
  // rotation (boss, 2026-09-04). In autumn the nearest campaign is the
  // winter one and the winter crops are counted; once the winter crop is in
  // the ground the nearest campaign is the spring one, and asking about
  // winter seed then is meaningless — it is already sown. The light used to
  // read rotation_year0, which is THIS year's crop and only shifts at the
  // year's turn, so a spring field was charged its seed a second time for a
  // third of the year and the light stood amber for no reason a player
  // could see.
  const std::uint8_t campaign_month = NearestSowingMonth(config, world);
  if (campaign_month >= kMonthsPerYear) {
    // No crop names a sowing window: nothing is ever sown, so nothing can be
    // short of seed.
    light.days_to_date = 0;
    light.coverage = 1.0F;
    light.light = StockLight::kGreen;
    return light;
  }
  light.days_to_date = DaysToWindow(world, campaign_month, campaign_month);

  // What the campaign's fields will want, by RESOURCE — two crops can share
  // one (winter wheat and spring wheat are both `wheat`), and counting per
  // crop would count the same grain twice.
  std::vector<float> need_kg(config.feed_values.size(), 0.0F);
  for (const FieldRow& field : world.fields.rows) {
    if (field.kind != LandKind::kArable || field.rotation_year0.value >= config.crops.size()) {
      continue;
    }
    // A FIELD THAT IS OCCUPIED IS NOT IN THIS CAMPAIGN. Standing corn is not
    // waiting to be sown, whatever the rotation says about it.
    if (field.phase == FieldPhase::kGrowing || field.phase == FieldPhase::kHarvest) {
      continue;
    }
    const CropDef& crop = config.crops[field.rotation_year0.value];
    if (crop.sow_from_month != campaign_month || !(crop.sowing_norm_kg_per_ha > 0.0F)) {
      continue;  // this field is sown in some other campaign
    }
    if (crop.resource.value < need_kg.size()) {
      need_kg[crop.resource.value] += crop.sowing_norm_kg_per_ha * field.area_ga;
    }
  }

  // THE TIGHTEST CROP DECIDES: you cannot sow oats with rye, and a village
  // short of one seed is short however much of another it has. Netting them
  // let a mountain of rye cancel the absence of potato seed and read green.
  float coverage = -1.0F;
  for (std::uint32_t resource = 0; resource < need_kg.size(); ++resource) {
    if (!(need_kg[resource] > 0.0F)) {
      continue;
    }
    const ResourceId id{static_cast<std::uint16_t>(resource)};
    const float have_kg =
        static_cast<float>(HeldEverywhere(world, id)) / static_cast<float>(kGramsPerKilogram);
    const float share = have_kg / need_kg[resource];
    coverage = coverage < 0.0F || share < coverage ? share : coverage;
  }
  if (coverage < 0.0F) {
    // The campaign asks for nothing: no free field is due this crop.
    light.coverage = 1.0F;
    light.light = StockLight::kGreen;
    return light;
  }
  light.coverage = coverage;
  light.light = LightFromCoverage(coverage, config.farming.seed_light_margin_share);
  return light;
}

}  // namespace core
