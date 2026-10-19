// A rick's supply from another rick (rick_supply.h; #1056 C1.2).

#include "rick_supply.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cart_trips.h"
#include "core_common/day_off.h"
#include "core_common/haul.h"
#include "core_common/hauled_book.h"
#include "core_common/ids.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table_ops.h"
#include "core_common/work_seam.h"
#include "feed_home.h"
#include "haul_ways.h"
#include "herd_forecast.h"
#include "stock_ops.h"

namespace core {
namespace {

constexpr float kGramsPerKg = static_cast<float>(kGramsPerKilogram);

/// What each unit row may give another rick, kg; below nought - not a
/// sender at all. A rick with herds bound to it keeps their hay for `keep`
/// days (gives what is left after them, nothing when they run short); a rick
/// no herd is bound to gives all it holds.
std::vector<float> SpareOfEveryRick(const ProductionConfig& config,
                                    const WorldState& world,
                                    std::uint32_t keep) {
  std::vector<float> spare(world.units.rows.size(), -1.0F);
  for (std::uint32_t row = 0; row < world.units.rows.size(); ++row) {
    const UnitRow& unit = world.units.rows[row];
    if (IsRickToEatFrom(unit, config) && unit.emptying == 0) {
      spare[row] = static_cast<float>(UnreservedOf(unit, config.hay_resource)) / kGramsPerKg;
    }
  }
  for (const RickHayAhead& bound : RickHayWithin(config, world, keep)) {
    if (spare[bound.rick_row] >= 0.0F) {
      spare[bound.rick_row] = bound.short_day < keep ? 0.0F : bound.left_kg;
    }
  }
  return spare;
}

/// The days until the next cut can have its hay in the ricks - the hay
/// lamp's own horizon (ForecastHerdFeed, kNearestScythes: to the nearest
/// first scythes and hay_new_cut_in_store_days into them), so the lamp and
/// the supply read one «cut». kNoCut when nothing is mown to refill a rick:
/// no meadow and no fallow mown as it lies (econ's case «a rick no meadow
/// stack feeds keeps its full windows»). Every rick the cut reaches is
/// refilled by it: a meadow's stack goes to the rick with the largest gap
/// (RickForLoadFrom), so the village has one answer, not one a rick.
constexpr std::uint32_t kNoCut = 0xFFFFFFFFU;

std::uint32_t DaysToTheRefillingCut(const ProductionConfig& config, const WorldState& world) {
  const bool mown = std::ranges::any_of(world.fields.rows, [](const FieldRow& field) {
    return field.kind == LandKind::kMeadow || field.mown_fallow != 0;
  });
  if (!mown) {
    return kNoCut;
  }
  const HerdFeedForecast forecast =
      ForecastHerdFeed(config, world, false, FeedHorizon::kNearestScythes);
  // A forecast with no herds to feed says no horizon (0): no cut to stop at.
  return forecast.horizon_days > 0 ? forecast.horizon_days : kNoCut;
}

/// The row with the most to spare but `receiver`; kNoRow when none spares
/// anything. Ties by the first row.
std::uint32_t RichestSender(const std::vector<float>& spare, std::uint32_t receiver) {
  std::uint32_t best = kNoRow;
  for (std::uint32_t row = 0; row < spare.size(); ++row) {
    if (row != receiver && spare[row] > 0.0F && (best == kNoRow || spare[row] > spare[best])) {
      best = row;
    }
  }
  return best;
}

}  // namespace

void PriceRickSupply(const ProductionConfig& config, WorldState& current) {
  for (UnitRow& unit : current.units.rows) {
    unit.rick_supply = RickSupplySeam{};
  }
  if (config.farming.rick_supply < 0.5F || config.hay_resource.value == kInvalidDefIdValue) {
    return;
  }
  const std::uint32_t threshold =
      UrgentHayHorizonDays(current, static_cast<std::uint32_t>(config.hay_cart_urgent_days)) +
      static_cast<std::uint32_t>(config.farming.rick_threshold_add_days);
  if (threshold == 0) {
    return;
  }
  // BOTH WINDOWS END AT THE CUT THAT REFILLS THE RICKS (#1096; econ
  // rick-window-to-the-cut-2026-10-19, boss 19 October). Until 0.37.303 the
  // shortfall read `threshold` days and the senders' spare twice that, both
  // past a cut 4-9 days away: on the canon's 66 dark-lamp days a covered
  // village priced a rick short, and the other ricks' hay, kept for their own
  // herds past the cut, read «no spare».
  const std::uint32_t cut = DaysToTheRefillingCut(config, current);
  const std::uint32_t short_window = std::min(threshold, cut);
  const std::uint32_t spare_window = std::min(2U * threshold, cut);
  if (short_window == 0) {
    return;  // the new hay is in store today: nothing to move before it
  }
  std::vector<RickHayAhead> below = RickHayWithin(config, current, short_window);
  std::erase_if(below, [short_window](const RickHayAhead& rick) {
    return rick.short_day >= short_window || !(rick.missing_kg > 0.0F);
  });
  if (below.empty()) {
    return;
  }
  std::ranges::sort(below, [](const RickHayAhead& left, const RickHayAhead& right) {
    return left.short_day != right.short_day ? left.short_day < right.short_day
                                             : left.rick_row < right.rick_row;
  });
  std::vector<float> spare = SpareOfEveryRick(config, current, spare_window);
  const float load_kg =
      static_cast<float>(CartLoadGrams(config.porter,
                                       CartGearToday(current, CartSpecsOf(config)).load_kg,
                                       config.hay_resource)) /
      kGramsPerKg;
  for (const RickHayAhead& rick : below) {
    UnitRow& receiver = current.units.rows[rick.rick_row];
    if (receiver.emptying != 0 || AnythingAwaits(receiver.construction)) {
      continue;  // one hauling work names one unit (unit_state.h, RickSupplyStands)
    }
    const std::uint32_t sender_row = RichestSender(spare, rick.rick_row);
    if (sender_row == kNoRow) {
      continue;
    }
    const float kg = std::min(std::max(rick.missing_kg, load_kg), spare[sender_row]);
    spare[sender_row] -= kg;
    const UnitRow& sender = current.units.rows[sender_row];
    const HaulRate rate = RateToward(config,
                                     current,
                                     sender.position,
                                     receiver.position,
                                     TravelMode::kCart,
                                     config.hay_resource);
    RickSupplySeam& seam = receiver.rick_supply;
    seam.from = current.units.row_ids[sender_row];
    seam.resource = config.hay_resource;
    seam.grams = GramsFromFloat(kg * kGramsPerKg);
    seam.days_remaining = CartDaysFor(config, seam.grams, rate);
    seam.days_written = seam.days_remaining;
    seam.threshold_days = static_cast<std::uint8_t>(std::min(short_window, 255U));
  }
}

void SettleRickSupply(const ProductionConfig& config, WorldState& current) {
  for (std::uint32_t row = 0; row < current.units.rows.size(); ++row) {
    UnitRow& receiver = current.units.rows[row];
    RickSupplySeam& seam = receiver.rick_supply;
    if (!RickSupplyStands(receiver)) {
      continue;
    }
    const std::uint32_t sender_row = FindRow(current.units, seam.from);
    if (sender_row == kNoRow) {
      seam = RickSupplySeam{};
      continue;
    }
    UnitRow& sender = current.units.rows[sender_row];
    const float done =
        seam.days_written > seam.days_remaining ? seam.days_written - seam.days_remaining : 0.0F;
    float converted = 1.0F;
    if (done > 0.0F && seam.days_written > 0.0F && seam.grams > 0) {
      const float share = std::min(done / seam.days_written, 1.0F);
      const Grams budget = GramsFromFloat(static_cast<float>(seam.grams) * share);
      const Grams offered = std::min(budget, UnreservedOf(sender, seam.resource));
      const Grams taken = -AddToStock(sender.stock, seam.resource, -offered);
      // A rick has no tonnage to be full against (field_haul.h): it takes all.
      const Grams moved = AddToStock(receiver.stock, seam.resource, taken);
      seam.grams = std::max<Grams>(seam.grams - moved, 0);
      const HaulRate rate = RateToward(
          config, current, sender.position, receiver.position, TravelMode::kCart, seam.resource);
      BookCartRun(config, current, CartLoadSource::kStore, rate, moved);
      // Who carried it (#671's instrument; hauled_book.h).
      WorkAssignment hauled;
      hauled.kind = WorkKind::kHauling;
      hauled.unit = current.units.row_ids[row];
      BookHauled(current.ledger.current,
                 seam.resource,
                 moved,
                 FootShareOf(current, hauled, SettlementHasCarts(current, config.horse_kind)));
      if (budget > 0) {
        converted = std::min(static_cast<float>(moved) / static_cast<float>(budget), 1.0F);
      }
      SpendHaulNotes(current, hauled, converted);
    }
    // What was drained and moved is settled; what the sender could not give
    // stays drained for the next hour (as the milk leg keeps it).
    seam.days_written = seam.days_remaining + (done * (1.0F - converted));
    if (seam.grams == 0) {
      seam.days_remaining = 0.0F;
      seam.days_written = 0.0F;
    }
  }
}

}  // namespace core
