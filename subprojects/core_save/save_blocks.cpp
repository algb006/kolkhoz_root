// The scalar world blocks and the ledger (save_blocks.h). Same law as the
// row codecs: declaration order of the defining header, the two directions
// adjacent, a sizeof tripwire per block that has one.

#include "save_blocks.h"

#include <cstddef>
#include <cstdint>
#include <span>

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

static_assert(sizeof(YearLedger) == 136 + (13 * kAmountsSize),
              "YearLedger changed — update the codec and VERSION_SAVE");
static_assert(sizeof(VitalsState) == 24, "VitalsState changed — update the codec and VERSION_SAVE");
static_assert(sizeof(CalendarState) == 24, "CalendarState changed — update the codec");
static_assert(sizeof(WeatherState) == 20, "WeatherState changed — update the codec");
static_assert(sizeof(ChairmanState) == 16, "ChairmanState changed — update the codec");
static_assert(sizeof(RngState) == 16, "RngState changed — update the codec and VERSION_SAVE");

constexpr std::uint8_t kMinEpoch = static_cast<std::uint8_t>(Epoch::kOne);

constexpr std::uint8_t kMaxEpoch = static_cast<std::uint8_t>(Epoch::kEpochEnd) - 1;

constexpr std::uint8_t kMaxPrecipitation = static_cast<std::uint8_t>(Precipitation::kSnow);

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
  sink.WriteAmounts(DefKind::kResource, book.yard_produce);
  sink.WriteAmounts(DefKind::kResource, book.plot_harvest);
  sink.WriteAmounts(DefKind::kResource, book.eaten);

  sink.WriteAmounts(DefKind::kResource, book.harvest);
  sink.WriteAmounts(DefKind::kResource, book.lost_no_room);
  sink.WriteAmounts(DefKind::kResource, book.spoiled);
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
  out.WriteFloat(book.herd_hungry_head_days);

  sink.WriteAmounts(DefKind::kResource, book.delivered);

  WriteFloatArray(out, book.work_days_by_kind);
  out.WriteI32(book.trudodni_accrued);
  out.WriteI32(book.trudodni_burned);
  out.WriteU32(book.walk_offs);
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
  book.yard_produce = source.ReadAmounts(DefKind::kResource);
  book.plot_harvest = source.ReadAmounts(DefKind::kResource);
  book.eaten = source.ReadAmounts(DefKind::kResource);

  book.harvest = source.ReadAmounts(DefKind::kResource);
  book.lost_no_room = source.ReadAmounts(DefKind::kResource);
  book.spoiled = source.ReadAmounts(DefKind::kResource);
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
  book.herd_hungry_head_days = in.ReadFloat();

  book.delivered = source.ReadAmounts(DefKind::kResource);

  ReadFloatArray(in, book.work_days_by_kind);
  book.trudodni_accrued = in.ReadI32();
  book.trudodni_burned = in.ReadI32();
  book.walk_offs = in.ReadU32();
  return book;
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

  out.WriteU8(static_cast<std::uint8_t>(world.epoch));
  out.WriteU64(world.world_seed);

  out.WriteU64(world.rng.state);
  out.WriteU64(world.rng.stream);

  out.WriteFloat(world.chairman.raikom_reputation);
  out.WriteFloat(world.chairman.authority);
  out.WriteFloat(world.chairman.shadow_reputation);
  out.WriteU8(world.chairman.horses_stabled);

  sink.WriteAmounts(DefKind::kResource, world.plan.due);
  sink.WriteAmounts(DefKind::kResource, world.plan.delivered);

  out.WriteFloat(world.vitals.life_expectancy_years);
  WriteFloatArray(out, world.vitals.satiety_year_means);
  out.WriteFloat(world.vitals.satiety_running_sum);
  out.WriteU32(world.vitals.satiety_running_days);
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

  world->epoch = static_cast<Epoch>(source.ReadEnumValue(kMinEpoch, kMaxEpoch, "epoch"));
  world->world_seed = in.ReadU64();

  world->rng.state = in.ReadU64();
  world->rng.stream = in.ReadU64();

  world->chairman.raikom_reputation = in.ReadFloat();
  world->chairman.authority = in.ReadFloat();
  world->chairman.shadow_reputation = in.ReadFloat();
  world->chairman.horses_stabled = in.ReadU8();

  world->plan.due = source.ReadAmounts(DefKind::kResource);
  world->plan.delivered = source.ReadAmounts(DefKind::kResource);

  world->vitals.life_expectancy_years = in.ReadFloat();
  ReadFloatArray(in, world->vitals.satiety_year_means);
  world->vitals.satiety_running_sum = in.ReadFloat();
  world->vitals.satiety_running_days = in.ReadU32();
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
