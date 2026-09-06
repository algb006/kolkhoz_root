/// @file
/// @brief FieldRow — the per-field state: land, crop, phase, fertility.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::fields under the double-buffer discipline: the
/// field is a unit of parallelism of the production phase (phase 4), so a
/// worker owns whole field rows; structural transitions — sowing, harvest,
/// loss, contour changes — happen only in the sequential production
/// decisions sub-step.
///
/// Design sources: farming design §2 (fertility 0-100 per field), §4-§5
/// (windows, temperature thresholds, the six phases), §6 (snow is the one
/// total loss), §7 (three-year rotation), §8 (manure is plowed in).
///
/// Fertility scale: 0-100, 50 is the neutral soil factor (yield x1.0), so
/// the soil multiplier is fertility / 50 — the reference runs' starting
/// multiplier 1.3 is fertility 65. The scale choice is the core's
/// (manual/64-land-model.md); what changes fertility is the design's.
///
/// Phase durations are labor-driven by design (§5): work_days_remaining is
/// the seam between production and labor (manual/65-labor-model.md).
/// Production opens a working phase by setting it to the phase's norm
/// (area x man-days per hectare); the labor sub-step drains it with the
/// crew's hourly output; production advances the phase when it reaches 0.
/// The two modules never call each other — the state carries the contract.

#ifndef CORE_COMMON_LAND_STATE_H_
#define CORE_COMMON_LAND_STATE_H_

#include <cstdint>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief The six field phases (farming design §5). One phase at a time;
/// they cycle and never skip.
enum class FieldPhase : std::uint8_t {
  kIdle = 0,  ///< Stubble, fallow or waiting. Contour edits allowed here only.
  kPlowing,   ///< Manure is plowed in here, never spread separately.
  kHarrowing,
  kSowing,   ///< Consumes ordinary grain/potatoes of the crop (§7).
  kGrowing,  ///< No field work; winter crops sit out the winter here.
  kHarvest,  ///< The heaviest phase of the year.

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kFieldPhaseCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kFieldPhaseCount,
};

/// @brief What kind of land the row is (boss answer to question Q6,
/// 2026-08-31). A MEADOW IS NOT A CROP: grass is mown where it grew, it is
/// not sown into a rotation, and it has no fertility to improve or exhaust.
/// Modelling it as a perennial crop field gave the run 200 hectares whose
/// fertility climbed from 55 to 100 and whose hay doubled over thirty years
/// with nobody doing anything (manual/balance/69-reconciliation.md §3 D5).
///
/// The two hay rates live in FarmingConfig, not in a crop row: the design
/// keeps them in prose until a land registry exists.
enum class LandKind : std::uint8_t {
  kArable = 0,        ///< Ploughed land: rotation, fertility, sowing, manure.
  kMeadow,            ///< Natural grassland, mown once a season.
  kFloodplainMeadow,  ///< The best grass of the farm; STUB until terrain zones.
  kDerelict,          ///< Arable nobody has raised: weeds and sod, no work until it is (phase 2).

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kLandKindCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kLandKindCount,
};

/// @brief What the weather is doing to a growing field, as a JUDGEMENT
/// rather than a number (boss parcel core-weather-stress, 2026-09-04).
///
/// The stress it names used to be ONE accumulator fed by drought and by
/// waterlogging alike. It scaled the harvest correctly and said nothing:
/// 0.3 of stress does not tell whether the field is drying or drowning, and
/// THE TWO ARE CURED BY OPPOSITE THINGS. Worse, they are not even the same
/// shape — drought costs a harvest, waterlogging costs a harvest AND the
/// dates it must be lifted by, with snow behind them. A player shown one
/// number is wrong half the time, and wrong most expensively in the half
/// where he had to hurry.
///
/// The threshold belongs to the core because the judgement does: the design
/// says "long heat without rain" and "drawn-out rains" and leaves the
/// number of days to whoever runs it (farming design §6). It is
/// FarmingConfig::weather_state_days, a table row, not a constant here.
enum class FieldWeatherState : std::uint8_t {
  kNone = 0,  ///< Neither run of days has reached the threshold.
  kDrying,    ///< Heat without rain, for weather_state_days running.
  kSoaking,   ///< Rain, for weather_state_days running.

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kFieldWeatherStateCount-1 and this is what they
  /// check against. Values are appended BEFORE it.
  kFieldWeatherStateCount,
};

/// @brief One field. Plain data.
/// A meadow that has not been mown within this world's memory. Not zero:
/// day zero is a real day — the world's first — and a meadow mown on it must
/// not read as never mown.
inline constexpr SimDay kNeverMownDay = static_cast<SimDay>(-1);

struct FieldRow {
  /// Center of the contour. The shape itself is presentation/routing data
  /// (project phase 2); the core needs only a location.
  Vec2 center;

  float area_ga = 0.0F;

  /// Soil fertility, 0-100; 50 is the neutral yield factor (see @file).
  Metric fertility = 50.0F;

  FieldPhase phase = FieldPhase::kIdle;

  /// The crop currently in the ground (sown, growing or being harvested);
  /// invalid when idle or fallow.
  CropId crop;

  /// The three-year rotation the player (or genesis) assigned: what to sow
  /// this year, next year, the year after. Invalid = fallow that year.
  CropId rotation_year0;

  CropId rotation_year1;

  CropId rotation_year2;

  /// The crop harvested last year and how many years in a row it repeated —
  /// the rotation memory behind the repeat penalty (§7).
  CropId last_crop;

  std::uint8_t repeat_years = 0;

  /// The share of the manure norm this field received for the current cycle,
  /// in PERCENT (§8): the bonus at the harvest — or at the year's turn for a
  /// bare fallow — scales by it, then it resets. It was a 0/1 flag until the
  /// fifth reconciliation pass: whole doses or nothing left the biggest field
  /// unmanured for thirty years.
  std::uint8_t manure_applied = 0;

  /// Arable land, meadow or derelict (see LandKind). A meadow ignores every
  /// field above it except `area_ga` and `phase`: no crop, no rotation, no
  /// fertility, no manure. Derelict land is arable that nobody has raised
  /// yet — the start's ninety hectares of weeds and sod: it keeps its
  /// fertility ("the land has rested", start canon §8) and gets no work of
  /// any kind until construction raises it. It is NOT rotation fallow, which
  /// is ploughed every year it stands (defect D11 taught the difference:
  /// ploughing the derelict cost 150 man-days a year nobody had asked for).
  /// The byte sits in the padding the row already had, so sizeof(FieldRow)
  /// is unchanged — but the SAVE STREAM grew by a byte per field, which the
  /// sizeof tripwire cannot see and VERSION_SAVE must (manual/67-save-format.md §7).
  LandKind kind = LandKind::kArable;

  /// Growth-season weather stress from HEAT, 0..1, accumulated daily while
  /// growing (farming design §6).
  ///
  /// This and the field below were one number until 2026-09-04, and the
  /// harvest still uses their SUM, capped exactly as the single number was:
  /// splitting them was not allowed to move the yield, and it did not (the
  /// thirty-year run is identical). What the split buys is that the row can
  /// now be ASKED which way it is suffering — see FieldWeatherState.
  float drought_stress = 0.0F;

  /// Growth-season weather stress from RAIN, 0..1, on the same terms.
  /// Kept apart from the one above because a sum of unmixable quantities is
  /// a number somebody decides by and gets wrong.
  float wet_stress = 0.0F;

  /// Consecutive growing days of heat and of rain. They are counters, not
  /// history: a day of the other kind resets the one it is not. The state
  /// below is judged off them, and they are saved because a load in the
  /// middle of a dry spell must not forget the spell.
  std::uint8_t drought_run_days = 0;

  std::uint8_t wet_run_days = 0;

  /// The core's judgement, so that no reader has to invent its own from
  /// temperature. Set every growing day from the two counters above.
  FieldWeatherState weather_state = FieldWeatherState::kNone;

  /// Game man-days of work left in the current working phase — the
  /// production/labor seam (see @file). Set by production at phase open,
  /// drained by labor's assigned crew, phase advances at 0. A phase set
  /// wired without the labor sub-step therefore never finishes a working
  /// phase — deliberately: work does not happen without workers.
  float work_days_remaining = 0.0F;

  /// Norm man-days of CARRYING still owed for the load below (task A4,
  /// manual/75-logistics.md §4). A seam of its own, and it earned one: the
  /// field's `work_days_remaining` carries the demand of whatever the field
  /// is DOING, and carrying a load off it is a second job that runs beside
  /// the first. Three separate findings of the A4 delivery cycle pointed at
  /// this one missing number — two jobs raised from a single seam, the
  /// carried grain having to be INFERRED by subtraction because nothing
  /// recorded it, and a loaded field unable to be ploughed because its only
  /// seam was busy. One float answers all three.
  ///
  /// Written by production (it sizes the demand and settles it at the day's
  /// last tick), drained by labor exactly like any other seam. Zero whenever
  /// `reaped_grams` is zero.
  float haul_days_remaining = 0.0F;

  /// What the settlement LAST WROTE into the seam above, so that it can tell
  /// what people carried from what the room did.
  ///
  /// It exists because the two are not the same and were treated as the
  /// same. The settlement used to read the drain as "today's demand minus
  /// what is left of yesterday's", and the demand is capped by the room the
  /// stores can still take — room that GROWS every day, because the village
  /// eats. So on a day when nobody was sent to the field at all, today's
  /// demand came out larger than yesterday's leftover and the difference was
  /// booked as a load carried. **The thirtieth year of the run harvested
  /// 1172 tonnes and spent 0.00 man-days carrying them** (seventh
  /// reconciliation pass).
  ///
  /// With the written figure kept, the drain is exactly what it says:
  /// written minus left, and nothing else can move it.
  float haul_days_written = 0.0F;

  /// Produce of `crop` reaped and NOT YET IN A STORE, in grams: the field
  /// brigade's buffer of the transport design (§9, "what accumulates: the
  /// harvest off the field"), in its smallest form — one resource, one
  /// number. Non-zero only while the stores had no room for the whole
  /// yield at payout (manual/72-storage-and-alarms.md §2): the field stays
  /// in kHarvest with no work left, production retries the delivery every
  /// day and empties this first, and kHarvestWaitingOnField stands meanwhile.
  /// STUB, with the term named (boss, 2026-09-03): the first settled snow
  /// takes what is still lying there, which BOUNDS the free storage rather
  /// than modelling spoilage. Real weathering of swaths — how many days of
  /// rain cost how much — is polish, and the design owes the number.
  /// Snow that ends the season loses it (kFieldLost) and books it to the
  /// ledger's lost_no_room — never silently. Task A4's logistics will move it
  /// instead of the instant stub; the buffer is the same. SAVED: history
  /// the simulation cannot rederive (VERSION_SAVE 4 → 5, the human's call).
  Grams reaped_grams = 0;

  /// What the waiting load IS. The buffer has to name its own resource: the
  /// field goes idle after payout and its `crop` is cleared for the next
  /// rotation slot, so by the time a cart comes the crop field no longer
  /// says what is lying there. Invalid exactly when reaped_grams is 0.
  /// (Added during implementation of task A3; the design named only the
  /// number and that was one field short — manual/72-storage-and-alarms.md §5.)
  ResourceId reaped_resource;

  /// THE DAY THIS MEADOW WAS LAST MOWN, or kNeverMownDay if it has not been
  /// within this world's memory. Meaningless on arable land.
  ///
  /// It exists because "the meadow is in flower" is a POSITION the layer has
  /// to paint after a load, and until 2026-09-06 nothing on the seam could
  /// say it: MowMeadow put the field straight back into kGrowing, so mown
  /// grass and standing grass were the same state. ue found it looking for
  /// somewhere to put butterflies and refused to read kHarvest as "in
  /// flower" — rightly: there kHarvest means "the work is not done", which
  /// is about the work order and not about the grass.
  ///
  /// A DAY AND NOT A FLAG, for the same reason the leaf-fall word is a day's
  /// event and not a winter: a flag needs a reset date somebody picks, and a
  /// wrong one lies silently. From a day, the aftermath comes out on its own
  /// — a meadow cut early flowers again before the season ends, a meadow cut
  /// late does not — so "an early cut gives the better hay and cuts the
  /// nectar flow short" is one fact with one home, and the timing of the
  /// mowing is the player's choice the design says it is.
  ///
  /// SECOND READER, AND IT DOES NOT EXIST YET. The apiary's yield is the
  /// older of the two in the design and has no implementation in this core:
  /// there is a unit type, a beekeeper in the staff table and honey in the
  /// resources, and no line of production anywhere. So this field can be
  /// checked today only through the layer's eyes.
  SimDay last_mown_day = kNeverMownDay;

  /// THE CORE'S JUDGEMENT that this meadow is in flower today, so that no
  /// reader invents its own — exactly as `weather_state` above it, and for
  /// the same reason: the window and the regrowth are a MECHANIC, and a
  /// mechanic the layer recomputes is a mechanic with two answers.
  ///
  /// Derived from `last_mown_day` and the day, and stored anyway, because
  /// the alternative is publishing the day and letting whoever reads it
  /// apply the rule. Set every day by the production phase, false on arable
  /// land. Its two readers are the butterflies the layer paints and the
  /// apiary's nectar flow, which the core does not yet model at all.
  bool in_flower = false;
};

/// @brief Is this meadow in flower today?
///
/// ONE HOME FOR THE JUDGEMENT, like FieldRow::weather_state next door: the
/// layer paints butterflies from it and the apiary will draw its nectar flow
/// from it, and a rule with two homes is a mechanic with two answers.
///
/// A meadow is in flower when the season allows it and the grass has had
/// time to come back since the cut. The aftermath falls out of the same
/// arithmetic rather than needing a rule of its own: cut early and the
/// regrowth still reaches the end of the window, cut late and it does not.
/// That is the design's "an early cut gives the better hay and cuts the
/// nectar flow short", and it makes the date of the mowing the player's
/// choice rather than a switch.
///
/// @param today          The day being painted.
/// @param first_month    First month of the flowering window, 0-based.
/// @param last_month     Last month of the flowering window, inclusive.
/// @param regrowth_days  Game days the aftermath needs to flower again.
constexpr bool MeadowInFlower(const FieldRow& field,
                              SimDay today,
                              std::uint8_t first_month,
                              std::uint8_t last_month,
                              std::uint16_t regrowth_days) {
  if (field.kind != LandKind::kMeadow && field.kind != LandKind::kFloodplainMeadow) {
    return false;
  }
  const std::uint8_t month = static_cast<std::uint8_t>(DateFromDay(today).month);
  if (month < first_month || month > last_month) {
    return false;
  }
  if (field.last_mown_day == kNeverMownDay) {
    return true;  // never cut in this world's memory: it stands
  }
  if (today < field.last_mown_day) {
    return false;  // a day before the cut cannot be after the regrowth
  }
  return today - field.last_mown_day >= regrowth_days;
}

/// @brief The fields table type used by WorldState.
using FieldTable = StateTable<FieldId, FieldRow>;

}  // namespace core

#endif  // CORE_COMMON_LAND_STATE_H_
