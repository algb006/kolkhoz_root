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

  // THERE IS NO kDerelict, AND THERE WAS ONE UNTIL 2026-09-12. It made an
  // overgrown field a KIND of land that refused every order and was skipped
  // whole by the production day, so ninety-three of the start's hundred and
  // sixty-three hectares could never be worked by anybody — and the thirty
  // year run grew sixteenfold without touching a hectare of them.
  //
  // The design says the opposite in as many words, and the words are these:
  // «медленно зарастает — и это только картинка… заброшенность нигде не
  // считается, работ не требует и ни на что в игре не влияет», and of the
  // field card, «фаза там стоит прежняя — простой. Это вид, а не состояние»
  // (farming design). An English rendering of those two stood here inside
  // quotation marks until 2026-09-12 — faithful in substance and findable in
  // the document by nobody, which is the same defect this file condemns two
  // paragraphs further down. A quotation is a promise that the words are
  // there. One ploughing brings it all back, at the same norm as
  // any other ground: "целина, залежь, задерневшее поле, пашня, которую
  // держат двадцать лет подряд — норма вспашки одна."
  //
  // So the look moved to FieldRow::overgrown and the kind went. What the
  // core could never do, it now simply does not forbid: raising the land is
  // ploughing it.
  //
  // AND NOT "the hectares enter the plan the year after", which this block
  // said until the second analysis pass read it. District design §9 asks for
  // that; the core does not do it. The norm is read at the spring
  // announcement, eight days after the rotation has already been shifted, so
  // it sees THIS year's slot and the core remembers no earlier year at all.
  // The same correction is written twice more, beside AnnouncePlan and beside
  // plan_grain_share — and it survived HERE through the repair that fixed
  // those two, which is the whole reason this file keeps saying that a fix
  // lands where it was noticed and not where it belongs.

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

/// @brief A meadow that has not been mown within this world's memory.
/// Not zero: day zero is a real day — the world's first — and a meadow mown
/// on it must not read as never mown.
///
/// It stands BEFORE FieldRow because the row's default member initialiser
/// needs it. That placement cost FieldRow its own @brief for a day: the
/// constant was spliced under the brief that belonged to the struct, and a
/// doc comment documents whatever follows it, not whatever it was written
/// for. UB-004 of the 2026-09-06 cycle, and the second time this delta a
/// brief ended up over the wrong declaration.
inline constexpr SimDay kNeverMownDay = static_cast<SimDay>(-1);

/// @brief One field. Plain data.
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

  /// Arable land or meadow (see LandKind). A meadow ignores every field
  /// above it except `area_ga` and `phase`: no crop, no rotation, no
  /// fertility, no manure.
  LandKind kind = LandKind::kArable;

  /// WEEDS AND SOD, AND NOTHING ELSE: the field has not been ploughed since
  /// the campaign began, so it reads overgrown from the road. Zero work, zero
  /// rules, zero arithmetic — the design is explicit that neglect "affects
  /// nothing in the game" and that the field's card goes on showing idle.
  ///
  /// STUB, and the shape of the stub is the honest part. The design computes
  /// the look from the LAST PLOUGHING — "the stage counts from the last
  /// ploughing, not from the last harvest" — and the core keeps no such day,
  /// so this is the start condition frozen: set from start_layout's
  /// `is_derelict` at genesis, cleared the first time the field is ploughed,
  /// and never set again. A field left idle for ten years will not grow over
  /// until there is a day to count from, and writing a year counter here to
  /// fake it would be inventing the mechanic rather than stubbing it.
  ///
  /// The layer that draws it is the only reader. Nothing in the core branches
  /// on this byte, and if anything ever does, that is the bug: it is a look.
  std::uint8_t overgrown = 0;

  /// WHETHER THE PLAYER HAS EVER TOLD THIS FIELD WHAT TO GROW, 0 or 1 — and
  /// it is a fact of its own because the three rotation slots cannot carry
  /// it. The player gives a field a chain of three seasons, crop or fallow
  /// (farming design §7), so an EMPTY SLOT means "fallow that year" in a
  /// chain that was given, and means nothing at all in a field that never
  /// got one. Three empty slots said both things at once.
  ///
  /// THE TREE HELD BOTH READINGS, in two modules, until 2026-09-12: the
  /// boundary's shape check for kSetRotation called three invalid crops "a
  /// legal rotation, three years of fallow", while the production side read
  /// them as "nobody has told this field anything". Nothing broke, because
  /// only genesis wrote three invalid slots and it wrote them for exactly
  /// the ground nobody works — but the day the player could order three
  /// fallow years, his field would have stopped being ploughed, recovered
  /// and manured, which is the opposite of what he asked for.
  ///
  /// So the two questions are two fields now. Boss's ruling of that evening:
  /// an empty slot means NOT ASSIGNED, deliberate fallow gets a word of its
  /// own, and fallow is never a row in the crop table — a row of zeros would
  /// be a counterfeit crop, and every check that reads norms per crop would
  /// get a subject that lies to it.
  ///
  /// IT STANDS AFTER `overgrown` BECAUSE THE CODEC WRITES IT THERE. A row is
  /// encoded in the declaration order of this header (save.h), and the first
  /// draft of the two crossed: declared before, written after. The pair was
  /// symmetric, so nothing was ever mis-read — what was lost is the only
  /// property that lets a codec be checked against its header by reading
  /// them side by side, and neither the sizeof nor the arity tripwire can
  /// see an order swap, because both numbers stay right (analysis, 0.17.96).
  ///
  /// AND IT CLEARS AGAIN ON AN EMPTY CHAIN. A kSetRotation naming no crop at
  /// all is the chairman taking his word back: the slots are cleared with the
  /// byte and the field is ground nobody has spoken to again (boss's ruling,
  /// 2026-09-12). It was one-way for one afternoon, and that was a trap — a
  /// field told once would have been worked for ever, the nearest release
  /// being an all-fallow chain which is still ploughed, recovered and
  /// manured every year (farming design §3), so a layout mistake cost work
  /// for the rest of the campaign. No new order kind carries the release:
  /// the same door sets a chain and withdraws one.
  ///
  /// ONE OR TWO EMPTY SLOTS STILL MEAN FALLOW YEARS. The bit's whole job is
  /// this difference and it survives the release rule: emptiness INSIDE a
  /// chain is a rested season, emptiness of the WHOLE chain is no chain.
  std::uint8_t rotation_assigned = 0;

  /// THE CHAIN'S FIRST SEASON HAS NOT BEEN USED YET, 0 or 1 — and while it
  /// stands, the year's turn does not advance the three slots.
  ///
  /// The slots are a cycle and the turn advances it, so the chain a chairman
  /// writes down would mean different years depending on the month he wrote
  /// it. Boss's requirement of 2026-09-12: THE CROP HE NAMES FIRST IS THE ONE
  /// THE NEXT SOWING PUTS IN THE GROUND. A chairman laying his three years
  /// out in November, with the harvest in and time to think, must not find
  /// his first crop in the third season for having given the order on time —
  /// that is "do not punish the unforeseeable" lost to a calendar detail.
  ///
  /// SO THE MARK IS A FACT ABOUT THE CHAIN, NOT ABOUT THE MONTH. SetRotation
  /// sets it on every chain it writes; OpenPlowing clears it, because that
  /// call is the one place every use of a rotation passes through — this
  /// year's crop, a fallow year's ploughing, and the autumn sowing of the
  /// next slot's winter crop all open their work there. RunYearStart holds
  /// the slots still while it stands and never clears it.
  ///
  /// A FIRST DRAFT ASKED THE CALENDAR INSTEAD — "was the first named crop's
  /// window past when the order arrived" — and the analysis pass found three
  /// reachable holes before it shipped: a field still carrying last year's
  /// standing crop is not sown in April however open April is; an order
  /// settling after the day's field walk misses a window that closes that
  /// night; and a cold spring opens nothing at all. In each the month said
  /// "there is still time", the ground said otherwise, and January carried
  /// the chairman's first crop away. THE MONTH WAS A PROXY FOR THE QUESTION,
  /// AND THE QUESTION IS ANSWERABLE DIRECTLY.
  ///
  /// WHAT IT DOES NOT DO, said rather than left to be found: a winter crop
  /// standing LATER in the chain is still sown in the autumn that precedes
  /// its year, because that is what a winter crop in a rotation means — and
  /// that sowing is a use of the chain, so it spends the mark. A chain named
  /// in August as (oats, winter rye, …) puts the rye in the ground that
  /// September, and the turn that follows moves the rye into year0, where
  /// the repeat guard in TrySow keeps it from going in twice.
  std::uint8_t rotation_skips_turn = 0;

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
  /// wrong one lies silently. From a day, both halves of the rule come out on
  /// their own — the meadow stands until the scythe reaches it, and the year
  /// the day belongs to says when the latch lets go. So "an early cut gives
  /// the better hay and cuts the nectar flow short" is one fact with one
  /// home, and the timing of the mowing is the player's choice the design
  /// says it is.
  ///
  /// SECOND READER, AND IT DOES NOT EXIST YET. The apiary's yield is the
  /// older of the two in the design and has no implementation in this core:
  /// there is a unit type, a beekeeper in the staff table and honey in the
  /// resources, and no line of production anywhere. So this field can be
  /// checked today only through the layer's eyes.
  SimDay last_mown_day = kNeverMownDay;

  /// THE CORE'S JUDGEMENT that this meadow is in flower today, so that no
  /// reader invents its own — exactly as `weather_state` above it, and for
  /// the same reason: the window and the cut are a MECHANIC, and a mechanic
  /// the layer recomputes is a mechanic with two answers. ue confirmed on
  /// 2026-09-06 that he stopped reading the field phase entirely and paints
  /// butterflies from this word, which makes it the only opinion there is.
  ///
  /// Derived from `last_mown_day` and the day, and stored anyway, because
  /// the alternative is publishing the day and letting whoever reads it
  /// apply the rule. Set every day by the production phase, false on arable
  /// land. Its two readers are the butterflies the layer paints and the
  /// apiary's nectar flow, which the core does not yet model at all.
  bool in_flower = false;
};

/// @brief Whether the player has given this field a rotation at all.
///
/// A CHAIN WITH A GAP IN IT IS STILL A CHAIN. The player gives each field a
/// chain of three seasons, crop or fallow (farming design §7), so a single
/// empty slot is a FALLOW YEAR and the field is worked: ploughed bare, left
/// to stand, sown from the next slot in the autumn. Three empty slots are
/// something else — nobody has told this ground anything, and the start
/// hands over ninety-three hectares of exactly that.
///
/// IT LIVES IN core_common BECAUSE FIVE PLACES ASK IT, in three modules:
/// sowing, the year's fertility recovery and the manure queue in
/// core_production, the office-wall mean fertility in core_world, and the
/// ledger's mean in core_report. The question is about a ROW, not about a
/// configuration, so the row's own header is where it belongs.
///
/// THE REASON RECORDED HERE FIRST WAS THE WRONG ONE, and the second analysis
/// pass caught it: it said core_labor needed the question and must not reach
/// into core_production. core_labor's asker was a refusal that was written
/// and then withdrawn the same hour, so by the time the move was justified
/// the justification had evaporated — and the move turned out to be right
/// for readers nobody had walked yet.
///
/// THE ASKERS, and for one afternoon only one of them had an answer.
/// The guard went into TrySow when LandKind::kDerelict was removed on
/// 2026-09-12; the fallow fertility recovery and the manure queue had both
/// been correct only because that land carried a kind they filtered out, and
/// went on treating unworked ground as resting fallow. Measured on the
/// delivery's own field sheet: the unworked ninety-three hectares climbed
/// 65 → 100 in six years, six a year, which is defect D5 of the
/// reconciliation returning under a new name.
///
/// THE TREE CARRIED THE OPPOSITE READING OF THESE THREE SLOTS FOR A DAY, and
/// this is what the byte above was made for. The boundary's shape check for
/// OrderKind::kSetRotation SAID, until the evening of 2026-09-12, that "the
/// three crops may all be invalid: that is three years of fallow, a legal
/// rotation and not an empty order", while this function had read the same
/// three invalid ids as "nobody has told this field anything". Both were
/// defensible and they could not both be right. The sentence is gone from
/// session.cpp now — quoted here in the past tense, because a quotation in
/// the present tense of words no longer in the tree is a promise the reader
/// cannot check.
///
/// SO THE QUESTION STOPPED BEING INFERRED. It is answered by
/// FieldRow::rotation_assigned, which genesis sets where the layout named a
/// chain and SetRotation sets when the player names one. An empty slot means
/// a fallow year in a chain that exists, and nothing at all in a field that
/// has none — and the two are now different bits rather than the same three
/// read two ways. Boss's ruling, 2026-09-12, the evening kSetRotation got
/// its consumer and the reading would otherwise have started to matter.
///
/// @note IT NO LONGER ASKS ANYTHING ABOUT THE IDS, which closes a hazard the
///       previous version carried in its own note: reading "is there an id
///       here" would have parted company with core_production's `value <
///       crops.size()` the day something wrote a slot without checking it
///       against the roster. SetRotation is exactly that something, and it
///       checks — every slot it writes is either invalid or a live crop row,
///       refused otherwise — but this function no longer depends on that
///       being true.
inline bool HasRotation(const FieldRow& field) {
  return field.rotation_assigned != 0;
}

/// @brief Is this meadow in flower today?
///
/// ONE HOME FOR THE JUDGEMENT, like FieldRow::weather_state next door: the
/// layer paints butterflies from it and the apiary will draw its nectar flow
/// from it, and a rule with two homes is a mechanic with two answers.
///
/// A meadow is in flower when the season allows it AND it has not been mown
/// this year. That is what makes the date of the mowing a choice rather than
/// a switch: cut in May and the nectar flow stops for two months, leave it
/// to July and the meadow has stood almost the whole window — which is the
/// design's "an early cut gives the better hay and cuts the nectar flow
/// short", read from the side the player sees.
///
/// The cut ends the flowering for the REST OF THE YEAR and no longer: the
/// stand comes back next spring. Deliberately a year and not a count of
/// days — see the body.
///
/// @param today        The day being painted.
/// @param first_month  First month of the flowering window, 0-based.
/// @param last_month   Last month of the flowering window, inclusive.
constexpr bool MeadowInFlower(const FieldRow& field,
                              SimDay today,
                              std::uint8_t first_month,
                              std::uint8_t last_month) {
  if (field.kind != LandKind::kMeadow && field.kind != LandKind::kFloodplainMeadow) {
    return false;
  }
  const Date date = DateFromDay(today);
  const std::uint8_t month = static_cast<std::uint8_t>(date.month);
  if (month < first_month || month > last_month) {
    return false;
  }
  if (field.last_mown_day == kNeverMownDay) {
    return true;  // never cut in this world's memory: it stands
  }
  // AN AFTERMATH DOES NOT FLOWER IN THE YEAR IT WAS MOWN, and that is a RULE
  // and not a number (boss, 2026-09-06). What stood here until then was a
  // regrowth of fourteen game days, invented by this core, and it was wrong
  // twice over: hay cut in flower comes back as leaf, so a second flowering
  // of the same stand would be a rarity in life and would blur the one
  // choice the mowing date is for in the game.
  //
  // A NUMBER THAT MEANS "NEVER" WILL ONE DAY TURN OUT TO BE A DAY. Shift the
  // window or the calendar and "a fortnight that will not fit" becomes
  // August. So the fact is stored as the fact: the cut belongs to a YEAR,
  // and the meadow is out of flower for the rest of it. The latch lets go in
  // the spring, exactly as WeatherState::cover_since_leaf_fall counts from
  // the leaf-fall and not from the snow.
  return DateFromDay(field.last_mown_day).year != date.year;
}

/// @brief The fields table type used by WorldState.
using FieldTable = StateTable<FieldId, FieldRow>;

}  // namespace core

#endif  // CORE_COMMON_LAND_STATE_H_
