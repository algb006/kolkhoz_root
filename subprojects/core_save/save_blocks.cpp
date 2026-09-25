// The scalar world blocks and the ledger (save_blocks.h). Same law as the
// row codecs: declaration order of the defining header, the two directions
// adjacent, a sizeof tripwire per block that has one.

#include "save_blocks.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include "core_common/aggregate_arity.h"
#include "core_common/calendar.h"
#include "core_common/ledger_state.h"
#include "core_common/world_state.h"
#include "save_dictionary.h"
#include "save_stream.h"

namespace core {
namespace {

// Sizes measured for save format 1 on x86-64.
//
// A ResourceAmounts is a std::vector, and sizeof(std::vector) belongs to the
// standard library and its debug level — 24 bytes under libc++, 32 under the
// MSVC STL with iterator debugging — not to the save format, which writes the
// vector element by element. Counting the vectors out leaves the block's own
// fields, which is what adding a field actually moves. The first MSVC build
// of this module is what taught us: the three blocks that hold amounts
// tripped at once while the nine that do not held, which is a compiler
// telling the truth about its layout, not a format that changed.
constexpr std::size_t kAmountsSize = sizeof(ResourceAmounts);

// 2026-09-13: work at a producing unit widened work_days_by_kind by a float,
// and the alignment of the block rounded the four bytes up to eight.
// 2026-09-13: the limit's three int32 flows took it to 160.
// 2026-09-14: digging (WorkKind::kExtraction) widened work_days_by_kind by a
// float: 168, measured.
// 2026-09-15: the night trades' catch, a fourteenth amounts vector (save
// format 40).
// 2026-09-15 again: what the distillers stole, a fifteenth (save format 42).
// 2026-09-17: the era-readiness index's nine yearly inputs — satisfaction's
// sum and count, able-bodied person-days, the plan's per cent and the byte
// that says it exists, and the four numbers of the wintering as it stood on
// 1 December (save format 52). 168 became 200, measured: twenty-eight bytes
// of fields in thirty-two of growth, so four went to padding — which is
// exactly why the arity below stands beside the size and not instead of it.
// 2026-09-17 again, save 53: the worst season's food variety and the count of
// seasons lived, for the transition's variety block. 200 became 208.
// 2026-09-18, save 58: plan_due, what the district asked by position (M12) —
// a sixteenth amounts column and a fifty-sixth field.
// 2026-09-18, save 60: samogon_paid, the drink's price in kind (register
// 205) — a seventeenth column and a fifty-seventh field.
// 2026-09-18 again, save 61: lost_to_snow, the standing crop the snow took —
// an eighteenth column and a fifty-eighth field.
// Save 62: seized, what the district took above the accumulation limit — a
// nineteenth column and a fifty-ninth field.
// Save 63: the season's reaping pace, two floats (today and the best day).
// Save 64: the daylight of those two days, two floats more.
// Save 70: built_in, what went into a building, and yard_feed, what the
// yards' beasts ate — a twenty-first column and a sixty-fifth field, each
// predicted before its field was added.
// Save 73: processed and made, what the shops took in and turned out — a
// twenty-third column and a sixty-seventh field, predicted before the build.
// Save 82: WorkKind::kPlanting lengthened work_days_by_kind by one entry — the
// struct's size and arity stay, the section grows 8 bytes; not predicted.
// Save 83: plan_delivered, what went against each position at the turn (boss
// seq 18, item 3) — a twenty-fourth column and a sixty-eighth field.
// Save 86: milk_debt, the debt standing (boss seq 7 of epoch1-5) — 8 bytes
// and a sixty-ninth field, predicted before the build.
// Save 89: the goods loan taken and repaid — a twenty-sixth column and a
// seventy-first field, predicted before the build.
// Save 90: the removals by cause and by kind — three columns, 29 x amounts
// and 74 fields, predicted before the build.
// Save 94 (0.36.5, econ's instruments): hauled_to_stores and the road's
// blocked job-days by kind — a thirtieth column and twelve u32, 76 fields;
// predicted 232 + 29 A -> 280 + 30 A before the build.
static_assert(sizeof(YearLedger) == 280 + (30 * kAmountsSize),
              "YearLedger changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<YearLedger>() == 76,
              "YearLedger gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(VitalsState) == 24, "VitalsState changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<VitalsState>() == 4,
              "VitalsState gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(CalendarState) == 24, "CalendarState changed — update the codec");
static_assert(AggregateArity<CalendarState>() == 6,
              "CalendarState gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-18, save 56: the sky step and its heavy phase, three bytes — both
// tripwires fired, as predicted before the build.
// 2026-09-19, save 72: РАСПУТИЦА, one byte — it sits in the tail padding, so
// only the field count fired, as predicted before the build.
// 0.36.8, save 95: the road beds, three conditions and three floats — 32 -> 48
// and 13 -> 14, both predicted before the build and both held.
static_assert(sizeof(WeatherState) == 48,
              "WeatherState changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<WeatherState>() == 14,
              "WeatherState gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-17, save 49: the night pasture's standing order, its first night and
// its camp took the block from 16 bytes to 24 and from four fields to seven.
// 2026-09-18, save 57: the ration's checkbox, eight fields; it landed in the
// padding beside the night pasture's two bytes, so the size stays 24.
// Save 65: the cancelled day off — its series count (a byte, into the padding)
// and its day (four bytes, which do not fit): 24 -> 32, ten fields.
// Save 69: the season of the last talk, four bytes: 32 -> 36, eleven fields,
// predicted before the field was added and measured after.
// Save 77: the trip to the district — two ticks (eight-aligned, so 36 pads to
// 40), three days, a year, two bytes: 36 -> 72, nineteen fields; 32 bytes in
// the codec. Predicted before the fields were added.
// Save 81: the pencil's deferred summons, a byte at offset 72: 72 -> 80,
// twenty fields; the size was predicted, the arity was not named (a miss).
static_assert(sizeof(ChairmanState) == 80, "ChairmanState changed — update the codec");
static_assert(AggregateArity<ChairmanState>() == 20,
              "ChairmanState gained or lost a field — update the codec and VERSION_SAVE");
// PLANSTATE HAD NO TRIPWIRE AT ALL until 2026-09-12, and it was the only
// serialized block without one: six blocks go into the save, five were
// guarded. A field added to the plan would have been dropped by the codec in
// silence — exactly the case the pair of guards exists for, found on the way
// in to adding two. The size is not asserted as a literal because the struct
// holds two vectors whose size is the standard library's business (the same
// reason YearLedger's assert is written in terms of kAmountsSize).
//
// 2026-09-13: two more fields, and the SIZE DID NOT MOVE AT ALL. The fourth
// byte filled the hole after the three that were there, and the float landed
// on the four bytes of padding the struct already carried to its own
// alignment: +8 before, +8 after. The field count caught both alone — and
// this is the third time in two days that the count was the only one to see
// a change. A size guessed from "a byte plus a float must be twelve" would
// have been wrong here too, which is why it is measured and not reasoned.
// 2026-09-18, save 62: the accumulation limit, a third amounts vector.
// 2026-09-19, save 66: the milk cart's daily share (8 bytes) and the outside
// deliveries, a fourth amounts vector — measured 4 x amounts + 24, 11 fields.
// 2026-09-24, save 85: the milk debt, 8 bytes and a twelfth field, predicted
// before the build.
// Save 89: the goods loan owed and taken, two amounts vectors — 6 x amounts
// + 32 and 14 fields, predicted before the build.
static_assert(sizeof(PlanState) == (6 * kAmountsSize) + 32,
              "PlanState changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<PlanState>() == 14,
              "PlanState gained or lost a field — update the codec and VERSION_SAVE");
// THE CONTAINER ITSELF, and it was the one thing here without a guard.
// Seventeen asserts below watch the BLOCKS of a world and not one watched the
// world: a new top-level member of WorldState — a float beside the chairman,
// say — would simply not be written, and nothing would say so. Every tripwire
// in this file exists because a field once went missing from a stream; this
// is the outermost place that could happen and the last one still open.
//
// The number is the count of WorldState's own members, not of anything
// inside them. Raise it only together with the code that actually carries the
// new member — and that is NOT always this pair of functions: of the eighteen
// members, ten live here, six row tables go through save_rows.cpp, the ledger
// through WriteLedger, and `step_events` is deliberately never written at all.
// The first draft of this note said "a line in WriteWorldBlocks and one in
// ReadWorldBlocks", which would have sent the next reader to the wrong file
// for eight members out of eighteen.
//
// And raise VERSION_SAVE with it, because a member nobody writes is a save
// that silently forgets it.
// 2026-09-13: nineteen — the timber stands, a row table in save_rows.cpp.
// Twenty-one the same night: the limit's points (here, WriteWorldBlocks) and
// its carts (a row table in save_rows.cpp).
// 2026-09-17, save 50: the running total of every point ever granted. BOTH
// tripwires fired — the size and the arity — which is the pair working as
// designed: the size alone would have missed a field that landed in padding,
// and the arity alone would have missed a widened one.
static_assert(sizeof(LimitState) == 8, "LimitState changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<LimitState>() == 2,
              "LimitState gained or lost a field — update the codec and VERSION_SAVE");
// Twenty-two on 2026-09-14: the district's specialists on the road (a row
// table in save_rows.cpp).
// Twenty-three the same day: the couples waiting for a free house.
// Twenty-four the same day: the extraction sites (a row table in
// save_rows.cpp).
// Twenty-five on 2026-09-15: the district's visits on their way (a row table
// in save_rows.cpp).
// Twenty-six the same day: the night trades' outings (a row table in
// save_rows.cpp).
// Twenty-seven the same day: the distillers' month at the stores (a block
// here, WriteWorldBlocks).
// Twenty-eight on 2026-09-15: the district MTS's column (a block here).
// THE SIZE ALONE DOES NOT SEE A FIELD THAT FITS IN THE PADDING, and this
// struct has three bytes of it after `complaint_raised` — exactly the hole
// `rotation_skips_turn` slipped into in save_rows.cpp, where the size said
// "nothing changed" about a byte that had. The arity asks the other question.
// And it asked it on 2026-09-18, save 55, when `dry_months` went into that
// very padding: the size stayed 16, the arity went red, alone.
// Save 60 the same day: dry_months LEFT for the yard (FamilyRow), and the
// leak's month flag, the distillers' vacancy and the settlement's
// alcoholism came — six fields; the size is read off the build.
static_assert(sizeof(NightTheftTally) == 24,
              "NightTheftTally changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<NightTheftTally>() == 6,
              "NightTheftTally gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(MtsColumnState) == 24,
              "MtsColumnState changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<MtsColumnState>() == 7,
              "MtsColumnState gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-17, save 50: the era events that have come (EraEventState).
static_assert(AggregateArity<EraEventState>() == 1,
              "EraEventState gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-17, save 52: readiness for the transition (ReadinessState). Its
// components are structs of structs, so the arity here counts the MEMBERS of
// the top level and the codec below walks the rest by hand; the nested
// tripwires are the two that follow.
static_assert(AggregateArity<ReadinessState>() == 11,
              "ReadinessState gained or lost a member — update the codec and VERSION_SAVE");
// 2026-09-17, save 54: `measured` split off `available` — the era not having
// a component and this year not being able to score it are two facts, and one
// byte for both made the emptiest year score best.
static_assert(AggregateArity<ReadinessComponent>() == 3,
              "ReadinessComponent changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<TransitionBlocks>() == 6,
              "TransitionBlocks gained or lost a block — update the codec and VERSION_SAVE");
// 2026-09-18, save 57: the chairman's issue norms (kSetIssueNorm), 32.
// 2026-09-19, save 68: the sports field's month, 33.
static_assert(sizeof(SportMonth) == 2, "SportMonth changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<SportMonth>() == 2,
              "SportMonth gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-19, save 79: the district's cars, 34 — written and read as their
// own section (save.cpp, district_cars).
// 2026-09-25, save 92: the road network, 35 — its own section (save.cpp,
// roads). 0.36.1: the road index, 36 — DERIVED and deliberately NOT written:
// DecodeWorld builds it from the roads it read (save.cpp).
static_assert(AggregateArity<WorldState>() == 36,
              "WorldState gained or lost a member — write it, read it, and have VERSION_SAVE "
              "raised");

// AND THE COUNT ITSELF, SPELLED OUT — the one number neither tripwire below
// can see. Both of them are written in terms of kFundKindCount: the size
// assert multiplies by it and the arity assert counts the whole array as one
// member, so appending a FIFTH fund would add a fifth vector to the save
// stream and leave both of them green. A guard that adapts to the change it
// exists to catch is no guard at all, and this is the shape aggregate_arity.h
// warns about in its own closing note — whoever puts an array in a state row
// takes its length out of the tripwires' reach.
static_assert(static_cast<std::size_t>(FundKind::kFundKindCount) == 4,
              "a fund was added or removed — the save stream gained or lost a vector, so "
              "update the codec and have VERSION_SAVE raised");
static_assert(sizeof(FundReleaseState) ==
                  static_cast<std::size_t>(FundKind::kFundKindCount) * kAmountsSize,
              "FundReleaseState changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<FundReleaseState>() == 1,
              "FundReleaseState gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(RngState) == 16, "RngState changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<RngState>() == 2,
              "RngState gained or lost a field — update the codec and VERSION_SAVE");

constexpr std::uint8_t kMinEpoch = static_cast<std::uint8_t>(Epoch::kOne);

constexpr std::uint8_t kMaxEpoch = static_cast<std::uint8_t>(Epoch::kEpochEnd) - 1;

constexpr std::uint8_t kMaxPrecipitation = static_cast<std::uint8_t>(Precipitation::kSnow);

constexpr std::uint8_t kMaxPlanVerdict =
    static_cast<std::uint8_t>(PlanVerdict::kPlanVerdictCount) - 1;

// The two names the day carries beside its numbers (world_state.h). Bounded
// by the LAST VALUE and not by the count, the way every other enum here is:
// the terminator is not a value and a save carrying it is a corrupt save.
constexpr std::uint8_t kMaxPhenomenon =
    static_cast<std::uint8_t>(WeatherPhenomenon::kWeatherPhenomenonCount) - 1;

constexpr std::uint8_t kMaxWindBand = static_cast<std::uint8_t>(WindBand::kWindBandCount) - 1;

// The sky's five steps and its heavy phase (save format 56): an hour of the
// day for the start, and at most the whole day for the length.
constexpr std::uint8_t kMaxSkyStep = static_cast<std::uint8_t>(SkyStep::kSkyStepCount) - 1;
constexpr std::uint8_t kLastHour = 23;
constexpr std::uint8_t kHoursInDay = 24;

constexpr std::uint8_t kMaxWeekday = static_cast<std::uint8_t>(Weekday::kSunday);

/// A fixed-size float array goes out as its elements, with no count: the
/// length is part of the format, not of the data.
void WriteFloatArray(ByteWriter& out, std::span<const float> values) {
  for (const float value : values) {
    out.WriteFloat(value);
  }
}

void ReadFloatArray(ByteReader& in, std::span<float> values) {
  for (float& value : values) {
    value = in.ReadFloat();
  }
}

void WriteYearLedger(SaveSink& sink, const YearLedger& book) {
  ByteWriter& out = sink.Out();
  out.WriteU16(book.year);

  out.WriteU32(book.births);
  out.WriteU32(book.deaths);
  out.WriteU32(book.arrivals);
  out.WriteU32(book.departures);
  out.WriteU32(book.weddings);

  out.WriteFloat(book.satiety_day_mean_sum);
  out.WriteU32(book.satiety_days);
  out.WriteFloat(book.satiety_day_mean_min);
  out.WriteU32(book.hungry_at_once_max);

  sink.WriteAmounts(DefKind::kResource, book.issued);
  sink.WriteAmounts(DefKind::kResource, book.ration);
  sink.WriteAmounts(DefKind::kResource, book.nets);
  sink.WriteAmounts(DefKind::kResource, book.night_catch);
  sink.WriteAmounts(DefKind::kResource, book.stolen);
  sink.WriteAmounts(DefKind::kResource, book.samogon_paid);  // save 60
  sink.WriteAmounts(DefKind::kResource, book.yard_produce);
  sink.WriteAmounts(DefKind::kResource, book.plot_harvest);
  sink.WriteAmounts(DefKind::kResource, book.eaten);

  sink.WriteAmounts(DefKind::kResource, book.harvest);
  sink.WriteAmounts(DefKind::kResource, book.hauled_to_stores);  // save 94
  sink.WriteAmounts(DefKind::kResource, book.lost_no_room);
  sink.WriteAmounts(DefKind::kResource, book.lost_to_snow);  // save 61
  sink.WriteAmounts(DefKind::kResource, book.seized);        // save 62
  sink.WriteAmounts(DefKind::kResource, book.spoiled);
  sink.WriteAmounts(DefKind::kResource, book.built_in);   // save 70
  sink.WriteAmounts(DefKind::kResource, book.yard_feed);  // save 70
  sink.WriteAmounts(DefKind::kResource, book.processed);  // save 73
  sink.WriteAmounts(DefKind::kResource, book.made);       // save 73
  sink.WriteAmounts(DefKind::kResource, book.seed);
  out.WriteFloat(book.area_sown_ha);
  out.WriteFloat(book.area_harvested_ha);
  out.WriteFloat(book.area_lost_ha);
  out.WriteFloat(book.area_manured_ha);
  out.WriteI64(book.manure_plowed_in);

  sink.WriteAmounts(DefKind::kResource, book.herd_produce);
  sink.WriteAmounts(DefKind::kResource, book.feed);
  out.WriteU32(book.herd_births);
  out.WriteU32(book.herd_deaths_age);
  out.WriteU32(book.herd_deaths_hunger);
  out.WriteU32(book.herd_culled);
  // The same removals by cause and by kind (save 90), against the livestock
  // dictionary: a reordered livestock.csv reads back by key.
  sink.WriteAmounts(DefKind::kLivestock, book.herd_males_culled);
  sink.WriteAmounts(DefKind::kLivestock, book.herd_surplus_slaughtered);
  sink.WriteAmounts(DefKind::kLivestock, book.herd_autumn_slaughtered);
  out.WriteFloat(book.herd_hungry_head_days);

  sink.WriteAmounts(DefKind::kResource, book.delivered);
  // What the district asked, by position (save 58, M12).
  sink.WriteAmounts(DefKind::kResource, book.plan_due);
  // What went against each position (save 83).
  sink.WriteAmounts(DefKind::kResource, book.plan_delivered);
  out.WriteI64(book.milk_debt);                                   // save 86
  sink.WriteAmounts(DefKind::kResource, book.goods_loan_taken);   // save 89
  sink.WriteAmounts(DefKind::kResource, book.goods_loan_repaid);  // save 89

  WriteFloatArray(out, book.work_days_by_kind);
  for (const std::uint32_t days : book.road_blocked_job_days) {  // save 94
    out.WriteU32(days);
  }
  out.WriteI32(book.trudodni_accrued);
  out.WriteI32(book.trudodni_burned);
  // The season's reaping pace (save 63; the best day's bytes carry the last
  // day since 2026-09-19 — same place, same width).
  out.WriteFloat(book.reaping_today);
  out.WriteFloat(book.reaping_last_day);
  // And the daylight they were reaped under (save 64).
  out.WriteFloat(book.reaping_today_daylight);
  out.WriteFloat(book.reaping_last_day_daylight);
  out.WriteU32(book.walk_offs);
  out.WriteFloat(book.horse_backed_assignment_days);
  out.WriteFloat(book.total_assignment_days);
  out.WriteI32(book.limit_points_granted);
  out.WriteI32(book.limit_points_spent);
  out.WriteI32(book.limit_points_burned);
  out.WriteFloat(book.satisfaction_sum);
  out.WriteU32(book.satisfaction_samples);
  out.WriteFloat(book.able_bodied_days);
  out.WriteFloat(book.plan_percent);
  out.WriteU8(book.plan_percent_known);
  out.WriteFloat(book.worst_season_variety);
  out.WriteU8(book.variety_seasons_seen);
  out.WriteFloat(book.food_days_dec1);
  out.WriteFloat(book.feed_days_dec1);
  out.WriteU16(book.winter_days_dec1);
  out.WriteU8(book.winter_cover_taken);
}

YearLedger ReadYearLedger(LoadSource& source) {
  ByteReader& in = source.In();
  YearLedger book;
  book.year = in.ReadU16();

  book.births = in.ReadU32();
  book.deaths = in.ReadU32();
  book.arrivals = in.ReadU32();
  book.departures = in.ReadU32();
  book.weddings = in.ReadU32();

  book.satiety_day_mean_sum = in.ReadFloat();
  book.satiety_days = in.ReadU32();
  book.satiety_day_mean_min = in.ReadFloat();
  book.hungry_at_once_max = in.ReadU32();

  book.issued = source.ReadAmounts(DefKind::kResource);
  book.ration = source.ReadAmounts(DefKind::kResource);
  book.nets = source.ReadAmounts(DefKind::kResource);
  book.night_catch = source.ReadAmounts(DefKind::kResource);
  book.stolen = source.ReadAmounts(DefKind::kResource);
  book.samogon_paid = source.ReadAmounts(DefKind::kResource);
  book.yard_produce = source.ReadAmounts(DefKind::kResource);
  book.plot_harvest = source.ReadAmounts(DefKind::kResource);
  book.eaten = source.ReadAmounts(DefKind::kResource);

  book.harvest = source.ReadAmounts(DefKind::kResource);
  book.hauled_to_stores = source.ReadAmounts(DefKind::kResource);  // save 94
  for (const Grams grams : book.hauled_to_stores) {
    if (grams < 0) {
      source.Fail("the book's hauled-in column is negative");
    }
  }
  book.lost_no_room = source.ReadAmounts(DefKind::kResource);
  book.lost_to_snow = source.ReadAmounts(DefKind::kResource);
  book.seized = source.ReadAmounts(DefKind::kResource);
  book.spoiled = source.ReadAmounts(DefKind::kResource);
  book.built_in = source.ReadAmounts(DefKind::kResource);
  book.yard_feed = source.ReadAmounts(DefKind::kResource);
  book.processed = source.ReadAmounts(DefKind::kResource);
  book.made = source.ReadAmounts(DefKind::kResource);
  book.seed = source.ReadAmounts(DefKind::kResource);
  book.area_sown_ha = in.ReadFloat();
  book.area_harvested_ha = in.ReadFloat();
  book.area_lost_ha = in.ReadFloat();
  book.area_manured_ha = in.ReadFloat();
  book.manure_plowed_in = in.ReadI64();

  book.herd_produce = source.ReadAmounts(DefKind::kResource);
  book.feed = source.ReadAmounts(DefKind::kResource);
  book.herd_births = in.ReadU32();
  book.herd_deaths_age = in.ReadU32();
  book.herd_deaths_hunger = in.ReadU32();
  book.herd_culled = in.ReadU32();
  book.herd_males_culled = source.ReadAmounts(DefKind::kLivestock);         // save 90
  book.herd_surplus_slaughtered = source.ReadAmounts(DefKind::kLivestock);  // save 90
  book.herd_autumn_slaughtered = source.ReadAmounts(DefKind::kLivestock);   // save 90
  for (const ResourceAmounts* column :
       {&book.herd_males_culled, &book.herd_surplus_slaughtered, &book.herd_autumn_slaughtered}) {
    for (const Grams heads : *column) {
      if (heads < 0) {
        source.Fail("the book's removals by kind are negative");
      }
    }
  }
  book.herd_hungry_head_days = in.ReadFloat();

  book.delivered = source.ReadAmounts(DefKind::kResource);
  book.plan_due = source.ReadAmounts(DefKind::kResource);
  book.plan_delivered = source.ReadAmounts(DefKind::kResource);
  book.milk_debt = in.ReadI64();
  if (book.milk_debt < 0) {
    source.Fail("the book's milk debt is negative");
  }
  book.goods_loan_taken = source.ReadAmounts(DefKind::kResource);   // save 89
  book.goods_loan_repaid = source.ReadAmounts(DefKind::kResource);  // save 89
  for (const ResourceAmounts* column : {&book.goods_loan_taken, &book.goods_loan_repaid}) {
    for (const Grams grams : *column) {
      if (grams < 0) {
        source.Fail("the book's goods loan column is negative");
      }
    }
  }

  ReadFloatArray(in, book.work_days_by_kind);
  for (std::uint32_t& days : book.road_blocked_job_days) {  // save 94
    days = in.ReadU32();
  }
  book.trudodni_accrued = in.ReadI32();
  book.trudodni_burned = in.ReadI32();
  book.reaping_today = in.ReadFloat();
  book.reaping_last_day = in.ReadFloat();
  book.reaping_today_daylight = in.ReadFloat();
  book.reaping_last_day_daylight = in.ReadFloat();
  book.walk_offs = in.ReadU32();
  book.horse_backed_assignment_days = in.ReadFloat();
  book.total_assignment_days = in.ReadFloat();
  book.limit_points_granted = in.ReadI32();
  book.limit_points_spent = in.ReadI32();
  book.limit_points_burned = in.ReadI32();
  book.satisfaction_sum = in.ReadFloat();
  book.satisfaction_samples = in.ReadU32();
  book.able_bodied_days = in.ReadFloat();
  book.plan_percent = in.ReadFloat();
  book.plan_percent_known = in.ReadU8();
  book.worst_season_variety = in.ReadFloat();
  book.variety_seasons_seen = in.ReadU8();
  book.food_days_dec1 = in.ReadFloat();
  book.feed_days_dec1 = in.ReadFloat();
  book.winter_days_dec1 = in.ReadU16();
  book.winter_cover_taken = in.ReadU8();
  return book;
}

/// One scored component: the number and the byte that says the era has it.
/// The byte is range-checked like every other 0/1 of the record — a byte that
/// is neither refuses the file rather than being read as "available,
/// probably", which for this field would turn a component the era does not
/// have into a nought weighed at its full weight.
void WriteComponent(ByteWriter& out, const ReadinessComponent& component) {
  out.WriteFloat(component.score);
  out.WriteU8(component.available);
  out.WriteU8(component.measured);
}

void ReadComponent(LoadSource& source, ReadinessComponent& component) {
  component.score = source.In().ReadFloat();
  component.available = source.ReadEnumValue(0, 1, "readiness component available");
  component.measured = source.ReadEnumValue(0, 1, "readiness component measured");
}

void WriteReadiness(ByteWriter& out, const ReadinessState& readiness) {
  out.WriteU16(readiness.year);
  WriteComponent(out, readiness.economy.plan);
  WriteComponent(out, readiness.economy.winter_stocks);
  WriteComponent(out, readiness.economy.mechanisation);
  WriteComponent(out, readiness.economy.funds);
  WriteComponent(out, readiness.society.satisfaction);
  WriteComponent(out, readiness.society.kolkhoz_effort);
  WriteComponent(out, readiness.society.social_objects);
  WriteComponent(out, readiness.society.demography);
  out.WriteFloat(readiness.economic_index);
  out.WriteFloat(readiness.social_index);
  out.WriteU8(readiness.both_above_run);
  out.WriteU8(readiness.wintering_run);
  for (const float year : readiness.plan_percent_years) {
    out.WriteFloat(year);
  }
  out.WriteU8(readiness.plan_years_filled);
  out.WriteU8(readiness.blocks.food_variety);
  out.WriteU8(readiness.blocks.social_objects);
  out.WriteU8(readiness.blocks.own_traction);
  out.WriteU8(readiness.blocks.wintering_two_years);
  out.WriteU8(readiness.blocks.units_at_level);
  out.WriteU8(readiness.blocks.office_repaired);
  out.WriteFloat(readiness.satisfaction_stub_points);
}

void ReadReadiness(LoadSource& source, ReadinessState& readiness) {
  ByteReader& in = source.In();
  readiness.year = in.ReadU16();
  ReadComponent(source, readiness.economy.plan);
  ReadComponent(source, readiness.economy.winter_stocks);
  ReadComponent(source, readiness.economy.mechanisation);
  ReadComponent(source, readiness.economy.funds);
  ReadComponent(source, readiness.society.satisfaction);
  ReadComponent(source, readiness.society.kolkhoz_effort);
  ReadComponent(source, readiness.society.social_objects);
  ReadComponent(source, readiness.society.demography);
  readiness.economic_index = in.ReadFloat();
  readiness.social_index = in.ReadFloat();
  readiness.both_above_run = in.ReadU8();
  readiness.wintering_run = in.ReadU8();
  for (float& year : readiness.plan_percent_years) {
    year = in.ReadFloat();
  }
  // The ring's fill, range-checked: a count above three would read past the
  // array on the very next score, and a corrupt byte must refuse the file
  // rather than be trusted to be small.
  readiness.plan_years_filled = source.ReadEnumValue(0, 3, "plan years filled");
  readiness.blocks.food_variety = source.ReadEnumValue(0, 1, "food variety block");
  readiness.blocks.social_objects = source.ReadEnumValue(0, 1, "social objects block");
  readiness.blocks.own_traction = source.ReadEnumValue(0, 1, "own traction block");
  readiness.blocks.wintering_two_years = source.ReadEnumValue(0, 1, "wintering block");
  readiness.blocks.units_at_level = source.ReadEnumValue(0, 1, "units at level block");
  readiness.blocks.office_repaired = source.ReadEnumValue(0, 1, "office repaired block");
  readiness.satisfaction_stub_points = in.ReadFloat();
}

}  // namespace

void WriteWorldBlocks(SaveSink& sink, const WorldState& world) {
  ByteWriter& out = sink.Out();

  // CalendarState — calendar.h. The caches go out with the rest so that
  // every field of the struct is accounted for; the reader re-derives them.
  out.WriteU64(world.calendar.tick);
  out.WriteU32(world.calendar.day);
  out.WriteU16(world.calendar.date.year);
  out.WriteU8(static_cast<std::uint8_t>(world.calendar.date.month));
  out.WriteU8(world.calendar.date.day_in_month);
  out.WriteU8(static_cast<std::uint8_t>(world.calendar.weekday));
  out.WriteU8(static_cast<std::uint8_t>(world.calendar.season));
  out.WriteU8(static_cast<std::uint8_t>(world.calendar.day_zero_weekday));

  // WeatherState — world_state.h.
  out.WriteFloat(world.weather.air_temperature_celsius);
  out.WriteFloat(world.weather.daylight_hours);
  out.WriteU8(static_cast<std::uint8_t>(world.weather.precipitation));
  // The day's swing and the day's sky (the cloud parcel, 2026-09-04). Both
  // are recomputable from (seed, day), and both are saved anyway for the
  // same reason the temperature is: a loaded world must be able to answer
  // before it has stepped once.
  out.WriteFloat(world.weather.temperature_swing_celsius);
  out.WriteFloat(world.weather.cloud_cover);
  // The day's SKY STEP and its heavy phase (save format 56, 2026-09-18).
  // Recomputable like the rest, saved for the same reason.
  out.WriteU8(static_cast<std::uint8_t>(world.weather.sky));
  out.WriteU8(world.weather.heavy_from_hour);
  out.WriteU8(world.weather.heavy_hours);
  // The day's NAME and the day's wind band (the wind parcel, 2026-09-05).
  // Recomputable from (seed, day) like everything above them, and saved for
  // the same reason: a loaded world must be able to answer before it has
  // stepped once.
  out.WriteU8(static_cast<std::uint8_t>(world.weather.phenomenon));
  out.WriteU8(static_cast<std::uint8_t>(world.weather.wind));
  // How many days the snow has lain (the settled-snow parcel, 2026-09-05).
  // THE ONLY WEATHER FIELD THAT IS NOT RECOMPUTABLE from (seed, day): a
  // cover is history, so losing it here would lose it for good, and a
  // loaded January would show bare ground until the next snowfall.
  out.WriteU16(world.weather.snow_cover_days);
  // Whether a cover has lain since the last leaf fall (2026-09-06). NOT
  // recomputable either, and for a sharper reason than the count above it:
  // it separates the count's TWO ZEROS — no snow yet, and snow that melted —
  // which is exactly the pair a loaded world cannot rediscover by looking at
  // today.
  out.WriteU8(world.weather.cover_since_leaf_fall ? 1U : 0U);
  // РАСПУТИЦА (save 72, boss seq 186). Recomputable from (seed, day) like the
  // sky, and saved for the sky's reason.
  out.WriteU8(world.weather.mud ? 1U : 0U);
  // THE BEDS (save 95, roads delivery 3): each bed's condition and the days
  // it stays wet. History like the cover — a bed is wet because it rained —
  // so not recomputable from (seed, day), and lost for good if not saved.
  for (std::size_t bed = 0; bed < kRoadBedCountValue; ++bed) {
    out.WriteU8(static_cast<std::uint8_t>(world.weather.road_beds.condition[bed]));
    out.WriteFloat(world.weather.road_beds.wet_days_left[bed]);
  }

  out.WriteU8(static_cast<std::uint8_t>(world.epoch));
  out.WriteU64(world.world_seed);

  out.WriteU64(world.rng.state);
  out.WriteU64(world.rng.stream);

  out.WriteFloat(world.chairman.raikom_reputation);
  out.WriteFloat(world.chairman.authority);
  out.WriteFloat(world.chairman.shadow_reputation);
  out.WriteU8(world.chairman.horses_stabled);
  // The night pasture (save format 49): the standing order, whether it has
  // ever begun, and the camp the children keep. The place is written even
  // when no order stands — a Vec2 of zeroes is shorter to say than a rule
  // about when to write it.
  out.WriteU8(world.chairman.night_pasture_ordered);
  out.WriteU8(world.chairman.night_pasture_begun);
  out.WriteFloat(world.chairman.night_pasture_place.x);
  out.WriteFloat(world.chairman.night_pasture_place.y);
  // The ration's checkbox (save 57): labor-payment §5, kSetRation.
  out.WriteU8(world.chairman.ration_auto);
  // The cancelled day off (save 65): the series and the day, 0 for none.
  out.WriteU8(world.chairman.days_off_cancelled_in_a_row);
  out.WriteU32(world.chairman.cancelled_day_off);
  // The chairman's talk (save 69): the season of the last one, plus one.
  out.WriteU32(world.chairman.last_talk_season);
  // The trip to the district (save 77).
  out.WriteU64(world.chairman.away_from_tick);
  out.WriteU64(world.chairman.away_until_tick);
  out.WriteU32(world.chairman.last_trip_day);
  out.WriteU32(world.chairman.summon_letter_day);
  out.WriteU32(world.chairman.summon_day);
  out.WriteU16(world.chairman.plan_traded_year);
  out.WriteU8(world.chairman.summon_cause);
  out.WriteU8(world.chairman.away_summoned);
  // The pencil's deferred summons (save 81).
  out.WriteU8(world.chairman.pencil_pending);

  out.WriteFloat(world.traction_ration);
  // The chairman's issue norms (save 57, kSetIssueNorm), through the
  // resource dictionary like every amounts vector; empty until his first
  // order, and empty round-trips as empty.
  sink.WriteAmounts(DefKind::kResource, world.issue_norms);
  sink.WriteAmounts(DefKind::kResource, world.plan.due);
  sink.WriteAmounts(DefKind::kResource, world.plan.delivered);
  sink.WriteAmounts(DefKind::kResource, world.plan.accumulation_limit);  // save 62
  // The milk cart's daily share and what went with no position (save 66).
  out.WriteI64(world.plan.milk_daily_share);
  out.WriteI64(world.plan.milk_debt);  // save 85: the milk with debt
  sink.WriteAmounts(DefKind::kResource, world.plan.delivered_outside);
  sink.WriteAmounts(DefKind::kResource, world.plan.goods_loan_owed);   // save 89
  sink.WriteAmounts(DefKind::kResource, world.plan.goods_loan_taken);  // save 89
  out.WriteU8(static_cast<std::uint8_t>(world.plan.last_verdict));
  out.WriteU8(world.plan.failed_years_in_a_row);
  out.WriteU8(world.plan.met_years_in_a_row);
  // WHETHER THE DISTRICT HAS SPOKEN THIS YEAR, and the area its next figure
  // will be computed from. Lose the first and a loaded campaign cannot tell
  // "asked for nothing" from "was never asked"; lose the second and the
  // spring after a load names a norm off an area of zero — the plan the
  // chairman cannot fail, which is the defect this pair exists to end.
  out.WriteU8(world.plan.announced);
  out.WriteFloat(world.plan.worked_ha_last_year);
  out.WriteFloat(world.plan.worked_ha_this_year);
  for (const ResourceAmounts& opened : world.unsealed.by_fund) {
    sink.WriteAmounts(DefKind::kResource, opened);
  }

  out.WriteFloat(world.vitals.life_expectancy_years);
  WriteFloatArray(out, world.vitals.satiety_year_means);
  out.WriteFloat(world.vitals.satiety_running_sum);
  out.WriteU32(world.vitals.satiety_running_days);

  // The limit's points left this year (district design §1, save format 31),
  // and beside it every point ever granted (save format 50): the design's
  // measure of how far the farm has come, and the first thing weighed against
  // it is electrification. It is stored rather than summed because nothing in
  // the books keeps it — `closed` is one year and a chronicle year carries no
  // points at all.
  out.WriteU32(static_cast<std::uint32_t>(world.limit.points));
  out.WriteU32(static_cast<std::uint32_t>(world.limit.points_granted_total));

  // The distillers' month at the stores (crime design §7, save format 42).
  out.WriteU64(static_cast<std::uint64_t>(world.night_theft.stolen_this_month));
  out.WriteU32(world.night_theft.month_index);
  out.WriteU8(world.night_theft.complaint_raised);
  // Save 60: the month's open leak, the distillers' vacancy, the
  // settlement's alcoholism (the sobriety's clock moved to the yard).
  out.WriteU8(world.night_theft.leak_open_this_month);
  out.WriteU32(world.night_theft.distiller_short_since);
  out.WriteFloat(world.night_theft.settlement_alcoholism);
  // The sports field's month (save 68): its open days and yesterday's downpour.
  out.WriteU8(world.sport_month.open_days);
  out.WriteU8(world.sport_month.downpour_yesterday);

  // The district MTS's column of this season (MTS design §1, save format 46).
  out.WriteU8(static_cast<std::uint8_t>(world.mts_column.phase));
  sink.WriteDefId(DefKind::kLimitLot, world.mts_column.lot.value);
  out.WriteU32(world.mts_column.arrive_day);
  out.WriteU32(world.mts_column.camp.value);
  out.WriteFloat(world.mts_column.worked_ha);
  // The field begun and its hectares (save format 47).
  out.WriteU32(world.mts_column.field.value);
  out.WriteFloat(world.mts_column.field_ha);

  // The era events that have come (epochs design §14, save format 50).
  out.WriteU8(world.era_events.electrification_unlocked);

  // Readiness for the transition (epochs design §6, save format 52). The
  // indices could be recomputed from a closed year; the RUNS could not —
  // "three years running" is what the campaign accumulated, and that is the
  // whole reason this is state rather than a light stood up again on load.
  WriteReadiness(out, world.readiness);
}

void ReadWorldBlocks(LoadSource& source, WorldState* world) {
  ByteReader& in = source.In();

  world->calendar.tick = in.ReadU64();
  world->calendar.day = in.ReadU32();
  world->calendar.date.year = in.ReadU16();
  world->calendar.date.month = static_cast<Month>(in.ReadU8());
  world->calendar.date.day_in_month = in.ReadU8();
  world->calendar.weekday = static_cast<Weekday>(in.ReadU8());
  world->calendar.season = static_cast<Season>(in.ReadU8());
  world->calendar.day_zero_weekday =
      static_cast<Weekday>(source.ReadEnumValue(0, kMaxWeekday, "day-zero weekday"));
  // The four cached fields above are derived data (calendar.h): re-deriving
  // them from the tick is both cheaper than validating them and stricter —
  // an inconsistent date cannot reach a phase.
  RefreshCalendarCaches(world->calendar);

  world->weather.air_temperature_celsius = in.ReadFloat();
  world->weather.daylight_hours = in.ReadFloat();
  world->weather.precipitation =
      static_cast<Precipitation>(source.ReadEnumValue(0, kMaxPrecipitation, "precipitation"));
  world->weather.temperature_swing_celsius = in.ReadFloat();
  world->weather.cloud_cover = in.ReadFloat();
  world->weather.sky = static_cast<SkyStep>(source.ReadEnumValue(0, kMaxSkyStep, "sky step"));
  world->weather.heavy_from_hour =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, kLastHour, "heavy phase start"));
  world->weather.heavy_hours =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, kHoursInDay, "heavy phase hours"));
  world->weather.phenomenon =
      static_cast<WeatherPhenomenon>(source.ReadEnumValue(0, kMaxPhenomenon, "weather phenomenon"));
  world->weather.wind = static_cast<WindBand>(source.ReadEnumValue(0, kMaxWindBand, "wind band"));
  world->weather.snow_cover_days = in.ReadU16();
  // Range-checked like every other narrow field: a byte that is neither 0
  // nor 1 is a corrupt save, not a truthy value.
  world->weather.cover_since_leaf_fall = source.ReadEnumValue(0, 1, "cover since leaf fall") != 0;
  world->weather.mud = source.ReadEnumValue(0, 1, "mud season") != 0;
  for (std::size_t bed = 0; bed < kRoadBedCountValue; ++bed) {
    world->weather.road_beds.condition[bed] = static_cast<RoadCondition>(source.ReadEnumValue(
        0,
        static_cast<std::uint8_t>(static_cast<std::uint8_t>(RoadCondition::kRoadConditionCount) -
                                  1U),
        "road bed"));
    world->weather.road_beds.wet_days_left[bed] = in.ReadFloat();
  }

  world->epoch = static_cast<Epoch>(source.ReadEnumValue(kMinEpoch, kMaxEpoch, "epoch"));
  world->world_seed = in.ReadU64();

  world->rng.state = in.ReadU64();
  world->rng.stream = in.ReadU64();

  world->chairman.raikom_reputation = in.ReadFloat();
  world->chairman.authority = in.ReadFloat();
  world->chairman.shadow_reputation = in.ReadFloat();
  world->chairman.horses_stabled = in.ReadU8();
  world->chairman.night_pasture_ordered = in.ReadU8();
  world->chairman.night_pasture_begun = in.ReadU8();
  world->chairman.night_pasture_place.x = in.ReadFloat();
  world->chairman.night_pasture_place.y = in.ReadFloat();
  world->chairman.ration_auto =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "the ration's checkbox"));
  world->chairman.days_off_cancelled_in_a_row = in.ReadU8();
  world->chairman.cancelled_day_off = in.ReadU32();
  world->chairman.last_talk_season = in.ReadU32();
  world->chairman.away_from_tick = in.ReadU64();
  world->chairman.away_until_tick = in.ReadU64();
  world->chairman.last_trip_day = in.ReadU32();
  world->chairman.summon_letter_day = in.ReadU32();
  world->chairman.summon_day = in.ReadU32();
  world->chairman.plan_traded_year = in.ReadU16();
  world->chairman.summon_cause = source.ReadEnumValue(
      0,
      static_cast<std::uint8_t>(static_cast<std::uint8_t>(SummonCause::kSummonCauseCount) - 1),
      "the summons' cause");
  world->chairman.away_summoned = source.ReadEnumValue(0, 1, "the summons' mark");
  world->chairman.pencil_pending = source.ReadEnumValue(0, 1, "the pencil's deferred summons");

  world->traction_ration = in.ReadFloat();
  world->issue_norms = source.ReadAmounts(DefKind::kResource);
  world->plan.due = source.ReadAmounts(DefKind::kResource);
  world->plan.delivered = source.ReadAmounts(DefKind::kResource);
  world->plan.accumulation_limit = source.ReadAmounts(DefKind::kResource);
  world->plan.milk_daily_share = in.ReadI64();
  world->plan.milk_debt = in.ReadI64();
  // A NEGATIVE SHARE OR DEBT IS NO STATE the simulation makes (the debt is
  // wanted - taken, never below nought). Taken as it stands, it would ask the
  // stores for a negative amount at the next milking, so the load refuses it.
  if (world->plan.milk_daily_share < 0 || world->plan.milk_debt < 0) {
    source.Fail("the milk cart's share or debt is negative");
  }
  world->plan.delivered_outside = source.ReadAmounts(DefKind::kResource);
  world->plan.goods_loan_owed = source.ReadAmounts(DefKind::kResource);   // save 89
  world->plan.goods_loan_taken = source.ReadAmounts(DefKind::kResource);  // save 89
  // A NEGATIVE LOAN IS NO STATE the simulation makes: owed is paid down to
  // nought and no further, and taken only grows within a year.
  for (std::size_t resource = 0; resource < world->plan.goods_loan_owed.size(); ++resource) {
    if (world->plan.goods_loan_owed[resource] < 0) {
      source.Fail("the goods loan owed is negative");
    }
  }
  for (std::size_t resource = 0; resource < world->plan.goods_loan_taken.size(); ++resource) {
    if (world->plan.goods_loan_taken[resource] < 0) {
      source.Fail("the goods loan taken is negative");
    }
  }
  world->plan.last_verdict =
      static_cast<PlanVerdict>(source.ReadEnumValue(0, kMaxPlanVerdict, "plan verdict"));
  world->plan.failed_years_in_a_row = in.ReadU8();
  world->plan.met_years_in_a_row = in.ReadU8();
  world->plan.announced = in.ReadU8() != 0 ? 1U : 0U;
  world->plan.worked_ha_last_year = in.ReadFloat();
  world->plan.worked_ha_this_year = in.ReadFloat();
  for (ResourceAmounts& opened : world->unsealed.by_fund) {
    opened = source.ReadAmounts(DefKind::kResource);
  }

  world->vitals.life_expectancy_years = in.ReadFloat();
  ReadFloatArray(in, world->vitals.satiety_year_means);
  world->vitals.satiety_running_sum = in.ReadFloat();
  world->vitals.satiety_running_days = in.ReadU32();

  world->limit.points = static_cast<std::int32_t>(in.ReadU32());
  world->limit.points_granted_total = static_cast<std::int32_t>(in.ReadU32());

  world->night_theft.stolen_this_month = static_cast<Grams>(in.ReadU64());
  world->night_theft.month_index = in.ReadU32();
  world->night_theft.complaint_raised = in.ReadU8();
  world->night_theft.leak_open_this_month =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "the month's open leak"));
  world->night_theft.distiller_short_since = in.ReadU32();
  world->night_theft.settlement_alcoholism = in.ReadFloat();
  world->sport_month.open_days = in.ReadU8();
  world->sport_month.downpour_yesterday =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "downpour yesterday"));

  world->mts_column.phase = static_cast<MtsColumnPhase>(
      source.ReadEnumValue(0,
                           static_cast<std::uint32_t>(MtsColumnPhase::kMtsColumnPhaseCount) - 1U,
                           "mts column phase"));
  world->mts_column.lot = LimitLotId{source.ReadDefId(DefKind::kLimitLot)};
  world->mts_column.arrive_day = in.ReadU32();
  world->mts_column.camp = UnitId{in.ReadU32()};
  world->mts_column.worked_ha = in.ReadFloat();
  world->mts_column.field = FieldId{in.ReadU32()};
  world->mts_column.field_ha = in.ReadFloat();

  // Range-checked like the other 0/1 bytes of the record: a byte that is
  // neither refuses the file rather than being read as "true, probably".
  world->era_events.electrification_unlocked =
      source.ReadEnumValue(0, 1, "electrification unlocked");

  ReadReadiness(source, world->readiness);
}

/// A campaign is fifty to seventy years; the ceiling is four orders above
/// that, and it exists for one reason only — a corrupt length must not turn
/// into a reserve() of four billion rows before a single one is read. Same
/// guard the row tables carry (save.cpp, kMaxNextIdValue).
constexpr std::uint32_t kMaxChronicleYears = 1U << 16U;

void WriteLedger(SaveSink& sink, const LedgerState& ledger) {
  WriteYearLedger(sink, ledger.current);
  WriteYearLedger(sink, ledger.closed);
  // The office wall (ledger_state.h, Chronicle). THE ONE HISTORY THE
  // SIMULATION CANNOT REDERIVE — `current` and `closed` are two years and
  // the wall is fifty — so it is saved for the same reason reaped_grams is.
  ByteWriter& out = sink.Out();
  out.WriteU32(static_cast<std::uint32_t>(ledger.chronicle.size()));
  for (const ChronicleYear& year : ledger.chronicle) {
    out.WriteU16(year.year);
    out.WriteU32(year.residents);
    out.WriteFloat(year.fertility);
    out.WriteI64(year.harvest_kcal);
  }
}

LedgerState ReadLedger(LoadSource& source) {
  LedgerState ledger;
  ledger.current = ReadYearLedger(source);
  ledger.closed = ReadYearLedger(source);
  ByteReader& in = source.In();
  const std::uint32_t years = in.ReadU32();
  if (years > kMaxChronicleYears) {
    source.Fail("chronicle length is absurd");
    return ledger;
  }
  ledger.chronicle.reserve(years);
  for (std::uint32_t index = 0; index < years; ++index) {
    ChronicleYear year;
    year.year = in.ReadU16();
    year.residents = in.ReadU32();
    year.fertility = in.ReadFloat();
    year.harvest_kcal = in.ReadI64();
    ledger.chronicle.push_back(year);
  }
  return ledger;
}

}  // namespace core
