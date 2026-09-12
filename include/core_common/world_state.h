/// @file
/// @brief WorldState — the complete state of the simulated world.
/// @threading PARALLEL_READONLY
/// The double-buffer discipline of the step cycle governs all access: the
/// previous-step WorldState is read-only for every phase, the current one is
/// written by the phase that owns each block (single-threaded phases) or by
/// workers over disjoint row ranges (parallel phases). The step-cycle
/// contract (stage 1, task F2) formalizes the swap; this header only fixes
/// what the state is.
///
/// Design rules of this struct (the state model, manual/52-state-model.md):
///   * WorldState is a value: copying it snapshots the whole world. That is
///     what double buffering, saving and headless comparison runs rely on.
///     Plain data and std::vector only — no pointers, no handles to anything
///     outside the state.
///   * Everything the simulation computes from scratch each step — worker
///     productivity, family standing, "living signals" — is NOT stored here.
///     If it can be derived, it is derived (architecture, §4).
///   * The struct grows by plan stages: stage 2 fills weather, stage 3 adds
///     resident and family tables, stage 4 land and herds, stage 5 labor,
///     stage 7 the ledger of yearly flows — the one block nothing reads;
///     project phase 2 the order book and the step's event outbox, the
///     two blocks the boundary writes and reads (manual/70-boundary.md).
///     Adding a member is the expected, cheap extension (architecture, §7ж);
///     reshaping existing members is the expensive event.

#ifndef CORE_COMMON_WORLD_STATE_H_
#define CORE_COMMON_WORLD_STATE_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/event_state.h"
#include "core_common/family_state.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/resident_state.h"
#include "core_common/unit_state.h"

namespace core {

/// @brief Game epoch. Reaching the next one is the campaign's arc:
/// 80 residents at start, ~500 by Epoch II, ~1500 by Epoch III.
///
/// @enum_length kEpochCount — and this one gets its length BESIDE the enum
/// rather than inside it, which is the opposite of the rule everywhere else
/// (2026-09-04). The reason is the numbering: an epoch is a thing the player
/// is told about and there is no epoch zero, so a trailing `kEpochCount`
/// would take the value FOUR while there are THREE epochs. A terminator
/// that lies about the count is worse than none — every mirror would size
/// its array one too long and never hear a complaint. So the enum gets a
/// bound, `kEpochEnd`, which is honestly one past the last, and the count is
/// derived from it below.
enum class Epoch : std::uint8_t {
  kOne = 1,
  kTwo = 2,
  kThree = 3,

  /// NOT AN EPOCH: one PAST the last, for a range check. Because the enum
  /// counts from one this is four, not three — take kEpochCount for the
  /// number of them. Values are appended BEFORE it.
  kEpochEnd,
};

/// @brief How many epochs there are. Derived from the bound above, so a
/// fourth epoch moves both by being appended in one place.
inline constexpr std::uint32_t kEpochCount = static_cast<std::uint32_t>(Epoch::kEpochEnd) - 1;

/// @brief The epoch as an index into a dense per-epoch table.
///
/// The enum counts from ONE — an epoch is a thing the player is told about,
/// and there is no epoch zero — while every per-epoch table in the tables
/// counts from zero. This one line is where the two meet, and it was
/// written out four times in three files before task A6 collected it.
constexpr std::uint32_t EpochIndex(Epoch epoch) {
  return static_cast<std::uint32_t>(epoch) - 1;
}

/// @brief Precipitation on the current day.
enum class Precipitation : std::uint8_t {
  kNone = 0,
  kRain,
  kSnow,

  /// NOT A VALUE: the number of them, for a consumer's mirror. Values are
  /// appended BEFORE it.
  kPrecipitationCount,
};

/// @brief What the day IS, one name for it — the eight the design names
/// (camera design §4). One per day: the presentation shows one icon and the
/// quest layer orders one name, so a day that is both raining and foggy has
/// to be called something, and the roster is ordered so that the call is
/// always the same one.
///
/// WIND IS NOT IN HERE, and that is the whole shape of this enum
/// (Кожаный босс, 2026-09-05). Wind is random and almost unrelated to the
/// weather — a windy sunny summer day is an ordinary day — so it stands
/// BESIDE any of these rather than among them. Were it a ninth name, "clear
/// and still" and "clear and blowing" would be two different days with one
/// name, and the icon would have to drop "clear" to say "wind".
enum class WeatherPhenomenon : std::uint8_t {
  /// Nothing is happening to the sky worth a name of its own. The commonest
  /// day, and it exists as a NAME rather than as an absence so that the
  /// quest layer can order it: "a clear morning for the holiday".
  kClear = 0,

  /// Fog: the dense morning kind lying along the floodplain. Delays the
  /// start of work.
  kFog,

  /// Rain, from drizzle to downpour. Stops the harvest and spoils grain on
  /// the threshing floor.
  kRain,

  /// Thunder, lightning, darkening. MAY TO AUGUST AND NEVER OUTSIDE IT, by
  /// climate and by quest order alike: a thunderstorm in January is refused,
  /// not granted.
  ///
  /// A SQUALL IS POSSIBLE INSIDE IT AND NOWHERE ELSE, which is not the same
  /// as "a storm has one": the wind is drawn first and on its own, and a
  /// storm that was going to blow hard blows a squall instead. A still storm
  /// stays still — the rain is not what lays the corn.
  kThunderstorm,

  /// Snowfall, piling on the ground and the roofs. Snow on a field not yet
  /// reaped kills that harvest whole (farming design §6).
  kSnowfall,

  /// A blizzard: wind and snow together, no visibility, drifts, the road
  /// shut. IT DOES NOT COME IN A HARD FROST, which is physics and a live
  /// signal at once — the cruellest cold stands on the stillest, clearest
  /// day, so the two winter troubles look like opposites and neither can be
  /// mistaken for the other. The word is BLIZZARD and not "buran": the
  /// design keeps one term per thing.
  kBlizzard,

  /// Frost: rime and a skin of ice. Kills seedlings; anything tender dies
  /// in the night (farming design §4).
  kFrost,

  /// Heat: shimmer and burnt grass. Drought and a fall in the milk.
  kHeat,

  /// NOT A VALUE: the count, for a consumer's mirror. Values are appended
  /// BEFORE it.
  kWeatherPhenomenonCount,
};

/// @brief How hard it blows today, as a BAND and not a number.
///
/// The player never sees numbers (chairman design §5), and the layer draws
/// from the band, so the band is what crosses the seam. Whatever number the
/// generator used to pick it stays inside the generator.
enum class WindBand : std::uint8_t {
  /// Still. The grass stands, the chimney smoke goes up in a column.
  kCalm = 0,

  /// Waves across the field, crowns swaying, washing on the line.
  kWind,

  /// Dust, drifting snow, branches bending. Spray drifts off target, and
  /// snow is blown off the winter crop — a loss to the winter sowing.
  kStrongWind,

  /// A short furious gust that lays the standing corn. IT HAPPENS ONLY
  /// INSIDE A THUNDERSTORM: the squall is a part of the storm, not a
  /// weather of its own, and flattening the corn is its work and not the
  /// rain's.
  kSquall,

  /// NOT A VALUE: the count, for a consumer's mirror.
  kWindBandCount,
};

/// @brief The two counts as plain numbers, for anything that has to SIZE an
/// array by them. A `static_cast` at every such site reads as arithmetic on
/// a name; this reads as what it is.
inline constexpr std::size_t kWeatherPhenomenonCountValue =
    static_cast<std::size_t>(WeatherPhenomenon::kWeatherPhenomenonCount);

inline constexpr std::size_t kWindBandCountValue =
    static_cast<std::size_t>(WindBand::kWindBandCount);

/// @brief One day of the forecast: what it will be called and how it will
/// blow. Two fields because the seam carries two (WeatherPhenomenon,
/// WindBand) — "clear and a strong wind" is a legitimate forecast and a
/// legitimate quest order, and one name could not carry it.
struct DayForecast {
  WeatherPhenomenon phenomenon = WeatherPhenomenon::kClear;

  WindBand wind = WindBand::kCalm;
};

/// @brief Weather of the current day.
/// Written by the time-and-weather phase (phase 1, single-threaded), frozen
/// for the rest of the step. Daylight bounds the working day (time design,
/// §6); temperature drives heating and the cold metric.
/// @note STUB: the exact field set is confirmed at stage 2 (weather task);
/// these three are the ones other systems already depend on by design.
struct WeatherState {
  float air_temperature_celsius = 10.0f;

  float daylight_hours = 12.0f;

  Precipitation precipitation = Precipitation::kNone;

  /// Half the day's swing about the mean above, in Celsius: the afternoon is
  /// the mean PLUS this and the night is the mean MINUS it.
  ///
  /// It is a property of THE DAY and not of the season, which is the whole
  /// point: the sky moves it (a clear noon is hotter and a clear midnight
  /// colder — one phenomenon in two directions), while the mean stays where
  /// the season table put it so that nothing else in the simulation drifts.
  ///
  /// It lives here rather than being looked up from the weather table by
  /// each reader, and that is a fix rather than a convenience: production
  /// kept its own copy of the season amplitudes to compute the afternoon,
  /// which is one number with two homes.
  float temperature_swing_celsius = 0.0f;

  /// Overcast, 0 = clear sky, 1 = solid cloud. A quantity of the day in its
  /// own right: rain implies cloud, cloud does not imply rain, and an
  /// overcast dry day is exactly the day the swing rule exists for.
  float cloud_cover = 0.5f;

  /// What this day is CALLED — one name, the presentation's icon and the
  /// quest layer's order (WeatherPhenomenon).
  WeatherPhenomenon phenomenon = WeatherPhenomenon::kClear;

  /// How hard it blows, as a band (WindBand). A SECOND FIELD BESIDE THE
  /// FIRST and not a ninth name in it: the icon for wind is drawn next to
  /// the icon for the phenomenon, because otherwise a windy clear day would
  /// have to be called "wind" and lose the "clear".
  WindBand wind = WindBand::kCalm;

  /// HOW MANY DAYS THE SNOW HAS LAIN. 0 is bare ground; anything above is
  /// the core's word that snow COVERS the ground today.
  ///
  /// A STATE AND NOT AN EVENT, and that is the whole reason it is a field
  /// rather than a SimEvent: the presentation paints a POSITION, not a
  /// transition, and a world just loaded has to know whether snow lies
  /// before it has stepped once. Three readers wait on this one word and
  /// none of them may guess it from the month — the layer's white winter
  /// ("until the core says settled snow, winter ground is dead grass"), the
  /// fallen leaf that lies until the snow and not for a fixed forty days,
  /// and the field, where snow on an unreaped crop is the one total loss.
  ///
  /// THE ONLY FIELD OF THIS STRUCT THAT IS NOT A FUNCTION OF (seed, day).
  /// Everything else here is drawn afresh for whatever day is asked, which
  /// is what makes the three-day forecast free. Snow on the ground is
  /// history by nature — it is there because it fell and has not yet
  /// melted — so it is carried forward from yesterday by the weather phase
  /// and it is saved. That is a departure from the file's own rule, made
  /// deliberately and not by drift: the alternative was to walk the year
  /// back to the last certainly-bare summer day on every query, which costs
  /// a day's weather forty-eight times over to answer a question nobody
  /// asks about the future.
  ///
  /// COUNTED IN DAYS RATHER THAN STORED AS A FLAG, because a count is one
  /// fact and a flag beside a count would be two homes for it. What "the
  /// cover is settled" means is decided by the melt rule that maintains
  /// this number, not by a second threshold on top of it: a dusting that
  /// thaws tomorrow never reaches a second day.
  std::uint16_t snow_cover_days = 0;

  /// WHETHER A COVER HAS LAIN SINCE THE LAST LEAF FALL. False means the
  /// fallen leaf is still on the ground; true means it rotted under snow and
  /// is gone.
  ///
  /// TWO ZEROS LIVE IN snow_cover_days ABOVE, and only this word tells them
  /// apart: "no snow has fallen yet" and "the snow melted in a thaw". The
  /// first holds the leaf, the second does not — the design says the leaf
  /// does not reappear from under the snow because it ROTTED there, so a
  /// thaw returns nothing. The layer cannot separate them and must not try:
  /// after a load it has no memory of the transition, and it is obliged to
  /// paint a position (architecture §8г). Found by ue's first leaf-fall run,
  /// where 1 January of every campaign — before a single snowflake — came
  /// out with the forest in last year's leaves, over a zero that was honest.
  ///
  /// COUNTED FROM THE LEAF FALL AND NOT FROM THE WINTER. "This winter" was
  /// the first shape asked for, and it needs a reset date; a wrong date lies
  /// silently, and by this core's own weather the snow can lay in March, in
  /// which year the leaf must lie until March. The leaf cycle is the clock
  /// the design actually names, so the window is an event that is already
  /// modelled rather than a month somebody picks.
  ///
  /// RESET AND RAISE ARE BOTH LIVE. The reset is an EVENT and not a date
  /// picked in code: it falls on the first day of the month named by
  /// `leaf_fall_month` in world_params.csv — a table of its own, because
  /// weather_params.csv is named for weather and a month of leaf fall is not.
  /// A cover lying on that day raises it again the same day: the leaf falls
  /// in the morning and snow that evening rots it.
  bool cover_since_leaf_fall = false;
};

/// @brief The chairman's standing. He is an abstract figure without a body or
/// personal metrics (design: chairman), but his reputations are world state.
/// @note NO LONGER A STUB, and the note that said so was already false when
/// it was written: `horses_stabled` below is set by the herd day. Since
/// 2026-09-12 `raikom_reputation` moves as well — the district's verdict on
/// the year steps it up or down (core_production, JudgePlan). `authority`
/// and `shadow_reputation` DO still stand at their neutral defaults, and
/// naming which two is the whole repair: a blanket "STUB" over a struct
/// half of whose fields are alive tells a reader the opposite of the truth
/// about the other half.
/// @note Written only from the sequential decisions slot (slot 3): the herd
/// day and the production decisions sub-step. No parallel phase touches it.
struct ChairmanState {
  /// Reputation with the district committee, 0..100. The chairman's main
  /// metric (metrics design, §3).
  Metric raikom_reputation = 50.0f;

  /// Authority with the villagers, 0..100. Aggregate; decides re-election in
  /// Epoch III (metrics design, §6).
  Metric authority = 50.0f;

  /// Reputation in the shadow world, 0..100. Exists only with the role lines;
  /// stays 0 until that system exists.
  Metric shadow_reputation = 0.0f;

  /// 0/1: the kolkhoz horses have been gathered off the private yards into
  /// the kolkhoz yard — the ONE-TIME turn of the start canon (livestock
  /// design §5: the "at the horse" mark is set at the founding, lifted by
  /// the groom, and never comes back, whatever happens to the yard later).
  /// Set by the herd day the morning after a groom is appointed; read by the
  /// labor placement, which stops locking householders to horse work the
  /// moment it is set. A milestone of the campaign, which is why it sits
  /// with the chairman's numbers and not on any herd (task A7).
  std::uint8_t horses_stabled = 0;
};

/// @brief Settlement-wide vital statistics (design decision 105).
/// Life expectancy = 60 + medicine(0..+8) + nutrition(-4..+4) + living
/// conditions(0..+4) + working conditions(-2..+2), factors averaged over
/// 3 years, recomputed once a year. Phase 1 keeps medicine and living
/// conditions at zero and working conditions constant; only nutrition is
/// alive (stage 6). Its derivatives — the aging threshold (LE - 20) and the
/// last-birth median — are computed from the value, never stored.
/// Written only in the sequential demography sub-step.
struct VitalsState {
  /// Current life expectancy, biological years. Starts at the canonical 60.
  float life_expectancy_years = 60.0F;

  /// Mean settlement satiety of each of the last 3 finished years, oldest
  /// first — the nutrition factor's 3-year window. Neutral 70 start: the
  /// mapping knob turns 70 into a zero nutrition contribution.
  std::array<float, 3> satiety_year_means = {70.0F, 70.0F, 70.0F};

  /// Running mean accumulation of the CURRENT year: daily settlement mean
  /// satiety summed, and the number of days summed. Folded into
  /// satiety_year_means at the year turn, then reset.
  float satiety_running_sum = 0.0F;

  std::uint32_t satiety_running_days = 0;
};

/// @brief What the chairman has taken out of the sealed funds, by resource.
///
/// THE FUNDS ARE NOTIONAL AND THE GRAIN IS ONE HEAP, so unsealing cannot
/// move anything: every rung of the ladder is computed afresh each day out
/// of the sowing norms, the plan and the herds' keep (resources design §6),
/// and what a release does is make that computation ask for less.
///
/// ONE SLOT PER FUND, because the rungs fail differently: the seed fund's
/// release costs the spring sowing, the plan reserve's costs the autumn
/// delivery, the fodder fund's costs the horses their winter and so the
/// sowing its speed. A single number could not say which risk the chairman
/// took, and the risk is the whole content of the decision.
///
/// Written only where the order that fills it is consumed — the production
/// decisions sub-step of the sequential decisions slot (slot 3) — and read
/// by core_residents' distribution earlier in that same slot, off the
/// previous step's value.
///
/// @note Zeroed at the YEAR'S TURN and nowhere else. The design's door is for
/// an emergency ("нечем кормить людей"), and one that carried over would
/// quietly become a lower fund — but one cleared a second time in the spring
/// would erase the hungry winter it was opened for on the very day the
/// sowing year begins, which is what it did for one afternoon.
/// AN ARRAY OVER THE FUNDS AND NOT A FIELD PER FUND. It was two named
/// vectors for one afternoon, and a third rung of the ladder — the fodder
/// fund — would have cost a save format of its own for nothing but a name.
/// Indexed by FundKind, the fourth fund costs no format at all: the rung is
/// data, and the ladder is what the design keeps extending.
///
/// The kNone slot is carried and never written. Paying one empty vector to
/// keep the index and the enum the same number is the cheaper half of the
/// bargain — an index that needs shifting by one is the arithmetic nobody
/// gets wrong twice, only once.
struct FundReleaseState {
  std::array<ResourceAmounts, static_cast<std::size_t>(FundKind::kFundKindCount)> by_fund;
};

/// @brief How the district judged the economic year that has just closed.
///
/// THREE VALUES AND NOT A BOOLEAN, because "no year has been judged yet" is
/// news of its own: the first winter closes before the first harvest, and a
/// campaign that has never been judged must not read as a campaign that
/// passed. The same distinction the table readers keep making between "the
/// cell says nothing" and "the cell says zero".
enum class PlanVerdict : std::uint8_t {
  /// No economic year has closed yet. The value a new campaign carries.
  kNone = 0,

  /// Delivered in full on every position the district named.
  kMet,

  /// Short on at least one position. Three of these in a row is the first
  /// trigger of "Под суд" (epochs design §8).
  kFailed,

  /// NOT A VALUE: the count, for the codec's range check and for a
  /// consumer's mirror. Values are appended BEFORE it.
  kPlanVerdictCount,
};

/// @brief The yearly delivery plan and how the district judged the last one.
///
/// THE PLAN IS HANDED DOWN, and until 2026-09-12 it was not: `due` accrued
/// as the grain was reaped — a share of the settlement's own harvest (plan
/// §11, the phase-1 stub) — and was shipped at the year's turn. That made a
/// verdict impossible to fail: what you owed WAS what you had cut, so every
/// year was met by construction, and a counter of failed years would have
/// been a structural zero dressed as a check. The district names the figure
/// now, a bad year no longer forgives itself, and the counter can move.
///
/// The plan carries only what is GROWN (district design §9): grain by crop,
/// potato, vegetables, flax, milk, meat, egg, wool. Boards, workshop goods,
/// honey and fish never enter it.
struct PlanState {
  /// What the district expects this year, by resource. Dense by ResourceId.
  ResourceAmounts due;

  /// What has been delivered against `due` so far this year.
  ResourceAmounts delivered;

  /// The verdict on the year that closed last. kNone until the first one
  /// closes.
  PlanVerdict last_verdict = PlanVerdict::kNone;

  /// How many economic years in a row closed kFailed. Reset to zero by a
  /// met year. Reaching the threshold is the "три сорванных плана подряд"
  /// trigger of epochs design §8 — the CONDITION, which is the core's half;
  /// the commission, the case and the court are the presentation's.
  std::uint8_t failed_years_in_a_row = 0;

  /// How many in a row closed kMet — what "стабильное перевыполнение"
  /// (district design §9) will be counted on when the district starts
  /// raising norms. STUB: kept truthfully, read by nothing yet, because
  /// "раз в несколько лет район решает" names no period and a period
  /// invented here would be a mechanic invented here.
  std::uint8_t met_years_in_a_row = 0;
};

/// @brief The complete state of the simulated world at one step.
/// Two instances exist at run time — read buffer and write buffer — and swap
/// at the end of each step. A save is this struct serialized; a headless run
/// is this struct advanced 10 000 times and compared.
///
/// Growth plan (do not restructure, only append):
///   stage 5+: labor assignments live inside resident rows, not here.
struct WorldState {
  CalendarState calendar;

  WeatherState weather;

  Epoch epoch = Epoch::kOne;

  /// Every person of the settlement (stage 3). Row layout: resident_state.h.
  ResidentTable residents;

  /// Every household (stage 3). Row layout: family_state.h.
  FamilyTable families;

  /// Every field (stage 4). Row layout: land_state.h.
  FieldTable fields;

  /// Every unit (stage 4; construction itself is deferred). unit_state.h.
  UnitTable units;

  /// Every herd (stage 4; sizes static until feeding). herd_state.h.
  HerdTable herds;

  /// Campaign seed: fixed at world creation, never changes, drives every
  /// derived counter-style random draw. Same seed, same commands — same world.
  std::uint64_t world_seed = 0;

  /// The one sequential RNG of the world (random.h): advanced only in
  /// single-threaded phases; parallel code uses counter hashes instead.
  RngState rng;

  ChairmanState chairman;

  /// The district's demand and its verdict on the last year. Written only
  /// in the production decisions sub-step of the sequential decisions slot
  /// (slot 3) — announced in the spring, judged at the year's turn — and
  /// read there and by core_residents' distribution, which runs earlier in
  /// the same slot.
  PlanState plan;

  /// HOW MUCH OF THE WORKING STOCK'S WORK RATION WAS COVERED BY FODDER GRAIN
  /// yesterday, 0..1 — the traction ration (boss's decision of 2026-09-12;
  /// resources design §6).
  ///
  /// WHY THIS EXISTS, and the reason is a defect rather than a wish. Hay
  /// carries a horse's whole need (`max_share` 1.0 in feed_links.csv), so a
  /// horse on hay alone is FED: `unfed_days` stays nil, and unfed_days is
  /// the only thing anything reads. The model therefore said "alive, so it
  /// pulls at full strength", and unsealing the fodder fund cost the
  /// chairman nothing whatever — oats paid for what hay gives away. That is
  /// not a wage without a holder; it is A PRICE WITH NO SUBJECT.
  ///
  /// Hay is the MAINTENANCE ration and fodder grain the WORK ration, which
  /// is what the `work_only` column of feed_links.csv has said from the
  /// first day — a flag that was right all along and had not one reader.
  /// This is its reader: the share scales the ploughing and the sowing and
  /// nothing else. Milk, deaths and the alarm stay with `unfed_days`, where
  /// they belong and where they already work.
  ///
  /// YESTERDAY'S ONLY AT THE DAY'S FIRST TICK, and this block said
  /// "yesterday's" flatly until 2026-09-12, which was wrong for twenty-three
  /// hours in twenty-four. The herd day writes the ration ONCE, at the tick
  /// that turns the day. Phases are opened by AdvanceFinishedPhases, which
  /// runs on EVERY tick on purpose — a field ploughed by noon opens its
  /// harrowing at noon rather than losing the afternoon. So a phase opened
  /// at the day's turn is pulled by animals fed last night, and one opened
  /// later the same day by animals fed that morning.
  ///
  /// NOTHING RACES: both live in the same sequential sub-step and the order
  /// inside a tick is fixed, so the run is deterministic either way. What
  /// was wrong was the SENTENCE — and the sentence is what the next reader
  /// would have built on.
  ///
  /// One number for the whole settlement rather than one per herd: a phase
  /// is opened on a field and the core does not say which horse pulls it.
  /// Naming a horse would be a precision the order book does not carry.
  float traction_ration = 0.0F;

  /// What the chairman has taken out of the sealed funds this economic year
  /// (FundReleaseState). Zeroed at the year's turn with the plan it belongs
  /// to — an unsealing is an emergency of ITS year, not a standing licence.
  FundReleaseState unsealed;

  /// Life expectancy and its factor window (stage 6, decision 105).
  VitalsState vitals;

  /// The accountant's yearly book of flows (stage 7). Nothing in the
  /// simulation reads it; the run report does. ledger_state.h.
  LedgerState ledger;

  /// The chairman's order book (project phase 2, the boundary): commands
  /// that came across the boundary, waiting for or being executed by the
  /// subsystem whose rules apply. Appended only by the step engine before
  /// phase 1; SAVED — a waiting order survives a load. order_state.h.
  OrderTable orders;

  /// This step's outbox (project phase 2, the boundary): what happened,
  /// for the presentation. Cleared by the step engine after the copy,
  /// appended by sequential code only, NOT SAVED. event_state.h.
  StepEventLog step_events;
};

}  // namespace core

#endif  // CORE_COMMON_WORLD_STATE_H_
