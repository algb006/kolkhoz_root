/// @file
/// @brief The stock traffic light: four lights, each a forecast of whether a
/// stock reaches the date it has to reach.
/// @threading PARALLEL_READONLY
/// Plain data with no state of its own. Forecasts are NOT part of WorldState:
/// like alarms (alarm_state.h) they are DERIVED from a completed state
/// between steps, by the subsystems whose rules they are, and handed to the
/// presentation by the session. Nothing in the simulation reads them, nothing
/// saves them, and a loaded world stands its lights again the moment the
/// session asks.
///
/// AN ALARM IS A FACT, A LIGHT IS A FORECAST (office design §5). When the
/// alarm "the herd is underfed" stands, the winter is already here; the
/// yellow light comes on months before it and means one thing: go into the
/// farm and sort it out. That is the whole reason the light exists — the
/// player who has never wintered here has no habit of reading summaries, and
/// this is the only thing that warns him in time.
///
/// ONE NUMBER, TWO READERS. The panel compares the forecast with its date
/// and paints a colour; the story reads the number itself and can set a
/// quest while the barn is still full (boss, 2026-09-04). They are not two
/// calculations that must be kept in step — there is one, and both read it.
/// A second calculation would let the player see a quest saying the feed
/// runs out in February beside a green light.
///
/// WHO COMPUTES. The same law as alarms: a rule lives with its
/// configuration, so the subsystem that owns the consumption owns the light.
/// Food is core_residents (it holds the eating norms), feed and seed are
/// core_production (feed rates, the pasture season, sowing norms). The
/// boundary computes none of it and holds no threshold.
///
/// THE FORECAST IS HONEST AND DULL, and that is a requirement, not a
/// limitation (office design §5): from the stock that is there and the
/// consumption that is known, to the named date. It does not guess what the
/// player will do — no new fields, no purchase, no die-off.

#ifndef CORE_COMMON_STOCK_FORECAST_H_
#define CORE_COMMON_STOCK_FORECAST_H_

#include <cstdint>

namespace core {

/// @brief The four lights. A light is given to a DEADLY shortage, not to an
/// important one: the lack must ruin rather than inconvenience, must take a
/// season or years to repair, and must have a date (office design §5). That
/// is why there is no light for building materials — their lack stops work,
/// but the farm does not die of it and money mends it in a month.
///
/// FOUR, AND A FIFTH IS A HUMAN'S DECISION. Otherwise there are twenty in a
/// year and the top of the screen is a resource bar again, only in colour.
enum class StockKind : std::uint8_t {
  /// PEOPLE. Hunger is death and flight from the village. Everything edible
  /// summed into eater-days: nothing is split inside a light, because the
  /// trouble is one and the way into the farm is one.
  kFood = 0,

  /// THE HERD. A die-off, and a herd takes years to grow back. Fodder-days
  /// against the WINTER ration: counting "what they eat today" in summer is
  /// meaningless — the herd is out at pasture, the light would stand green
  /// until November and yellow when the hay can no longer be cut. It is
  /// most useful in haymaking, which is precisely when it must be right.
  kFeed,

  /// THE WINTER. An unheated house is illness and death.
  kFirewood,

  /// THE WHOLE NEXT YEAR. Eat the seed fund and there is nothing to sow.
  /// Food and seed are the same grain and still two lights: the start's
  /// most expensive mistake shows nothing until sowing, and by then it
  /// costs a year. A light belongs exactly where the reckoning is deferred.
  kSeed,

  /// Not a light: the number of them, for a consumer's mirror.
  kStockKindCount,
};

/// @brief What one light says.
enum class StockLight : std::uint8_t {
  /// Enough with room to spare. Nothing to do.
  kGreen = 0,

  /// It will not last to the date at the current rate, or only just. An
  /// invitation to look into it, not a disaster. A light that is yellow
  /// always is noise and stops being seen within a week — if that happens
  /// the thresholds are wrong, not the player.
  kYellow,

  /// Already short. The matching alarm stands beside it and says the same
  /// thing after the fact.
  kRed,

  /// THE QUESTION IS FAIR AND THERE IS NOTHING TO ANSWER IT WITH — no
  /// consumption rate exists for this stock yet. NOT a colour, and above all
  /// not green: a light that is green because nothing could be computed
  /// teaches the player to trust it, and lies exactly once, in the first
  /// winter the light was put there for.
  ///
  /// Distinct from the two other refusals the core can give (boss's
  /// summary, 2026-09-04): "never" is an answer that may change tomorrow,
  /// "not applicable" is a question that does not exist, and this is a
  /// question that exists and cannot be answered yet. A reader shows
  /// nothing for it — an empty field takes no default when the default
  /// reads as well-being.
  kNoData,
};

/// @brief Why a light says nothing. Meaningful only with StockLight::kNoData.
///
/// The reader has to tell these apart, and it cannot from the absence alone:
/// one is a hole in the design and the other is a hole in the wiring, and
/// they are fixed by different people (boss, 2026-09-04).
enum class NoDataReason : std::uint8_t {
  /// The light is computed; this field says nothing.
  kNone = 0,

  /// THE DESIGN HAS NOT GIVEN THE NUMBERS. No consumption rate for this
  /// stock exists in any table, so there is nothing to forecast from.
  /// Firewood today: heating design names the factors — frost, the state of
  /// the house, the unit's level, insulation — and not one number, and its
  /// own "what next" list still carries the open item.
  kRateNotInDesign,

  /// The rate exists and nothing in this build answers for the kind — a
  /// subsystem that is not wired yet. Not the same complaint at all: this
  /// one is ours to fix.
  kNoSubsystemAnswered,
};

/// @brief `days_of_stock` when nothing is being consumed at all: the stock
/// does not run out under the current arithmetic. An answer, not an error,
/// and not zero and not "a very large number" — a reader must be able to
/// tell it from a real forecast of many days.
constexpr std::int32_t kStockNeverRunsOut = -1;

/// @brief How far ahead a forecast is willing to look, in game days — two
/// years. A stock that survives the whole horizon reports exactly this
/// number, and a reader must take it as "at least this", not as a count.
///
/// There is a horizon because the honest way to forecast a stock with
/// per-feed ceilings is to drain it day by day, and something has to stop
/// the loop. Two years is past every date any light is measured against, so
/// the colour is never affected by where the horizon falls; only the story's
/// number saturates, and it saturates far outside the range in which a
/// story would act.
constexpr std::int32_t kStockForecastHorizonDays = 96;

/// @brief One light, ready to draw and ready to reason about.
struct StockForecast {
  StockKind kind = StockKind::kFood;

  StockLight light = StockLight::kNoData;

  /// Game days the stock lasts at the known consumption, from today.
  /// kStockNeverRunsOut when nothing consumes it. Meaningless — and left at
  /// 0 — when `light` is kNoData.
  std::int32_t days_of_stock = 0;

  /// Game days from today to the date this light is measured against: the
  /// harvest, the pasture, the end of the cold, the sowing. 0 when the date
  /// is today or unknown.
  std::int32_t days_to_date = 0;

  /// Why the light is dark. kNone whenever `light` is a colour.
  NoDataReason no_data_reason = NoDataReason::kNone;
};

/// @brief The colour of one light from its two numbers.
/// @param days_of_stock Days the stock lasts, or kStockNeverRunsOut.
/// @param days_to_date Days to the date the light is measured against.
/// @param margin_days How much slack "with room to spare" means for THIS
///        light: green needs the stock to outlast the date by at least this
///        many days. Comes from the owner's own table — the number differs
///        by light and is meant to (a hard winter is not a sowing norm), so
///        one shared threshold would be a wrong answer to three of the four
///        questions.
/// @param already_short True when the trouble is here NOW — the light's own
///        alarm stands. Red is a FACT, not a forecast: "already short", with
///        the alarm beside it saying the same thing after the event (office
///        design §5).
/// @return Never kNoData: that answer is about a light nobody computes, and
///         a caller that got here has computed one.
///
/// Deliberately the whole rule, in one place, for every light. The colour is
/// not a threshold on the stock — "red" is not "below N tonnes", because the
/// same tonnage is a comfortable winter for one herd and a disaster for
/// another. It is a threshold on TIME, and time is what the two numbers say.
StockLight LightFrom(std::int32_t days_of_stock,
                     std::int32_t days_to_date,
                     std::int32_t margin_days,
                     bool already_short);

}  // namespace core

#endif  // CORE_COMMON_STOCK_FORECAST_H_
