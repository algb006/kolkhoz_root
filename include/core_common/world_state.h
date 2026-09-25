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
#include <memory>

#include "core_common/calendar.h"
#include "core_common/district_car_state.h"
#include "core_common/district_visit_state.h"
#include "core_common/event_state.h"
#include "core_common/extraction_state.h"
#include "core_common/family_state.h"
#include "core_common/herd_state.h"
#include "core_common/land_state.h"
#include "core_common/ledger_state.h"
#include "core_common/limit_state.h"
#include "core_common/night_trade_state.h"
#include "core_common/order_state.h"
#include "core_common/quantities.h"
#include "core_common/random.h"
#include "core_common/readiness_state.h"
#include "core_common/resident_state.h"
#include "core_common/road_rules.h"
#include "core_common/road_state.h"
#include "core_common/specialist_state.h"
#include "core_common/timber_state.h"
#include "core_common/unit_state.h"
#include "core_common/wedding_state.h"

namespace core {

class RoadIndex;  // core_common/road_route.h — the network made queryable.

// Epoch and kEpochCount live in core_common/calendar.h since 2026-09-15: the
// day off is a pure function of the weekday and the epoch, and the host asked
// for it in a public header that does not pull the whole world state.

/// @brief The epoch as an index into a dense per-epoch table.
///
/// The enum counts from ONE — an epoch is a thing the player is told about,
/// and there is no epoch zero — while every per-epoch table in the tables
/// counts from zero. This one line is where the two meet, and it was
/// written out four times in three files before task A6 collected it.
constexpr std::uint32_t EpochIndex(Epoch epoch) {
  return static_cast<std::uint32_t>(epoch) - 1;
}

/// @brief The epoch as the HUMAN number that table keys and columns use —
/// `categories_top_epoch_1`, the `era` column of unit_types.csv.
///
/// It is the enum value unchanged, and it exists anyway, for one reason:
/// until 2026-09-18 there was a name for the 0-based direction and none for
/// this one, so a reader who needed the human number wrote `+ 1` — twice in
/// one file, in a world where the enum ALREADY counts from one. Both readers
/// then asked every table for the next era, and the bug survived a warning
/// that printed the wrong key by name nine times a run.
///
/// A conversion with no name gets written as arithmetic, and arithmetic on an
/// enum whose base the reader has not checked is a guess. Now both directions
/// have a name and neither has a number.
constexpr std::uint32_t EpochHumanNumber(Epoch epoch) {
  return static_cast<std::uint32_t>(epoch);
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

/// @brief THE SKY OF THE DAY — five steps, the day's first quantity (camera
/// design §4 «Небо — пять ступеней»; the human's word, 2026-09-18: «1. Ясно.
/// 2. Переменная облачность. 3. Пасмурно. 4. Слабые осадки. 5. Сильные осадки
/// и сильный ветер»). The generator draws it before anything else and every
/// other field of the day is read from it; the HUD icon is drawn by it.
///
/// NAMED, NEVER NUMBERED. The design counts the steps from one and this enum
/// counts from nought; a table indexed by the enum is indexed by the NAME's
/// value, and a human number is never computed from it by adding one — that
/// arithmetic is how Epoch's `+ 1` bug lived a week (world_state.h, above).
enum class SkyStep : std::uint8_t {
  kClear = 0,           ///< 1. Ясно — no cloud, no precipitation.
  kPartlyCloudy,        ///< 2. Переменная облачность — sky in breaks.
  kOvercast,            ///< 3. Пасмурно — solid cloud, dry.
  kLightPrecipitation,  ///< 4. Слабые осадки — drizzle, rain, sparse snow.
  /// 5. Сильные осадки — a HEAVY PHASE of 2–12 hours inside the day, the
  /// rest of the day light (WeatherState::heavy_from_hour, heavy_hours);
  /// strong wind, a squall in a thunderstorm, only inside the phase. Never
  /// two such days running.
  kHeavyPrecipitation,

  /// NOT A VALUE: the count, for a consumer's mirror and a table's size.
  kSkyStepCount,
};

inline constexpr std::size_t kSkyStepCountValue = static_cast<std::size_t>(SkyStep::kSkyStepCount);

/// @brief What the day IS CALLED beyond its sky: the FORM of its
/// precipitation on steps 4–5, or one SIGN beside a dry sky (camera design §4).
/// One per day, and since 2026-09-18 the two families cannot meet — a form
/// exists only on a wet step and a sign only on a dry one — so the priority
/// below orders only the signs among themselves.
///
/// WIND IS NOT IN HERE, and that is the whole shape of this enum
/// (Кожаный босс, 2026-09-05). Wind is random and almost unrelated to the
/// weather — a windy sunny summer day is an ordinary day — so it stands
/// BESIDE any of these rather than among them. Were it a ninth name, "clear
/// and still" and "clear and blowing" would be two different days with one
/// name, and the icon would have to drop "clear" to say "wind".
enum class WeatherPhenomenon : std::uint8_t {
  /// NOTHING BEYOND THE SKY: the step says it all. Until 2026-09-18 this was
  /// `kClear` and meant "a clear day"; clear is now SkyStep::kClear, and a
  /// dry overcast day is also this value. Renamed rather than given a second
  /// meaning under the old name — a name that changes what it means and
  /// keeps its spelling is how two readers part without either noticing.
  kNone = 0,

  /// SIGN: fog, the dense morning kind lying along the floodplain — on
  /// steps 1–3, in still or light wind. Delays the start of work.
  kFog,

  /// FORM of steps 4–5 in the warm: drizzle on 4, a downpour on 5 outside
  /// the storm window. Stops the harvest and spoils grain on the threshing
  /// floor.
  kRain,

  /// FORM of step 5 in the warm, MAY TO AUGUST — ALWAYS, and never outside
  /// it (the human's word, 2026-09-18: «грозы бывают с мая по август»). The
  /// squall is inside it and nowhere else.
  kThunderstorm,

  /// FORM of step 4 in the cold, and of step 4 between −1 and +1 where it is
  /// wet snow — DRAWN as snow and COUNTED as rain (precipitation kRain).
  /// Snow on a field not yet reaped kills that harvest whole (farming §6).
  kSnowfall,

  /// FORM of step 5 in the cold, DECEMBER TO FEBRUARY — ALWAYS (the human's
  /// word, 2026-09-18: «Тоже самое метель»). Wind and snow together, the
  /// road shut. Below −12 step 5 does not come at all: the cruellest cold
  /// is the stillest, clearest day. The word is BLIZZARD and not "buran".
  kBlizzard,

  /// SIGN: frost, rime and a skin of ice — on steps 1–2, a clear night in
  /// the growing months. Kills seedlings (farming design §4).
  kFrost,

  /// SIGN: heat, shimmer and burnt grass — on steps 1–2, afternoon from
  /// +28. Drought and a fall in the milk.
  kHeat,

  /// FORM of step 5 in the cold OUTSIDE December–February: heavy snowfall,
  /// a second row of flakes on the icon and no wind strokes (boss,
  /// 2026-09-18, commit 32849dd5). Appended, not placed beside its
  /// siblings: the order of this enum is the wire format.
  kHeavySnowfall,

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
  /// The sky first, as the generator draws it and the icon is drawn by it.
  /// A forecast shows a day of step 5 as step 5, whatever its hours.
  SkyStep sky = SkyStep::kPartlyCloudy;

  WeatherPhenomenon phenomenon = WeatherPhenomenon::kNone;

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

  /// Overcast, 0 = clear sky, 1 = solid cloud. SINCE 2026-09-18 A READING OF
  /// THE STEP, not a draw of its own: the step is the seam's word and the
  /// layer does not interpret this number. Kept for its readers.
  float cloud_cover = 0.5f;

  /// THE SKY — the day's first quantity (SkyStep). Everything else in this
  /// struct except the temperature and the snow is read from it.
  SkyStep sky = SkyStep::kPartlyCloudy;

  /// Step 5 only: the hour its heavy phase begins, 0..23, and how many hours
  /// it lasts, 2..12 by the tables; the rest of the day is step 4. Both 0 on
  /// any other step. The layer draws the downpour in these hours and decides
  /// nothing itself (camera design §4, the human's word 2026-09-18: «Сильные
  /// остадки не должны продолжаться долго, н более половину суток»).
  std::uint8_t heavy_from_hour = 0;
  std::uint8_t heavy_hours = 0;

  /// What this day is CALLED beyond its sky — a precipitation form on steps
  /// 4–5 or one sign on a dry step (WeatherPhenomenon).
  WeatherPhenomenon phenomenon = WeatherPhenomenon::kNone;

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

  /// РАСПУТИЦА — the dirt roads are soaked today (econ's mud-season audit,
  /// boss seq 182 and 186; roads design §1). Carts and carriers go at
  /// `mud_speed_factor` of their speed, and a district lot ordered today takes
  /// its base term divided by it. The layer draws the mud from this word.
  ///
  /// A FUNCTION OF (seed, day), like the sky and unlike the snow cover: it is
  /// decided by MudOnDay (weather_of_day.h) from the days' own weather, which
  /// any day can draw for itself — so a forecast could ask MudOnDay too. NOT
  /// WeatherOfDay: that one leaves this field false, and only the weather
  /// phase writes it. Saved
  /// anyway, for the reason the sky is: a loaded world answers before it has
  /// stepped once. Save 72.
  ///
  /// SINCE 0.36.8 THE HAUL READS the dirt bed's condition below instead,
  /// which carries this word; the district's cart and lots still read it.
  bool mud = false;

  /// What the weather has made of each bed today and how long each stays
  /// wet (road_rules.h, RoadBedsAfter; roads design §1). Written once a day by
  /// the weather phase from yesterday's beds — history, like the snow cover,
  /// so saved. Save 95.
  RoadBeds road_beds;
};

/// @brief The sports field's month (leisure §12, «Погода для уличных
/// занятий»; register 223; save 68). A day is OPEN when its mean is at
/// `sport_open_temp_c` or over, it has no precipitation, and it is not the day
/// after a downpour; a month COUNTS when `sport_open_days_min` of its days
/// were open.
struct SportMonth {
  /// Open days in the month running; cleared at the month's turn.
  std::uint8_t open_days = 0;

  /// 0/1: yesterday had heavy hours — a downpour, and the field is mud today.
  std::uint8_t downpour_yesterday = 0;
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

  /// 0/1: the chairman's standing order to spend the summer nights at
  /// pasture (kGrazeAtNight). Given once and held: «с этого лета уводится
  /// каждое». It sits with the chairman's numbers for the reason
  /// `horses_stabled` does — it is a decision of the campaign and not a
  /// property of any herd, and there is one team.
  std::uint8_t night_pasture_ordered = 0;

  /// 0/1: the team has been out at least once, so the first night is said
  /// once and not every evening of every summer.
  std::uint8_t night_pasture_begun = 0;

  /// 0/1: the automatic minimum ration — the checkbox of labor-payment §5
  /// («можно включить заранее, и тогда паёк выдаётся автоматически всем, кто
  /// просел»). Genesis writes it from food.csv `ration_auto`, which is its
  /// START VALUE and no longer the rule (boss, 2026-08-30: «авто-режим станет
  /// стартовым значением галочки, когда появится игрок»); from then on only
  /// the chairman's kSetRation moves it.
  std::uint8_t ration_auto = 1;

  /// THE CANCELLED DAY OFF (kCancelDayOff; time §9, leisure §6-§7; save 65):
  /// how many cancelled days off stand in a row. The first day off actually
  /// taken breaks the series; the rest price of the next one is −4 × this.
  std::uint8_t days_off_cancelled_in_a_row = 0;

  /// The day the chairman cancelled, or 0 when none stands. 0 is free as the
  /// sentinel because an order cancels from TOMORROW on, and no order is
  /// read before day 0 — so the day it names is 1 or later.
  /// Read through IsDayOffIn (core_common/day_off.h) and nowhere else.
  SimDay cancelled_day_off = 0;

  /// THE CHAIRMAN'S TALK (kTalkToSport; boss seq 141 А; save 69): the
  /// calendar season of the village's last talk, plus one — 0 when there has
  /// been none. A season is December–February, March–May, June–August or
  /// September–November, counted from the campaign's start (TalkSeasonOf,
  /// core_residents/sport.h); one talk a season.
  std::uint32_t last_talk_season = 0;

  /// Where the children keep the team: a point on a floodplain meadow,
  /// drawn once from the campaign's own generator when the order is given.
  ///
  /// THE CORE PICKS IT AND NOT THE LAYER, and the design says why: geometry
  /// is the layer's constant and the layer does not choose within it, so a
  /// point the layer drew would part company with the save on the first
  /// evening. Zero when no order stands.
  Vec2 night_pasture_place;

  // -- the trip to the district (econ/manual/proposals/district-trip.md;
  // boss seq 187, 205-206; save 77) ----------------------------------------

  /// The chairman is AWAY from `away_from_tick` (the day's 8:00) to
  /// `away_until_tick` (that evening; in the mud the next morning). Both 0
  /// when no trip stands; a trip booked for tomorrow stands with its future
  /// ticks. While away, no order to the village is accepted
  /// (OrderRefusal::kChairmanAway) and a district visit waits for him; the
  /// standing orders and rules run the village.
  Tick away_from_tick = 0;
  Tick away_until_tick = 0;

  /// The day of the last trip of his own, + 1 — 0 when none: «раз в месяц»,
  /// a second in the same month is refused (kTripThisMonth). A summons does
  /// not count.
  std::uint32_t last_trip_day = 0;

  /// «НА КОВЁР» (district-trip.md §3): the day the district's letter comes
  /// and the day he is called for, 0 when none stands. The letter comes with
  /// the next post after the cause (STUB: the next day — the core knows no
  /// post days), the summons two days after the letter. He cannot refuse:
  /// at 8:00 of `summon_day` he leaves by himself; a blizzard moves the day
  /// to the first passable one.
  std::uint32_t summon_letter_day = 0;
  std::uint32_t summon_day = 0;

  /// The year whose plan he has bargained, + 1 — 0 when never: «раз в год,
  /// до апреля» (kTradePlan).
  std::uint16_t plan_traded_year = 0;

  /// Why he is summoned (SummonCause), kNone when no summons stands.
  std::uint8_t summon_cause = 0;

  /// 0/1: the trip under way is a summons — it does not count as his own.
  std::uint8_t away_summoned = 0;

  /// 0/1: «НА КАРАНДАШЕ» WAITS (boss, boss-core-epoch1-2 seq 8). The
  /// reputation crossed the pencil line while another summons stood, and a
  /// summons is one at a time. On his return from that trip the pencil calls
  /// him once if the reputation is still at or below the line; risen above
  /// it, the mark goes silently. One deferred summons, never a repeat
  /// without a new crossing. Save 81.
  std::uint8_t pencil_pending = 0;
};

/// @brief Why the district calls the chairman «на ковёр» (district-trip.md
/// §3; boss seq 206). A complaint upward is a STUB until it exists.
enum class SummonCause : std::uint8_t {
  kNone = 0,
  kFailedYear,   ///< The year closed with the plan failed (PlanVerdict::kFailed).
  kOnThePencil,  ///< raikom_reputation fell to 20 or below («на карандаше»).

  /// An auditor's visit found a discrepancy (DistrictVisitFinding::
  /// kDiscrepancy). Appended the day after the contract: the estimate called
  /// the audit a STUB, and the core has had it since 2026-09-15.
  kAuditDiscrepancy,

  /// NOT A CAUSE: the count, for the mirrors.
  kSummonCauseCount,
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

  /// THE ACCUMULATION LIMIT (district §9 «Лимиты накопления»; register 234;
  /// boss, 2026-09-18): how much of each plannable produce the kolkhoz may
  /// hold in its stores, named with the plan in the spring. Dense by
  /// ResourceId; 0 = no limit on it (the first year has none — the district
  /// has no book of a year gone to size it from). A finance auditor's visit
  /// seizes whatever stands above it (district_visit.cpp). Save 62. The plan
  /// board shows it beside the stock, so the seizure can be foreseen.
  ResourceAmounts accumulation_limit;

  /// THE MILK CART'S SHARE OF A DAY (district §9 «Молоко — в плане с
  /// первого года»; register 231; boss seq 98 and 113; save 66), grams: the
  /// milk position ÷ the milking days from the spring's announcement to the
  /// turn. Named once with the figure. A day the herd gives less is not
  /// forgotten: its shortfall goes to milk_debt below. 0 before the
  /// announcement and after the turn.
  Grams milk_daily_share = 0;

  /// THE MILK OWED FROM SHORT DAYS (district §9, «Молоко — в плане с первого
  /// года»; the human's word «Молоко - вариант с долгом»; boss seq 26/28;
  /// save 85), grams. When the cart takes less than the day's share plus
  /// this debt, the difference stays here. When there is more milk, the cart
  /// takes the debt first, before the next morning's issue. Milk left after
  /// the issue goes over the plan and never pays the debt. Whatever is still
  /// owed at the turn is simply short in the verdict; the debt does not carry
  /// over the turn and is cleared with the share.
  Grams milk_debt = 0;

  /// WHAT WENT TO THE DISTRICT WITH NO POSITION TO GO AGAINST, by resource
  /// (district §1; boss seq 113; save 66): the winter's milk, from the turn
  /// to the spring's figure, left after the issue and carted all the same.
  /// Over the plan by construction: the overfulfilment counts it in tonnes of
  /// grain beside every position's surplus, and nothing in it can make up a
  /// position short. Cleared at the turn, after the year is scored.
  ResourceAmounts delivered_outside;

  /// THE DISTRICT'S GOODS LOAN OWED, by resource (wage design §6; district
  /// design, «Товарный заём»; boss, boss-core-epoch1-5 seq 15; save 89),
  /// grams, the markup included: a loan taken books its grams × (1 +
  /// goods_loan_markup) here. Paid at the year's turn after the plan, in kind,
  /// from what the plan could take above the held seed (DeliverableAboveSeed);
  /// what is left carries on and takes the markup again. NOT cleared at the
  /// turn and with no ceiling: «если и следующий год плохой — долг
  /// накапливается и душит». kGoodsLoanOwed stands while any is owed.
  ResourceAmounts goods_loan_owed;

  /// The loan TAKEN this year, by resource, grams as borrowed (no markup):
  /// one loan a resource a year, and this is what says a year has had it.
  /// Cleared at the turn.
  ResourceAmounts goods_loan_taken;

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

  /// WHETHER THE DISTRICT HAS NAMED A FIGURE THIS YEAR, 0 or 1 — and it is a
  /// fact of its own because a zero tonnage cannot carry it.
  ///
  /// An all-zero `due` is four different worlds: a settlement that worked no
  /// land last year, a table set with no campaign in it, a mistyped plan
  /// roster, and a year in which the district genuinely asked for nothing.
  /// Three of them must be quiet and one must be loud, and they are the same
  /// state in a tonnage of zero — so `kNoPlanYet`, the reserve door and the
  /// verdict all read one vector and cannot tell them apart.
  ///
  /// WRITTEN AND READ, and this paragraph said the opposite until 2026-09-17
  /// — the note outlived its own subject, which is the more dangerous half of
  /// a stale comment: a reader deciding whether a field can be trusted asks
  /// the note, not the tree. Set at the spring announcement even when the
  /// figure is zero (district_plan.cpp, AnnouncePlan), cleared beside the
  /// figure it describes both there and at the year's turn. Read by the
  /// plan's judge and the milk cart; `PlanHoldsIt`, which held last year's
  /// positions by the LIST, went in 0.34.42 (the plan rung holds the debt).
  ///
  /// What it still does not do is answer at the other three doors: kNoPlanYet,
  /// the reserve door and the verdict key off the tonnage to this day, and
  /// moving them changes what a chairman is told, which is boss's to
  /// schedule.
  std::uint8_t announced = 0;

  /// THE WORKED ARABLE OF THE YEAR THAT CLOSED, in hectares — the area the
  /// spring's norm is computed from. District design §9 asks for it in those
  /// words, and the emphasis is mine and outside the quotation: "по
  /// обработанной пашне прошлого года и нормальному урожаю с гектара" — LAST
  /// year's, which is the half that makes the figure un-gameable.
  ///
  /// IT IS LAST YEAR'S BECAUSE THIS YEAR'S IS THE CHAIRMAN'S TO CHANGE. The
  /// norm was priced off the crop standing in each field's slot until
  /// 2026-09-13, and an empty slot priced at nothing: a chairman who laid
  /// every field to fallow — or who withdrew every chain, which the order
  /// book allows on purpose as the forgiving move — owed the district
  /// NOTHING, verdict and trial included. A figure a player can zero on the
  /// morning it is read is not a norm, it is a button.
  ///
  /// Written at the year's turn, read at the spring announcement, and that
  /// order is also what district design §9 asks for in another line: raised
  /// ground enters the plan the year AFTER it is broken.
  ///
  /// IT IS THE LARGEST AREA THE YEAR HELD, not the area of one day, and the
  /// difference is the whole of the repair. A first draft sampled the fields
  /// at the turn — and the order book is read at the TOP of the same call, so
  /// a chairman could withdraw every chain on the last tick of December, let
  /// the figure be taken as zero, and re-issue the chains the next morning:
  /// two orders per field per year, nothing sown in January anyway, the
  /// harvest untouched and the district asking NOTHING for ever. The escape
  /// must cost something, and against a yearly maximum it costs the whole
  /// harvest: the fields have to stay released all year for the figure to
  /// fall.
  ///
  /// AND SINCE 2026-09-18 IT DOES NOT FALL AT ALL: the base is the larger of
  /// itself and the year's maximum (register 222). The yearly maximum made
  /// the escape cost one failed year and then switched the plan off for good;
  /// «недосев — способ провалить план, а не уменьшить его» (district §9)
  /// needs a base that sowing less cannot lower. Only the district writes
  /// arable off, and Epoch I has no such verb — a removed field stays on it.
  float worked_ha_last_year = 0.0F;

  /// The largest worked arable seen so far THIS year, in hectares — the
  /// running half of the pair above, raised daily and moved into it at the
  /// turn. Not a number anybody reads on its own.
  float worked_ha_this_year = 0.0F;
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

  /// THE CHAIRMAN'S ISSUE NORMS, grams per trudoden, by ResourceId — the
  /// bundle's positions of labor-payment §3 as the chairman set them
  /// (kSetIssueNorm; econ's audit M1, Л1). EMPTY until his first order, and
  /// empty means the table's norms stand (food.csv `issue_kg_per_trudoden`):
  /// the first order copies the whole bundle out of the table and moves one
  /// position, so the table is read in one place and the world holds only
  /// what the chairman decided. Read through IssueNormOf (family_exchange).
  ResourceAmounts issue_norms;

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

  /// Every place timber can be taken from (timber design §8a, 2026-09-13):
  /// groves, shelterbelts and the old-forest squares within a team's reach.
  /// Made at genesis from tables/timber_stands.csv; SAVED. timber_state.h.
  TimberStandTable stands;

  /// The district's limit (district design §1): the points left this year.
  /// Granted on the first tick and at every year's turn, burnt at the turn;
  /// SAVED.
  /// limit_state.h.
  LimitState limit;

  /// Lots bought on the limit and still on the district's cart (§4); SAVED.
  /// limit_state.h.
  LimitDeliveryTable limit_deliveries;

  /// Stock bought on the limit and not yet standing in the village (district
  /// design §1, "Скот и птицу поставляют без обоза"); SAVED. It is a table of
  /// its own and not a cart, because stock does not ride one: the head
  /// appears under a roof on its day, whole. limit_state.h.
  LivestockArrivalTable livestock_arrivals;

  /// The district MTS's column of this season (MTS design §1); SAVED.
  /// limit_state.h.
  MtsColumnState mts_column;

  /// The era events that have already come (epochs design §14); SAVED.
  /// limit_state.h.
  EraEventState era_events;

  /// The teachers and librarians the district is sending and who have not
  /// arrived yet (education design, "Эпоха I числами"); SAVED.
  /// specialist_state.h.
  SpecialistArrivalTable specialist_arrivals;

  /// The district's visits announced or called and not arrived yet
  /// (characters design §2); SAVED. district_visit_state.h.
  DistrictVisitTable district_visits;

  /// The district's cars on the road for somebody of the village — the
  /// ambulance (register 236; boss seq 210); SAVED (save 79).
  /// district_car_state.h.
  DistrictCarTable district_cars;

  /// The road network: one row a road or path, its axis and its condition
  /// by stretch (roads design §13); SAVED (save 92) — a map road without its
  /// axis, which the loader puts back from tables/roads.csv. road_state.h.
  RoadTable roads;

  /// The network made queryable (road_route.h): its graph, node-to-node
  /// distances and a grid of its pieces. DERIVED and NOT SAVED — built from
  /// `roads` at genesis and on load, and rebuilt by whatever changes the
  /// network; immutable and shared by the copies of the world, so the double
  /// buffer copies a pointer. Null in a world nobody indexed (RoadIndexOf
  /// then builds one on the spot).
  std::shared_ptr<const RoadIndex> road_index;

  /// The quiet trades out tonight (crime design §9); SAVED.
  /// night_trade_state.h.
  NightOutingTable night_outings;

  /// What the distillers took off the stores this month (crime design §7);
  /// SAVED. night_trade_state.h.
  NightTheftTally night_theft;

  /// The sports field's month (leisure §12, «Погода для уличных занятий»;
  /// register 223; boss seq 127); SAVED (save 68). Counted day by day by
  /// core_residents (sport.h) and read at the month's turn, then cleared.
  SportMonth sport_month;

  /// The couples ready to marry who wait for a free house (life-cycle design
  /// §12); SAVED. wedding_state.h.
  WeddingWaitTable wedding_waits;

  /// The plots clay, stone and sand are dug on (construction design §3;
  /// boss, parcel 270). Made at genesis from tables/extraction_sites.csv;
  /// SAVED. extraction_state.h.
  ExtractionSiteTable extraction_sites;

  /// Readiness for the era transition as of the last turn of the year
  /// (epochs design §6); SAVED. readiness_state.h.
  ///
  /// STATE AND NOT A DERIVATION, unlike the office's lights next door, and
  /// the difference is the history: "both indices above their thresholds for
  /// three years running" and "the wintering closed two years in a row"
  /// cannot be recomputed from a world that only holds today. A light is a
  /// forecast the session stands again on every load; a run of years is a
  /// fact the campaign accumulated and a save must carry.
  ReadinessState readiness;

  /// This step's outbox (project phase 2, the boundary): what happened,
  /// for the presentation. Cleared by the step engine after the copy,
  /// appended by sequential code only, NOT SAVED. event_state.h.
  StepEventLog step_events;
};

}  // namespace core

#endif  // CORE_COMMON_WORLD_STATE_H_
