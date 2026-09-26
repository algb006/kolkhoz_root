// The ledger section (save_ledger.h). Same law as the row codecs and the
// world's blocks: declaration order of the defining header, the two
// directions adjacent, a sizeof tripwire beside the arity.

#include "save_ledger.h"

#include <cstddef>
#include <cstdint>

#include "core_common/aggregate_arity.h"
#include "core_common/ledger_state.h"
#include "save_dictionary.h"
#include "save_stream.h"

namespace core {
namespace {

// A ResourceAmounts is a std::vector, and sizeof(std::vector) belongs to the
// standard library and its debug level, not to the save format, which writes
// the vector element by element (save_blocks.cpp, where the first MSVC build
// taught it). Counting the vectors out leaves the block's own fields.
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
// Save 96 (0.36.9, the produce cart off the road): four float and two gram
// arrays of three sources — 280 -> 376 and 76 -> 82, predicted before the
// build (the float arrays first, so the grams land 8-aligned: no padding).
// Save 97 (0.36.15, the cart's two ends): two more float arrays of three
// sources, the store's end — 376 -> 400 and 82 -> 84, predicted before the
// build (72 bytes of floats before the grams: still 8-aligned).
// Save 98 (0.36.17): the cart's sources 3 -> 4 (the district) — six float and
// two gram arrays each one entry longer: 400 -> 440, fields unmoved; predicted
// before the build.
// Save 100 (delivery 7a): WorkKind::kRoadWork lengthens work_days_by_kind and
// road_blocked_job_days by one entry each — 440 -> 448, fields unmoved,
// predicted off the dumped layout (the next field 8-aligned at 936).
// Save 102 (why not placed): 10 idle reasons, 13 x 4 short job-days and the
// two bases, 64 u32 after road_blocked_job_days — 448 -> 704 and 84 -> 88,
// predicted before the build (256 bytes: the float and gram arrays after
// stay where their alignment wants them).
static_assert(sizeof(YearLedger) == 704 + (30 * kAmountsSize),
              "YearLedger changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<YearLedger>() == 88,
              "YearLedger gained or lost a field — update the codec and VERSION_SAVE");

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
  // Why not placed (save 102), in the struct's order.
  for (const std::uint32_t days : book.idle_person_days) {
    out.WriteU32(days);
  }
  for (const auto& by_reason : book.short_job_days) {
    for (const std::uint32_t days : by_reason) {
      out.WriteU32(days);
    }
  }
  out.WriteU32(book.offered_job_days);
  out.WriteU32(book.candidate_person_days);
  // The produce cart off the road (save 96), in the struct's order.
  WriteFloatArray(out, book.cart_trips);
  WriteFloatArray(out, book.cart_off_road_m);
  WriteFloatArray(out, book.cart_off_road_worst_m);
  WriteFloatArray(out, book.cart_trips_off_road);
  WriteFloatArray(out, book.cart_store_off_road_m);        // save 97
  WriteFloatArray(out, book.cart_store_off_road_worst_m);  // save 97
  for (const Grams grams : book.cart_grams) {
    out.WriteI64(grams);
  }
  for (const Grams grams : book.cart_grams_off_road) {
    out.WriteI64(grams);
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
  for (std::uint32_t& days : book.idle_person_days) {  // save 102
    days = in.ReadU32();
  }
  for (auto& by_reason : book.short_job_days) {
    for (std::uint32_t& days : by_reason) {
      days = in.ReadU32();
    }
  }
  book.offered_job_days = in.ReadU32();
  book.candidate_person_days = in.ReadU32();
  ReadFloatArray(in, book.cart_trips);  // save 96
  ReadFloatArray(in, book.cart_off_road_m);
  ReadFloatArray(in, book.cart_off_road_worst_m);
  ReadFloatArray(in, book.cart_trips_off_road);
  ReadFloatArray(in, book.cart_store_off_road_m);        // save 97
  ReadFloatArray(in, book.cart_store_off_road_worst_m);  // save 97
  for (Grams& grams : book.cart_grams) {
    grams = in.ReadI64();
  }
  for (Grams& grams : book.cart_grams_off_road) {
    grams = in.ReadI64();
  }
  for (std::size_t origin = 0; origin < kCartLoadSourceCountValue; ++origin) {
    if (book.cart_grams[origin] < 0 || book.cart_grams_off_road[origin] < 0 ||
        book.cart_grams_off_road[origin] > book.cart_grams[origin]) {
      source.Fail(
          "the book's produce cart column is negative or carries more beyond the road "
          "than it carried");
    }
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

/// A campaign is fifty to seventy years; the ceiling is four orders above
/// that, and it exists for one reason only — a corrupt length must not turn
/// into a reserve() of four billion rows before a single one is read. Same
/// guard the row tables carry (save.cpp, kMaxNextIdValue).
constexpr std::uint32_t kMaxChronicleYears = 1U << 16U;

}  // namespace

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
