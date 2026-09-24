// The six row codecs (save_rows.h). Fields go out and come back in the
// DECLARATION ORDER of the state header that defines each row, and the two
// directions of one row sit next to each other so that a field added to one
// and forgotten in the other is visible in the diff.
//
// The static_asserts below are the tripwire for the case neither direction
// was touched: a new field changes sizeof(RowT) and the build stops until
// the writer, the reader and the assert are all updated — and the human
// bumps VERSION_SAVE (manual/67-save-format.md §7).
//
// AND THE SIZE IS ONLY HALF THE TRIPWIRE since 2026-09-05: it cannot see a
// field that fits the existing padding, which has happened four times here
// and left the build green every time. Beside each size stands the FIELD
// COUNT (aggregate_arity.h), which is the thing that actually changed.
//
// A size that fails on a
// NEW TARGET, though, is a different animal: there the layout moved and the
// format did not, and an assert that cannot tell the two apart cries wolf.
// Hence the reckoning below — anything whose size belongs to the standard
// library rather than to us is counted out of the expected total.

#include "save_rows.h"

#include <cstddef>
#include <cstdint>

#include "core_common/aggregate_arity.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "save_stream.h"

namespace core {
namespace {

// Sizes measured for save format 1 on x86-64.
//
// The rows that carry a ResourceAmounts — a std::vector — count it out: its
// sizeof is a property of the standard library and its debug level (24 bytes
// under libc++, 32 under the MSVC STL with iterator debugging), not of the
// format, which writes the vector element by element. The line above used to
// claim "Clang and MSVC agreeing"; the first MSVC build of this module said
// otherwise, and it was right. See save_blocks.cpp for the same reckoning.
constexpr std::size_t kAmountsSize = sizeof(ResourceAmounts);

// 2026-09-06: 164 -> 172, and MEASURED rather than reasoned, as ever. Two
// floats — the height and build deviations of the figure — and this time the
// size moved with the field count instead of hiding in padding.
// 2026-09-14: 176 -> 180, the assignment's extraction site.
// 2026-09-15: the night trade byte landed in padding beside social_status —
// 180 still, 39 fields.
// The same day the pupil's school, a unit id, took it to 184.
// 2026-09-15: days_worked_this_month, a byte, landed in padding — 184 still,
// 41 fields.
// 2026-09-17, save 51: hygiene, a float beside the other metrics. It did NOT
// land in padding — 188 now — so both tripwires fired, which is the pair
// doing what it is for: the size alone misses a field that slips into a hole,
// the arity alone misses one that widens an existing member.
// 2026-09-18, save 59: distiller_supplied_month took it to 196 (measured).
// 2026-09-19, save 69: talk_until_day, the chairman's talk — 200, predicted
// before the field was added and measured after.
// Save 78: the twin — an id after talk_until_day (+4) and the identical mark
// into the padding after has_passport: 204, predicted before.
// Save 79: away in the district — a day (+4) and three bytes beside the
// passport, which push `traits` a word on (+4): 212, predicted before.
static_assert(sizeof(ResidentRow) == 212,
              "ResidentRow changed — update the codec and VERSION_SAVE");
// 2026-09-18, save 59: distiller_supplied_month, a distiller's supplied month
// (crime §7, register 206) — 43 fields; the size is read off the build.
// Save 69: talk_until_day — 44. Save 78: twin and identical_twin — 46.
// Save 79: away_until_day, _hour, _walk_hours, _reason — 50.
static_assert(AggregateArity<ResidentRow>() == 50,
              "ResidentRow gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-14: first_meal_eaten landed in padding beside food_variety_mask; the
// size stayed 56 + amounts and the field count went to 16. The same day the
// lost house's position and the tent byte took it to 72 + amounts (measured)
// and 18 fields.
// 2026-09-19, save 65: overwork_penalty, a float — 72 -> 80 + amounts,
// measured by a sizeof probe, not reasoned; 21 fields.
// Save 74: a byte beside in_tent and two words after it — 80 -> 88 + amounts,
// by the layout, confirmed by the build.
// Save 75: in_barrack, a byte after lodging_penalty — 88 -> 96 + amounts (the
// byte opened a new word).
static_assert(sizeof(FamilyRow) == 96 + kAmountsSize,
              "FamilyRow changed — update the codec and VERSION_SAVE");
// 2026-09-18, save 57: ration_granted, the yard's ration decision — 19 fields;
// the size is read off the build below, not guessed.
// 2026-09-18, save 60: dry_months, the yard's sobriety clock — 20 fields.
// Save 74: asked_to_leave, asked_day, lodged_in and lodging_penalty — 25
// fields.
// Save 75: in_barrack and hunger_alarm_lit — 27 fields.
static_assert(AggregateArity<FamilyRow>() == 27,
              "FamilyRow gained or lost a field — update the codec and VERSION_SAVE");
// FieldRow took LandKind into a padding byte it already had, so sizeof did
// NOT move — the one case the tripwire of manual/67-save-format.md §7 cannot
// see. The STREAM grew by a byte per field all the same, and VERSION_SAVE is
// what has to notice.
//
// 2026-09-04: it moved for once, 64 -> 72. weather_stress became TWO floats
// plus two run counters and a judgement byte (the drought/waterlogging
// split), and this time the tripwire fired before the codec did — which is
// the one thing it is for.
// TWO MEMBERS, AND ONLY THE SECOND ONE MOVED THE SIZE. last_mown_day landed
// inside existing padding and left 72 bytes at 72 — the fifth time that has
// happened here, and the second time the field count caught what the size
// could not. in_flower then pushed the struct to 80. The pair is the whole
// argument for keeping both asserts: either alone would have missed one of
// the two.
//
// And 88 is MEASURED, not reasoned: 76 was once the obvious answer from
// adding a byte to 72 plus padding, and it was wrong. A size guessed to
// satisfy a guard teaches the guard the guess.
//
// TWO BYTES WENT IN ON 2026-09-12 AND THEY LANDED DIFFERENTLY, which is the
// argument for measuring each time rather than once. `overgrown` fell into
// padding the row already had and moved nothing; `rotation_assigned` did not,
// and took the row from 80 to 88 — eight bytes for one, because it opened a
// fresh alignment slot.
//
// AND A THIRD BYTE ON THE SAME DAY, rotation_skips_turn, WHICH THE SIZE DID
// NOT SEE AT ALL: 88 before and 88 after, because it landed in the padding
// the second byte had opened. The field count caught it alone, and it is the
// plainest argument yet for keeping the two asserts side by side rather than
// choosing between them. (No tally of how often that has happened: the
// running count above was written when it was true and is the kind of number
// that ages beside a rule without anybody noticing.)
// 2026-09-13: `sown_day` — the day ripening is counted from — landed in
// padding beside `last_mown_day` and left 88 bytes at 88. The SIZE assert said
// nothing; the FIELD COUNT caught it, which is now the third time the pair has
// split this way and the reason neither is allowed to stand alone.
// 2026-09-14: `start_reserve` followed `in_flower` into the row's tail padding;
// the size stayed 88 and the field count went to 30.
// 2026-09-15: `reaped_day`, four bytes after the tail byte, opened a slot of
// its own: 88 -> 96, measured, and the field count went to 31.
// 2026-09-19, save 65: the avral's step and phase, two bytes into the tail
// padding — 96 stays 96 (measured), 33 fields: the count caught it alone.
// Save 84: the harvest by parts' laid share (float) and grams (i64) —
// predicted 96 -> 112 before the build, 35 fields.
// Save 87: the sown share (float) beside the laid share. Predicted "112
// stays", on the belief that a padding hole stood between the laid share and
// the laid grams. A MISS, named: the laid share sits at offset 76, so there
// was no hole, and the float opened one of its own before the grams. Measured
// 120 (offsetof: laid share 76, sown share 80, laid grams 88), 36 fields.
static_assert(sizeof(FieldRow) == 120, "FieldRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<FieldRow>() == 36,
              "FieldRow gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-06: the stink radius pushed the row from 48 + amounts to 56 +
// amounts. The pause byte before it had landed in padding and moved nothing,
// which is the whole reason both asserts stand here.
// 2026-09-13: the module's parent and the production seam — three 4-byte
// fields — took it only to 64 + amounts: four of the twelve bytes landed in
// the tail padding. The field count below moved by three regardless.
// 2026-09-14: the works' reserved amounts — a second dense vector, inside the
// site block — took it to 64 + two amounts (VERSION_SAVE 37). The site block
// had no tripwire of its own, so a byte landing in its padding would have
// moved neither assert on the row: it has its own pair now.
// 2026-09-15: the insulated byte landed in padding beside `dead` — the size
// stayed, the field count went to 14 (VERSION_SAVE 44).
// 2026-09-19, save 67: the store's emptying byte and the perevalka's two
// floats — 64 -> 80 + two amounts (measured by a sizeof probe), 17 fields.
static_assert(sizeof(UnitRow) == 80 + (2 * kAmountsSize),
              "UnitRow changed — update the codec and VERSION_SAVE");
static_assert(sizeof(ConstructionState) == 16 + kAmountsSize,
              "ConstructionState changed — update the codec and VERSION_SAVE");
// 2026-09-19, save 65: the avral's step, a byte beside the phase — 16 + amounts
// stays (measured), 7 fields. Save 80: winter_works, a byte after max_crew —
// predicted into the padding before `reserved`, 16 + amounts and 8 fields.
static_assert(AggregateArity<ConstructionState>() == 8,
              "ConstructionState gained or lost a field — update the codec and VERSION_SAVE");
// Save 74: reserved_for_specialist, a byte beside `dead` — 18 fields.
static_assert(AggregateArity<UnitRow>() == 18,
              "UnitRow gained or lost a field — update the codec and VERSION_SAVE");
// Save 71: fed_share, the day's covered ration — 68 and nineteen fields,
// predicted before the field was added and measured after. Save 76:
// autumn_slaughter_done, a byte into the padding after disease_stage — 68
// and twenty, predicted before. Save 91: the adult age band, two floats after
// the age total — 76 and twenty-two, predicted before the fields were added.
static_assert(sizeof(HerdRow) == 76, "HerdRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<HerdRow>() == 22,
              "HerdRow gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-13: the felling mark — a stand id and a volume — took the order row
// from 64 to 72 and the assignment's stand from 24 to 28 (and the resident
// row that carries it with it).
// 2026-09-13: the limit lot (a definition id) took it from 72 to 80.
// 2026-09-14: the extraction site landed in the order row's padding — 80
// still, 22 fields, which is exactly the case the arity check is for — and
// took the assignment from 28 to 32.
// 2026-09-18, save 57: the ration's family and switch (kSetRation) took it
// to 88 and 25 fields.
// Save 82: the planting's hectares and species — predicted 88 -> 96 and 27
// fields before the fields were added.
static_assert(sizeof(OrderRow) == 96, "OrderRow changed — update the codec and VERSION_SAVE");
// 2026-09-16: the bought head's sex landed in the padding as well — 80 still,
// 23 fields. Two padding fields in a row now, which is the answer to whether
// the arity check was worth its line.
static_assert(AggregateArity<OrderRow>() == 27,
              "OrderRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(WorkAssignment) == 32,
              "WorkAssignment changed — update the codec and VERSION_SAVE");
// Save 88: rides_horse, a byte into the padding after `kind` — 32 still, 9
// fields; predicted before the build.
static_assert(AggregateArity<WorkAssignment>() == 9,
              "WorkAssignment gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(LimitDeliveryRow) == 8 + kAmountsSize,
              "LimitDeliveryRow changed — update the codec and VERSION_SAVE");
static_assert(sizeof(LivestockArrivalRow) == 16,
              "LivestockArrivalRow changed — update the codec and VERSION_SAVE");
// THE ARITY BESIDE THE SIZE, and on this row it is not a formality: the stage
// and the sex are two bytes in a struct that already has padding to spare, so
// a third such field would change the wire and leave sizeof exactly where it
// is. That is the case the size alone cannot see, and the order row proved it
// twice over on the very day this row was written.
static_assert(AggregateArity<LivestockArrivalRow>() == 6,
              "LivestockArrivalRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(AggregateArity<LimitDeliveryRow>() == 3,
              "LimitDeliveryRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(SpecialistArrivalRow) == 12,
              "SpecialistArrivalRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<SpecialistArrivalRow>() == 3,
              "SpecialistArrivalRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(WeddingWaitRow) == 12,
              "WeddingWaitRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<WeddingWaitRow>() == 3,
              "WeddingWaitRow gained or lost a field — update the codec and VERSION_SAVE");
// Save 82: a planting's species, hectares and two days — 48 -> 64, nine
// fields -> thirteen, predicted before the fields were added.
static_assert(sizeof(TimberStandRow) == 64,
              "TimberStandRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<TimberStandRow>() == 13,
              "TimberStandRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(ExtractionSiteRow) == 64,
              "ExtractionSiteRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<ExtractionSiteRow>() == 10,
              "ExtractionSiteRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(NightOutingRow) == 24,
              "NightOutingRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<NightOutingRow>() == 6,
              "NightOutingRow gained or lost a field — update the codec and VERSION_SAVE");
// Save 79: the district's car — two bytes, an id, two ticks: 24 and five
// fields, predicted before the row was written.
static_assert(sizeof(DistrictCarRow) == 24,
              "DistrictCarRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<DistrictCarRow>() == 5,
              "DistrictCarRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(DistrictVisitRow) == 8,
              "DistrictVisitRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<DistrictVisitRow>() == 4,
              "DistrictVisitRow gained or lost a field — update the codec and VERSION_SAVE");

/// Highest valid value of each u8 enum a row carries. The reader refuses
/// anything above (LoadSource::ReadEnumValue) — see its docs for why.
// EVERY LENGTH IS DERIVED, NOT WRITTEN. Each enum carries its own count as
// its last enumerator, and these are that count minus one — so appending a
// value moves the guard with it, and there is nothing left to forget.
//
// It used to be a hand-written last enumerator plus an instruction in the
// enum's own header saying "raise this when you append". Two homes for one
// length, and the journal codec's copy sat two tasks behind: A2 appended two
// order kinds and kMaxOrderKind stayed at kDemolishUnit, so a journal
// carrying kStartBuild would have been refused on decode. Found by a design
// pass, not by anything that runs — a rule whose only mechanism is a comment
// is an intention, and an intention fails no check because it takes part in
// none (boss, 2026-09-04).
//
// The counts are also what lets a CONSUMER static_assert the length of its
// own mirror: the graphics layer had eight refusal reasons against our nine
// and had no way to notice.
constexpr std::uint8_t kMaxSex = static_cast<std::uint8_t>(Sex::kSexCount) - 1;

constexpr std::uint8_t kMaxWorkKind = static_cast<std::uint8_t>(kWorkKindCount) - 1;

constexpr std::uint8_t kMaxConstructionPhase =
    static_cast<std::uint8_t>(ConstructionPhase::kConstructionPhaseCount) - 1;

constexpr std::uint8_t kMaxEducationStage =
    static_cast<std::uint8_t>(EducationStage::kEducationStageCount) - 1;

constexpr std::uint8_t kMaxSocialStatus =
    static_cast<std::uint8_t>(SocialStatus::kSocialStatusCount) - 1;

constexpr std::uint8_t kMaxNightTrade = static_cast<std::uint8_t>(NightTrade::kNightTradeCount) - 1;
static_assert(kMaxNightTrade < static_cast<std::uint8_t>(NightTrade::kNightTradeCount));

constexpr std::uint8_t kMaxFieldPhase = static_cast<std::uint8_t>(FieldPhase::kFieldPhaseCount) - 1;
/// The avral's ceiling as the byte the stream carries (order_state.h).
constexpr auto kMaxRushStepByte = static_cast<std::uint8_t>(kMaxRushStep);
constexpr std::uint8_t kMaxLandKind = static_cast<std::uint8_t>(LandKind::kLandKindCount) - 1;
constexpr std::uint8_t kMaxFieldWeatherState =
    static_cast<std::uint8_t>(FieldWeatherState::kFieldWeatherStateCount) - 1;

// MEM-002 fix: these two were left at the enumerators of before task A2
// while order_state.h grew kStartBuild, kUpgradeUnit and three refusals
// past them. The error was in the safe direction — nothing read out of
// range — but it broke loading: a staged kStartBuild is exactly what a save
// taken between steps carries, and DecodeWorld threw the whole world away
// over "order kind holds 8, outside 0..7". Both names are the LAST
// enumerator of their enum, and order_state.h says so where a new one gets
// appended.
constexpr std::uint8_t kMaxOrderKind = static_cast<std::uint8_t>(OrderKind::kOrderKindCount) - 1;

constexpr std::uint8_t kMaxFundKind = static_cast<std::uint8_t>(FundKind::kFundKindCount) - 1;

constexpr std::uint8_t kMaxLivestockArrivalStage =
    static_cast<std::uint8_t>(LivestockArrivalStage::kLivestockArrivalStageCount) - 1;
// AND THIS ONE HAD NO SELF-CHECK while the block below said every bound has
// one. Found on 2026-09-12 by an analysis walking the enum bounds after a
// value was removed from a different enum; the assertion is one line and the
// claim it was missing from is the reason it is worth writing down.
static_assert(kMaxFundKind < static_cast<std::uint8_t>(FundKind::kFundKindCount));
constexpr std::uint8_t kMaxTimberStandKind =
    static_cast<std::uint8_t>(TimberStandKind::kTimberStandKindCount) - 1;
static_assert(kMaxTimberStandKind <
              static_cast<std::uint8_t>(TimberStandKind::kTimberStandKindCount));
constexpr std::uint8_t kMaxDistrictFace =
    static_cast<std::uint8_t>(DistrictFace::kDistrictFaceCount) - 1;
static_assert(kMaxDistrictFace < static_cast<std::uint8_t>(DistrictFace::kDistrictFaceCount));
constexpr std::uint8_t kMaxDistrictVisitKind =
    static_cast<std::uint8_t>(DistrictVisitKind::kDistrictVisitKindCount) - 1;
static_assert(kMaxDistrictVisitKind <
              static_cast<std::uint8_t>(DistrictVisitKind::kDistrictVisitKindCount));
constexpr std::uint8_t kMaxDistrictCarKind =
    static_cast<std::uint8_t>(DistrictCarKind::kDistrictCarKindCount) - 1;
constexpr std::uint8_t kMaxDistrictCarPhase =
    static_cast<std::uint8_t>(DistrictCarPhase::kDistrictCarPhaseCount) - 1;
constexpr std::uint8_t kMaxAwayReason = static_cast<std::uint8_t>(AwayReason::kAwayReasonCount) - 1;
constexpr std::uint8_t kMaxDistrictVisitCause =
    static_cast<std::uint8_t>(DistrictVisitCause::kDistrictVisitCauseCount) - 1;
static_assert(kMaxDistrictVisitCause <
              static_cast<std::uint8_t>(DistrictVisitCause::kDistrictVisitCauseCount));
constexpr std::uint8_t kMaxOrderStatus =
    static_cast<std::uint8_t>(OrderStatus::kOrderStatusCount) - 1;
constexpr std::uint8_t kMaxOrderRefusal =
    static_cast<std::uint8_t>(OrderRefusal::kOrderRefusalCount) - 1;

// AND THE GUARD CHECKS ITSELF, here, where it lives. Every bound above must
// be strictly BELOW its count, because the count is not a value: a save
// carrying its number has to be refused, not decoded into a kind that does
// not exist. Writing `= kCount` instead of `= kCount - 1` is the one
// plausible slip these derivations still allow, and it would let exactly
// that byte through.
//
// A compile-time check and not a run-time one on purpose: there is no world
// in which the wrong bound is acceptable, so there is nothing to observe at
// run time — only something to forbid.
static_assert(kMaxSex < static_cast<std::uint8_t>(Sex::kSexCount));
static_assert(kMaxWorkKind < static_cast<std::uint8_t>(kWorkKindCount));
static_assert(kMaxEducationStage < static_cast<std::uint8_t>(EducationStage::kEducationStageCount));
static_assert(kMaxSocialStatus < static_cast<std::uint8_t>(SocialStatus::kSocialStatusCount));
static_assert(kMaxFieldPhase < static_cast<std::uint8_t>(FieldPhase::kFieldPhaseCount));
static_assert(kMaxLandKind < static_cast<std::uint8_t>(LandKind::kLandKindCount));
static_assert(kMaxFieldWeatherState <
              static_cast<std::uint8_t>(FieldWeatherState::kFieldWeatherStateCount));
static_assert(kMaxConstructionPhase <
              static_cast<std::uint8_t>(ConstructionPhase::kConstructionPhaseCount));
static_assert(kMaxOrderKind < static_cast<std::uint8_t>(OrderKind::kOrderKindCount));
static_assert(kMaxOrderStatus < static_cast<std::uint8_t>(OrderStatus::kOrderStatusCount));
static_assert(kMaxOrderRefusal < static_cast<std::uint8_t>(OrderRefusal::kOrderRefusalCount));

template <typename IdT>
void WriteEntityId(ByteWriter& out, IdT id) {
  out.WriteU32(id.value);
}

template <typename IdT>
IdT ReadEntityId(ByteReader& in) {
  return IdT{in.ReadU32()};
}

void WriteVec2(ByteWriter& out, Vec2 value) {
  out.WriteFloat(value.x);
  out.WriteFloat(value.y);
}

Vec2 ReadVec2(ByteReader& in) {
  Vec2 value;
  value.x = in.ReadFloat();
  value.y = in.ReadFloat();
  return value;
}

}  // namespace

// ---------------------------------------------------------------------------
// ResidentRow — resident_state.h
// ---------------------------------------------------------------------------

void WriteResidentRow(SaveSink& sink, const ResidentRow& row) {
  ByteWriter& out = sink.Out();
  WriteEntityId(out, row.family);
  WriteEntityId(out, row.mother);
  WriteEntityId(out, row.father);
  WriteEntityId(out, row.spouse);
  out.WriteU8(static_cast<std::uint8_t>(row.sex));
  out.WriteI32(row.birth_day);

  out.WriteFloat(row.satiety);
  out.WriteFloat(row.health);
  out.WriteFloat(row.rest);
  out.WriteFloat(row.cold);
  // Personal cleanliness (health design §3, save format 51).
  out.WriteFloat(row.hygiene);
  out.WriteFloat(row.mood);

  out.WriteU8(static_cast<std::uint8_t>(row.work.kind));
  out.WriteU8(row.work.rides_horse);  // save 88: the carter's horse (labor_state.h)
  WriteEntityId(out, row.work.field);
  WriteEntityId(out, row.work.herd);
  WriteEntityId(out, row.work.unit);
  WriteEntityId(out, row.work.stand);
  WriteEntityId(out, row.work.extraction_site);
  out.WriteFloat(row.work.worked_norm_days_today);
  out.WriteFloat(row.work.hours_away_today);

  // The post (task A7). Both halves or neither: a profession without a unit
  // names a groom of nowhere, a unit without a profession names a place
  // nobody holds — and both would load as a resident who is neither in the
  // accountant's pool nor at any work. Refused on the way IN, where the
  // damaged half can still be named.
  sink.WriteDefId(DefKind::kProfession, row.post.profession.value);
  WriteEntityId(out, row.post.unit);

  out.WriteU8(static_cast<std::uint8_t>(row.education_stage));
  out.WriteFloat(row.education_grade);
  out.WriteFloat(row.current_grade);
  WriteEntityId(out, row.school);
  out.WriteFloat(row.self_education);

  out.WriteFloat(row.skill_agriculture_schooled);
  out.WriteFloat(row.skill_agriculture_earned);
  out.WriteFloat(row.skill_technic_schooled);
  out.WriteFloat(row.skill_technic_earned);
  out.WriteFloat(row.skill_admin_schooled);
  out.WriteFloat(row.skill_admin_earned);

  out.WriteFloat(row.intellect);
  out.WriteFloat(row.stamina);
  out.WriteFloat(row.optimism);
  out.WriteFloat(row.ideology);
  out.WriteFloat(row.sportiness);
  out.WriteFloat(row.sport_inclination);

  out.WriteU8(static_cast<std::uint8_t>(row.social_status));
  out.WriteU8(static_cast<std::uint8_t>(row.night_trade));
  out.WriteU32(row.distiller_supplied_month);  // save 59
  out.WriteU8(row.days_worked_this_month);
  out.WriteFloat(row.alcoholism);
  out.WriteU32(row.talk_until_day);  // save 69
  WriteEntityId(out, row.twin);      // save 78
  out.WriteU32(row.away_until_day);  // save 79
  out.WriteFloat(row.crime_inclination);
  out.WriteU16(row.offense_count);
  out.WriteFloat(row.attitude_to_chairman);
  out.WriteU8(row.has_passport);
  out.WriteU8(row.identical_twin);   // save 78
  out.WriteU8(row.away_until_hour);  // save 79
  out.WriteU8(row.away_walk_hours);
  out.WriteU8(row.away_reason);
  out.WriteU16(row.traits);
  out.WriteFloat(row.height_deviation);
  out.WriteFloat(row.build_deviation);
}

ResidentRow ReadResidentRow(LoadSource& source) {
  ByteReader& in = source.In();
  ResidentRow row;
  row.family = ReadEntityId<FamilyId>(in);
  row.mother = ReadEntityId<ResidentId>(in);
  row.father = ReadEntityId<ResidentId>(in);
  row.spouse = ReadEntityId<ResidentId>(in);
  row.sex = static_cast<Sex>(source.ReadEnumValue(0, kMaxSex, "resident sex"));
  row.birth_day = in.ReadI32();

  row.satiety = in.ReadFloat();
  row.health = in.ReadFloat();
  row.rest = in.ReadFloat();
  row.cold = in.ReadFloat();
  row.hygiene = in.ReadFloat();
  row.mood = in.ReadFloat();

  row.work.kind = static_cast<WorkKind>(source.ReadEnumValue(0, kMaxWorkKind, "work kind"));
  row.work.rides_horse = source.ReadEnumValue(0, 1, "the carter's horse mark");
  row.work.field = ReadEntityId<FieldId>(in);
  row.work.herd = ReadEntityId<HerdId>(in);
  row.work.unit = ReadEntityId<UnitId>(in);
  row.work.stand = ReadEntityId<TimberStandId>(in);
  row.work.extraction_site = ReadEntityId<ExtractionSiteId>(in);
  row.work.worked_norm_days_today = in.ReadFloat();
  row.work.hours_away_today = in.ReadFloat();

  row.post.profession = ProfessionId{source.ReadDefId(DefKind::kProfession)};
  row.post.unit = ReadEntityId<UnitId>(in);
  if ((row.post.profession.value == kInvalidDefIdValue) !=
      (row.post.unit.value == kInvalidEntityIdValue)) {
    source.Fail("a post holds one half: a profession without a unit, or a unit without a post");
  }

  row.education_stage =
      static_cast<EducationStage>(source.ReadEnumValue(0, kMaxEducationStage, "education stage"));
  row.education_grade = in.ReadFloat();
  row.current_grade = in.ReadFloat();
  row.school = ReadEntityId<UnitId>(in);
  row.self_education = in.ReadFloat();

  row.skill_agriculture_schooled = in.ReadFloat();
  row.skill_agriculture_earned = in.ReadFloat();
  row.skill_technic_schooled = in.ReadFloat();
  row.skill_technic_earned = in.ReadFloat();
  row.skill_admin_schooled = in.ReadFloat();
  row.skill_admin_earned = in.ReadFloat();

  row.intellect = in.ReadFloat();
  row.stamina = in.ReadFloat();
  row.optimism = in.ReadFloat();
  row.ideology = in.ReadFloat();
  row.sportiness = in.ReadFloat();
  row.sport_inclination = in.ReadFloat();

  row.social_status =
      static_cast<SocialStatus>(source.ReadEnumValue(0, kMaxSocialStatus, "social status"));
  row.night_trade = static_cast<NightTrade>(source.ReadEnumValue(0, kMaxNightTrade, "night trade"));
  row.distiller_supplied_month = in.ReadU32();
  row.days_worked_this_month = in.ReadU8();
  row.alcoholism = in.ReadFloat();
  row.talk_until_day = in.ReadU32();
  row.twin = ReadEntityId<ResidentId>(in);
  row.away_until_day = in.ReadU32();
  row.crime_inclination = in.ReadFloat();
  row.offense_count = in.ReadU16();
  row.attitude_to_chairman = in.ReadFloat();
  row.has_passport = in.ReadU8();
  row.identical_twin = source.ReadEnumValue(0, 1, "the identical twin's mark");
  row.away_until_hour = in.ReadU8();
  row.away_walk_hours = in.ReadU8();
  row.away_reason = source.ReadEnumValue(0, kMaxAwayReason, "the reason for being away");
  row.traits = in.ReadU16();
  row.height_deviation = in.ReadFloat();
  row.build_deviation = in.ReadFloat();
  return row;
}

// ---------------------------------------------------------------------------
// FamilyRow — family_state.h
// ---------------------------------------------------------------------------

void WriteFamilyRow(SaveSink& sink, const FamilyRow& row) {
  ByteWriter& out = sink.Out();
  WriteEntityId(out, row.house);

  out.WriteFloat(row.satisfaction);
  out.WriteFloat(row.component_satiety);
  out.WriteFloat(row.component_common_cause);
  out.WriteFloat(row.component_needs);
  out.WriteFloat(row.component_rest);

  sink.WriteAmounts(DefKind::kResource, row.pantry);
  out.WriteFloat(row.satiety_year_mean);
  out.WriteU16(row.food_variety_mask);
  // Whether the family has eaten yet (2026-09-14): without it a campaign loaded
  // between a wedding and its first meal would judge the new family's variety.
  out.WriteU8(row.first_meal_eaten);
  // A family without a roof (2026-09-14, save format 35): where its house
  // stood, and whether it lives in a tent there.
  WriteVec2(out, row.lost_house_position);
  out.WriteU8(row.in_tent);
  // The certificate asked for, and the house lodged in (save 74).
  out.WriteU8(row.asked_to_leave);
  out.WriteU32(row.asked_day);
  out.WriteU32(row.lodged_in.value);
  out.WriteFloat(row.lodging_penalty);
  out.WriteU8(row.in_barrack);        // save 75
  out.WriteU8(row.hunger_alarm_lit);  // save 75

  out.WriteFloat(row.household_hours);
  out.WriteFloat(row.plot_ratio_sum);
  out.WriteU16(row.plot_ratio_days);
  // The chairman's ration decision for this yard (kSetRation, save 57).
  out.WriteU8(row.ration_granted);
  out.WriteU8(row.dry_months);           // save 60
  out.WriteFloat(row.overwork_penalty);  // save 65: the season's avrals and worked days off
  out.WriteFloat(row.private_plot_share);

  out.WriteI32(row.trudodni_account);
  out.WriteI32(row.trudodni_redeemed);
}

FamilyRow ReadFamilyRow(LoadSource& source) {
  ByteReader& in = source.In();
  FamilyRow row;
  row.house = ReadEntityId<UnitId>(in);

  row.satisfaction = in.ReadFloat();
  row.component_satiety = in.ReadFloat();
  row.component_common_cause = in.ReadFloat();
  row.component_needs = in.ReadFloat();
  row.component_rest = in.ReadFloat();

  row.pantry = source.ReadAmounts(DefKind::kResource);
  row.satiety_year_mean = in.ReadFloat();
  row.food_variety_mask = in.ReadU16();
  row.first_meal_eaten =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "family's first meal eaten"));
  row.lost_house_position = ReadVec2(in);
  row.in_tent = static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "family in a tent"));
  row.asked_to_leave =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "family asked to leave"));
  row.asked_day = in.ReadU32();
  row.lodged_in.value = in.ReadU32();
  row.lodging_penalty = in.ReadFloat();
  row.in_barrack = static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "family in a barrack"));
  row.hunger_alarm_lit =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "family's hunger alarm lit"));

  row.household_hours = in.ReadFloat();
  row.plot_ratio_sum = in.ReadFloat();
  row.plot_ratio_days = in.ReadU16();
  row.ration_granted =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "family's ration granted"));
  row.dry_months = in.ReadU8();
  row.overwork_penalty = in.ReadFloat();
  row.private_plot_share = in.ReadFloat();

  row.trudodni_account = in.ReadI32();
  row.trudodni_redeemed = in.ReadI32();
  return row;
}

// ---------------------------------------------------------------------------
// FieldRow — land_state.h
// ---------------------------------------------------------------------------

void WriteFieldRow(SaveSink& sink, const FieldRow& row) {
  ByteWriter& out = sink.Out();
  WriteVec2(out, row.center);
  out.WriteFloat(row.area_ga);
  out.WriteFloat(row.fertility);
  out.WriteU8(static_cast<std::uint8_t>(row.phase));

  sink.WriteDefId(DefKind::kCrop, row.crop.value);
  sink.WriteDefId(DefKind::kCrop, row.rotation_year0.value);
  sink.WriteDefId(DefKind::kCrop, row.rotation_year1.value);
  sink.WriteDefId(DefKind::kCrop, row.rotation_year2.value);
  sink.WriteDefId(DefKind::kCrop, row.last_crop.value);

  out.WriteU8(row.repeat_years);
  out.WriteU8(row.manure_applied);
  out.WriteU8(static_cast<std::uint8_t>(row.kind));
  // WEEDS AND SOD, AND NOTHING ELSE (land_state.h). It replaced a whole
  // LandKind value on 2026-09-12: an overgrown field used to be a KIND of
  // land that refused every order, and the design says it is a look. Written
  // as the 0/1 it is and read back the same way — see the read side for why
  // that is not the same as trusting the byte.
  out.WriteU8(row.overgrown);
  // WHETHER THE FIELD WAS EVER GIVEN A CHAIN, which the three crop slots
  // beside it cannot say: an empty slot in a chain that exists is a fallow
  // year, and the same emptiness in a field nobody assigned is nothing at
  // all (land_state.h). Lose this byte on a round trip and every unworked
  // hectare comes back as three fallow years — ploughed, recovered and
  // manured for ever after.
  out.WriteU8(row.rotation_assigned);
  // AND WHETHER THE COMING YEAR'S TURN SKIPS THIS FIELD, once. A chain
  // written for the year that is starting stands still at that turn so the
  // crop the chairman named first is the one the next sowing puts in
  // (land_state.h). Lose it on a round trip and a campaign saved in November
  // and loaded in December sows the chairman's SECOND crop — silently, and
  // with his own order sheet in front of him saying otherwise.
  out.WriteU8(row.rotation_skips_turn);
  // Drought and waterlogging, apart. One number could not say which, and
  // the two are cured by opposite things (boss, 2026-09-04).
  out.WriteFloat(row.drought_stress);
  out.WriteFloat(row.wet_stress);
  out.WriteU8(row.drought_run_days);
  out.WriteU8(row.wet_run_days);
  out.WriteU8(static_cast<std::uint8_t>(row.weather_state));
  out.WriteFloat(row.work_days_remaining);
  // The field brigade's buffer (task A3): what was reaped and has not
  // reached a store, and what it is. History the simulation cannot rederive.
  out.WriteFloat(row.haul_days_remaining);
  // The settlement's memory of what it wrote last night (task: the seventh
  // reconciliation pass). sizeof(FieldRow) did NOT move — the float landed in
  // padding the row already had, and the tripwire stayed silent. Fourth time
  // in a row, which is the ordinary outcome and not bad luck: a field added
  // to a row almost always lands in a hole, because holes are what sit
  // between fields of different widths.
  out.WriteFloat(row.haul_days_written);
  out.WriteI64(row.reaped_grams);
  sink.WriteDefId(DefKind::kResource, row.reaped_resource.value);
  // The harvest by parts (save 84): the share of this reaping already laid
  // into the heap, and its grams.
  out.WriteFloat(row.harvest_laid_share);
  out.WriteFloat(row.sown_share);  // save 87
  out.WriteI64(row.harvest_laid_grams);
  // The day this meadow was last mown (2026-09-06). History the simulation
  // cannot rederive: from today alone there is no telling a meadow standing
  // since spring from one cut a week ago, and the flowering the layer paints
  // is exactly that difference.
  out.WriteU32(row.last_mown_day);
  // The day the seed went in (2026-09-13). History the simulation cannot
  // rederive either, and the whole ripening rule is measured from it: a world
  // loaded without it would reap on the calendar alone, which is the behaviour
  // the rule removes.
  out.WriteU32(row.sown_day);
  // Ploughed last autumn and owing no spring furrow (2026-09-13). Set at
  // genesis and spent by the first ploughing, so a save taken in the first
  // spring has to carry which fields still hold it.
  out.WriteU8(row.autumn_plowed);
  // The judgement itself is saved beside the day it comes from, exactly as
  // weather_state is saved beside its counters: a world just loaded has to
  // be paintable before it has stepped once.
  out.WriteU8(row.in_flower ? 1U : 0U);
  // The start's reserve field (2026-09-14). Set once at genesis and read by
  // nothing but its removal, so a save without it would let the start quest's
  // field go unnoticed: removed, and the fact never raised.
  out.WriteU8(row.start_reserve);
  // The day the crop was last reaped whole (2026-09-15). The seed fund reads
  // it, and an idle field has no other trace of the harvest it gave: a world
  // loaded without it would reserve this year's seed for a crop already in
  // the stores.
  out.WriteU32(row.reaped_day);
  // The avral on the field's work and the phase it stands on (save 65).
  out.WriteU8(row.rush_step);
  out.WriteU8(static_cast<std::uint8_t>(row.rush_phase));
}

FieldRow ReadFieldRow(LoadSource& source) {
  ByteReader& in = source.In();
  FieldRow row;
  row.center = ReadVec2(in);
  row.area_ga = in.ReadFloat();
  row.fertility = in.ReadFloat();
  row.phase = static_cast<FieldPhase>(source.ReadEnumValue(0, kMaxFieldPhase, "field phase"));

  row.crop = CropId{source.ReadDefId(DefKind::kCrop)};
  row.rotation_year0 = CropId{source.ReadDefId(DefKind::kCrop)};
  row.rotation_year1 = CropId{source.ReadDefId(DefKind::kCrop)};
  row.rotation_year2 = CropId{source.ReadDefId(DefKind::kCrop)};
  row.last_crop = CropId{source.ReadDefId(DefKind::kCrop)};

  row.repeat_years = in.ReadU8();
  row.manure_applied = in.ReadU8();
  row.kind = static_cast<LandKind>(source.ReadEnumValue(0, kMaxLandKind, "land kind"));
  // NARROWED TO 0/1 ON THE WAY IN. The byte's domain is two values and a
  // save can carry any of two hundred and fifty-six. Nothing in the core
  // branches on it — it is a look — so a stray 7 would harm no arithmetic,
  // and that is exactly why it would never be caught: the layer would paint
  // weeds on a ploughed field and nothing would ever say why.
  //
  // A first draft of this comment claimed `paused` and `dead` beside it do
  // the same. They do not — both are read raw — and one of them is already
  // in the open-items list for it. Naming a neighbour as precedent without
  // reading the neighbour is how a practice gets invented backwards.
  row.overgrown = in.ReadU8() != 0 ? 1U : 0U;
  // Narrowed the same way and for the same reason: the domain is two values
  // and a save can carry any of two hundred and fifty-six. Unlike `overgrown`
  // above, the core DOES branch on this one — in five places — so a stray
  // byte here would not merely paint weeds, it would decide whether ninety
  // three hectares are farmed.
  row.rotation_assigned = in.ReadU8() != 0 ? 1U : 0U;
  // Narrowed like its two neighbours, and read raw it would decide whether a
  // year's rotation happens at all — the one write in the core that the
  // player can see in his own order sheet.
  row.rotation_skips_turn = in.ReadU8() != 0 ? 1U : 0U;
  row.drought_stress = in.ReadFloat();
  row.wet_stress = in.ReadFloat();
  row.drought_run_days = in.ReadU8();
  row.wet_run_days = in.ReadU8();
  row.weather_state = static_cast<FieldWeatherState>(
      source.ReadEnumValue(0, kMaxFieldWeatherState, "field weather state"));
  row.work_days_remaining = in.ReadFloat();
  row.haul_days_remaining = in.ReadFloat();
  row.haul_days_written = in.ReadFloat();
  row.reaped_grams = in.ReadI64();
  row.reaped_resource = ResourceId{source.ReadDefId(DefKind::kResource)};
  row.harvest_laid_share = in.ReadFloat();
  row.sown_share = in.ReadFloat();
  // A share, and the yield is multiplied by it: outside 0..1 a loaded field
  // would grow a crop nobody sowed, or less than none.
  if (!(row.sown_share >= 0.0F && row.sown_share <= 1.0F)) {
    source.Fail("a field's sown share is outside 0..1");
  }
  row.harvest_laid_grams = in.ReadI64();
  row.last_mown_day = in.ReadU32();
  row.sown_day = in.ReadU32();
  row.autumn_plowed = static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "autumn ploughed"));
  row.in_flower = source.ReadEnumValue(0, 1, "meadow in flower") != 0;
  row.start_reserve = static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "start reserve field"));
  row.reaped_day = in.ReadU32();
  row.rush_step =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, kMaxRushStepByte, "field's avral step"));
  row.rush_phase =
      static_cast<FieldPhase>(source.ReadEnumValue(0, kMaxFieldPhase, "field's avral phase"));
  return row;
}

// ---------------------------------------------------------------------------
// UnitRow — unit_state.h
// ---------------------------------------------------------------------------

void WriteUnitRow(SaveSink& sink, const UnitRow& row) {
  ByteWriter& out = sink.Out();
  sink.WriteDefId(DefKind::kUnitType, row.type.value);
  WriteVec2(out, row.position);
  out.WriteU8(row.level);
  WriteEntityId(out, row.household);
  sink.WriteAmounts(DefKind::kResource, row.stock);

  // The site block (task A2): a unit under construction is a unit row, so
  // its site is fields of this row and saves with it.
  out.WriteU8(static_cast<std::uint8_t>(row.construction.phase));
  out.WriteU8(row.construction.target_level);
  out.WriteFloat(row.construction.labor_days_total);
  out.WriteFloat(row.construction.labor_days_remaining);
  out.WriteU8(row.construction.max_crew);
  out.WriteU8(row.construction.rush_step);     // save 65: the avral on the step
  out.WriteU8(row.construction.winter_works);  // save 80: the site's winter class
  // What the works hold back of a standing unit's stock (2026-09-14,
  // VERSION_SAVE 37): the upgrade's carried-in recipe, which the stock
  // alone cannot tell from the store's own goods.
  sink.WriteAmounts(DefKind::kResource, row.construction.reserved);

  // Wear (task A5): the building's own age, which nothing can rederive.
  out.WriteFloat(row.wear);
  // The pause byte went into padding the row already had, so sizeof(UnitRow)
  // did NOT move and the tripwire above stayed silent — the same trap task
  // A7 sprang with OrderRow::profession. The wire grew all the same, and
  // that is what VERSION_SAVE counts.
  out.WriteU8(row.paused);
  // The store's emptying and its carrying seam (save 67).
  out.WriteU8(row.emptying);
  out.WriteFloat(row.haul_days_remaining);
  out.WriteFloat(row.haul_days_written);
  // The dead byte of a start placement (2026-09-12) went into the same
  // padding, and the tripwire stayed silent for the third time running —
  // which is the documented usual outcome, not the surprise. VERSION_SAVE
  // is 20 for it.
  out.WriteU8(row.dead);
  out.WriteU8(row.reserved_for_specialist);  // save 74
  out.WriteU8(row.insulated);                // unit rules §16, VERSION_SAVE 44
  out.WriteFloat(row.stink_radius_m);
  // Modules and the production seam (2026-09-13, VERSION_SAVE 30).
  WriteEntityId(out, row.parent);
  out.WriteFloat(row.production_days_remaining);
  out.WriteFloat(row.production_days_written);
}

UnitRow ReadUnitRow(LoadSource& source) {
  ByteReader& in = source.In();
  UnitRow row;
  row.type = UnitTypeId{source.ReadDefId(DefKind::kUnitType)};
  row.position = ReadVec2(in);
  row.level = in.ReadU8();
  row.household = ReadEntityId<FamilyId>(in);
  row.stock = source.ReadAmounts(DefKind::kResource);

  row.construction.phase = static_cast<ConstructionPhase>(
      source.ReadEnumValue(0, kMaxConstructionPhase, "construction phase"));
  row.construction.target_level = in.ReadU8();
  row.construction.labor_days_total = in.ReadFloat();
  row.construction.labor_days_remaining = in.ReadFloat();
  row.construction.max_crew = in.ReadU8();
  row.construction.rush_step =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, kMaxRushStepByte, "unit's avral step"));
  // Through the domain check and not as a raw byte: a 0/1 flag read raw is
  // the open UB-002's shape, and this one decides whether a crew is sent.
  row.construction.winter_works =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "site's winter class"));
  row.construction.reserved = source.ReadAmounts(DefKind::kResource);
  row.wear = in.ReadFloat();
  row.paused = in.ReadU8();
  // 0 none, 1 emptying, 2 emptying with its carrying paused (boss seq 119).
  row.emptying = static_cast<std::uint8_t>(source.ReadEnumValue(0, 2, "store emptying"));
  row.haul_days_remaining = in.ReadFloat();
  row.haul_days_written = in.ReadFloat();
  row.dead = in.ReadU8();
  row.reserved_for_specialist =
      static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "house held for a specialist"));
  row.insulated = in.ReadU8();
  row.stink_radius_m = in.ReadFloat();
  row.parent = ReadEntityId<UnitId>(in);
  row.production_days_remaining = in.ReadFloat();
  row.production_days_written = in.ReadFloat();
  return row;
}

// ---------------------------------------------------------------------------
// HerdRow — herd_state.h
// ---------------------------------------------------------------------------

void WriteHerdRow(SaveSink& sink, const HerdRow& row) {
  ByteWriter& out = sink.Out();
  sink.WriteDefId(DefKind::kLivestock, row.kind.value);
  WriteEntityId(out, row.unit);
  WriteEntityId(out, row.household);
  out.WriteU8(row.household_owned);

  out.WriteU16(row.newborn_count);
  out.WriteU16(row.juvenile_count);
  out.WriteU16(row.adult_count);
  out.WriteU16(row.adult_male_count);

  out.WriteFloat(row.newborn_progress);
  out.WriteFloat(row.juvenile_progress);
  out.WriteFloat(row.birth_progress);
  out.WriteFloat(row.cull_progress);
  out.WriteFloat(row.hunger_progress);
  out.WriteFloat(row.adult_age_game_years_total);
  out.WriteFloat(row.adult_age_min_game_years);  // save 91
  out.WriteFloat(row.adult_age_max_game_years);  // save 91

  out.WriteU16(row.billeted_count);
  out.WriteFloat(row.unfed_days);
  out.WriteFloat(row.fed_share);  // save 71
  out.WriteU8(row.disease_stage);
  out.WriteU8(row.autumn_slaughter_done);  // save 76
  out.WriteFloat(row.care_days_remaining);
}

HerdRow ReadHerdRow(LoadSource& source) {
  ByteReader& in = source.In();
  HerdRow row;
  row.kind = LivestockKindId{source.ReadDefId(DefKind::kLivestock)};
  row.unit = ReadEntityId<UnitId>(in);
  row.household = ReadEntityId<FamilyId>(in);
  row.household_owned = in.ReadU8();

  row.newborn_count = in.ReadU16();
  row.juvenile_count = in.ReadU16();
  row.adult_count = in.ReadU16();
  row.adult_male_count = in.ReadU16();

  row.newborn_progress = in.ReadFloat();
  row.juvenile_progress = in.ReadFloat();
  row.birth_progress = in.ReadFloat();
  row.cull_progress = in.ReadFloat();
  row.hunger_progress = in.ReadFloat();
  row.adult_age_game_years_total = in.ReadFloat();
  row.adult_age_min_game_years = in.ReadFloat();
  row.adult_age_max_game_years = in.ReadFloat();

  row.billeted_count = in.ReadU16();
  row.unfed_days = in.ReadFloat();
  row.fed_share = in.ReadFloat();
  row.disease_stage = in.ReadU8();
  row.autumn_slaughter_done = in.ReadU8();
  row.care_days_remaining = in.ReadFloat();
  return row;
}

// ---------------------------------------------------------------------------
// OrderRow — order_state.h
// ---------------------------------------------------------------------------
//
// The order book is saved because an order that waits is state: the design
// defers nearly everything the chairman decides, shows it as pending and
// lets it be taken back (manual/70-boundary.md §2). A campaign saved with
// an order waiting must resume with it waiting.

void WriteOrderRow(SaveSink& sink, const OrderRow& row) {
  ByteWriter& out = sink.Out();
  out.WriteU8(static_cast<std::uint8_t>(row.kind));
  out.WriteU8(static_cast<std::uint8_t>(row.status));
  out.WriteU8(static_cast<std::uint8_t>(row.refusal));
  out.WriteU8(static_cast<std::uint8_t>(row.work));
  out.WriteU64(row.issued_tick);

  WriteEntityId(out, row.resident);
  WriteEntityId(out, row.unit);
  WriteEntityId(out, row.field);
  WriteEntityId(out, row.herd);

  sink.WriteDefId(DefKind::kUnitType, row.unit_type.value);
  sink.WriteDefId(DefKind::kProfession, row.profession.value);
  sink.WriteDefId(DefKind::kCrop, row.rotation_year0.value);
  sink.WriteDefId(DefKind::kCrop, row.rotation_year1.value);
  sink.WriteDefId(DefKind::kCrop, row.rotation_year2.value);

  WriteVec2(out, row.position);

  // The unsealing (kUnsealFund, 2026-09-12). The resource goes through the
  // DICTIONARY like every other definition id in a save — unlike the
  // journal's raw one — so that a reordered resources.csv does not turn the
  // chairman's grain into somebody else's.
  out.WriteU8(static_cast<std::uint8_t>(row.fund));
  sink.WriteDefId(DefKind::kResource, row.resource.value);
  out.WriteU64(static_cast<std::uint64_t>(row.amount));

  // The felling mark (kMarkFelling, 2026-09-13).
  WriteEntityId(out, row.stand);
  out.WriteFloat(row.volume_m3);
  // The planting (kPlantForest, save 82): the zone's hectares and species.
  out.WriteFloat(row.area_ha);
  sink.WriteDefId(DefKind::kTreeSpecies, row.species.value);

  // The limit lot (kOrderLimitLot, 2026-09-13), through the dictionary.
  sink.WriteDefId(DefKind::kLimitLot, row.lot.value);

  // The extraction mark (kMarkExtraction, 2026-09-14); its mass is `amount`.
  WriteEntityId(out, row.extraction_site);

  // The sex of a head bought on the limit (2026-09-16). A raw byte and not a
  // dictionary id: it is a fact about the order, not a name of anything in
  // the tables.
  out.WriteU8(row.male);

  // The ration's switch (kSetRation, 2026-09-18, save 57): the family and
  // the 0/1 byte.
  WriteEntityId(out, row.family);
  out.WriteU8(row.enable);
}

OrderRow ReadOrderRow(LoadSource& source) {
  ByteReader& in = source.In();
  OrderRow row;
  row.kind = static_cast<OrderKind>(source.ReadEnumValue(0, kMaxOrderKind, "order kind"));
  row.status = static_cast<OrderStatus>(source.ReadEnumValue(0, kMaxOrderStatus, "order status"));
  row.refusal =
      static_cast<OrderRefusal>(source.ReadEnumValue(0, kMaxOrderRefusal, "order refusal"));
  row.work = static_cast<WorkKind>(source.ReadEnumValue(0, kMaxWorkKind, "order work kind"));
  row.issued_tick = in.ReadU64();

  row.resident = ReadEntityId<ResidentId>(in);
  row.unit = ReadEntityId<UnitId>(in);
  row.field = ReadEntityId<FieldId>(in);
  row.herd = ReadEntityId<HerdId>(in);

  row.unit_type = UnitTypeId{source.ReadDefId(DefKind::kUnitType)};
  row.profession = ProfessionId{source.ReadDefId(DefKind::kProfession)};
  row.rotation_year0 = CropId{source.ReadDefId(DefKind::kCrop)};
  row.rotation_year1 = CropId{source.ReadDefId(DefKind::kCrop)};
  row.rotation_year2 = CropId{source.ReadDefId(DefKind::kCrop)};

  row.position = ReadVec2(in);

  row.fund = static_cast<FundKind>(source.ReadEnumValue(0, kMaxFundKind, "fund kind"));
  row.resource = ResourceId{source.ReadDefId(DefKind::kResource)};
  row.amount = static_cast<Grams>(in.ReadU64());
  row.stand = ReadEntityId<TimberStandId>(in);
  row.volume_m3 = in.ReadFloat();
  row.area_ha = in.ReadFloat();
  row.species = TreeSpeciesId{source.ReadDefId(DefKind::kTreeSpecies)};
  row.lot = LimitLotId{source.ReadDefId(DefKind::kLimitLot)};
  row.extraction_site = ReadEntityId<ExtractionSiteId>(in);
  row.male = in.ReadU8();
  row.family = ReadEntityId<FamilyId>(in);
  row.enable = static_cast<std::uint8_t>(source.ReadEnumValue(0, 1, "order ration switch"));
  return row;
}

// ---------------------------------------------------------------------------
// TimberStandRow — timber_state.h (2026-09-13)
// ---------------------------------------------------------------------------
//
// `table_row` goes out raw, not through a dictionary: tables/timber_stands.csv
// is a baked export of the map, not a registry the player's campaign names
// things by, and a stand made from row 7 means row 7 of the same bake. THE
// LOADER DOES NOT CHECK THE ROW (MEM-201, 2026-09-13): a save from a bake
// with fewer rows loads, and production's own index check (timber_felling.cpp,
// DefOf) then leaves such a stand without logs from a felling and without
// old trunks. A bake that merely reorders its rows is not caught at all. The
// stand's own kind and loading point are saved beside the row so that the
// accountant's road and the layer's marker never depend on the bake.

void WriteTimberStandRow(SaveSink& sink, const TimberStandRow& row) {
  ByteWriter& out = sink.Out();
  out.WriteU32(row.table_row);
  out.WriteU8(static_cast<std::uint8_t>(row.kind));
  WriteVec2(out, row.position);
  out.WriteFloat(row.stock_m3);
  out.WriteFloat(row.marked_m3);
  out.WriteFloat(row.work_days_remaining);
  out.WriteU64(static_cast<std::uint64_t>(row.load_grams));
  out.WriteFloat(row.haul_days_remaining);
  out.WriteFloat(row.haul_days_written);
  // A planting (save 82): its species through the dictionary, its own
  // hectares, and its two days.
  sink.WriteDefId(DefKind::kTreeSpecies, row.species.value);
  out.WriteFloat(row.planted_area_ha);
  out.WriteU32(row.planted_day);
  out.WriteU32(row.matures_day);
}

TimberStandRow ReadTimberStandRow(LoadSource& source) {
  ByteReader& in = source.In();
  TimberStandRow row;
  row.table_row = in.ReadU32();
  row.kind = static_cast<TimberStandKind>(
      source.ReadEnumValue(0, kMaxTimberStandKind, "timber stand kind"));
  row.position = ReadVec2(in);
  row.stock_m3 = in.ReadFloat();
  row.marked_m3 = in.ReadFloat();
  row.work_days_remaining = in.ReadFloat();
  row.load_grams = static_cast<Grams>(in.ReadU64());
  row.haul_days_remaining = in.ReadFloat();
  row.haul_days_written = in.ReadFloat();
  row.species = TreeSpeciesId{source.ReadDefId(DefKind::kTreeSpecies)};
  row.planted_area_ha = in.ReadFloat();
  row.planted_day = in.ReadU32();
  row.matures_day = in.ReadU32();
  return row;
}

// ---------------------------------------------------------------------------
// ExtractionSiteRow — extraction_state.h (2026-09-14)
// ---------------------------------------------------------------------------
//
// `table_row` goes out raw, as a stand's does and for the same reason:
// tables/extraction_sites.csv is a baked export of the map. The resource goes
// through the dictionary like every other definition id, and the loading
// point is saved beside the row so the road never depends on the bake.

void WriteExtractionSiteRow(SaveSink& sink, const ExtractionSiteRow& row) {
  ByteWriter& out = sink.Out();
  out.WriteU32(row.table_row);
  sink.WriteDefId(DefKind::kResource, row.resource.value);
  WriteVec2(out, row.position);
  out.WriteU64(static_cast<std::uint64_t>(row.stock_grams));
  out.WriteU64(static_cast<std::uint64_t>(row.marked_grams));
  out.WriteFloat(row.work_days_remaining);
  out.WriteU64(static_cast<std::uint64_t>(row.load_grams));
  out.WriteFloat(row.haul_days_remaining);
  out.WriteFloat(row.haul_days_written);
  out.WriteU8(row.exhausted);
}

ExtractionSiteRow ReadExtractionSiteRow(LoadSource& source) {
  ByteReader& in = source.In();
  ExtractionSiteRow row;
  row.table_row = in.ReadU32();
  row.resource = ResourceId{source.ReadDefId(DefKind::kResource)};
  row.position = ReadVec2(in);
  row.stock_grams = static_cast<Grams>(in.ReadU64());
  row.marked_grams = static_cast<Grams>(in.ReadU64());
  row.work_days_remaining = in.ReadFloat();
  row.load_grams = static_cast<Grams>(in.ReadU64());
  row.haul_days_remaining = in.ReadFloat();
  row.haul_days_written = in.ReadFloat();
  row.exhausted = in.ReadU8();
  return row;
}

// ---------------------------------------------------------------------------
// LimitDeliveryRow — limit_state.h (2026-09-13)
// ---------------------------------------------------------------------------
// The lot through the dictionary, so a reordered catalogue keeps a cart's
// name; the goods are frozen grams by resource and go out as amounts.

void WriteLimitDeliveryRow(SaveSink& sink, const LimitDeliveryRow& row) {
  ByteWriter& out = sink.Out();
  sink.WriteDefId(DefKind::kLimitLot, row.lot.value);
  out.WriteU32(row.arrive_day);
  sink.WriteAmounts(DefKind::kResource, row.goods);
}

LimitDeliveryRow ReadLimitDeliveryRow(LoadSource& source) {
  ByteReader& in = source.In();
  LimitDeliveryRow row;
  row.lot = LimitLotId{source.ReadDefId(DefKind::kLimitLot)};
  row.arrive_day = in.ReadU32();
  row.goods = source.ReadAmounts(DefKind::kResource);
  return row;
}

// ---------------------------------------------------------------------------
// LivestockArrivalRow — limit_state.h (2026-09-16)
// ---------------------------------------------------------------------------
// The lot AND the kind both go through the dictionary: a reordered catalogue
// or a reordered livestock.csv must not turn a bought horse into somebody
// else's cow while it is on its way.

void WriteLivestockArrivalRow(SaveSink& sink, const LivestockArrivalRow& row) {
  ByteWriter& out = sink.Out();
  sink.WriteDefId(DefKind::kLimitLot, row.lot.value);
  sink.WriteDefId(DefKind::kLivestock, row.kind.value);
  out.WriteU16(row.head_count);
  out.WriteU32(row.arrive_day);
  out.WriteU8(static_cast<std::uint8_t>(row.stage));
  out.WriteU8(row.male);
}

LivestockArrivalRow ReadLivestockArrivalRow(LoadSource& source) {
  ByteReader& in = source.In();
  LivestockArrivalRow row;
  row.lot = LimitLotId{source.ReadDefId(DefKind::kLimitLot)};
  row.kind = LivestockKindId{source.ReadDefId(DefKind::kLivestock)};
  row.head_count = in.ReadU16();
  row.arrive_day = in.ReadU32();
  row.stage = static_cast<LivestockArrivalStage>(
      source.ReadEnumValue(0, kMaxLivestockArrivalStage, "livestock arrival stage"));
  row.male = in.ReadU8();
  return row;
}

// ---------------------------------------------------------------------------
// SpecialistArrivalRow — specialist_state.h (2026-09-14)
// ---------------------------------------------------------------------------
// The post through the dictionary, so a reordered roster keeps his name.

void WriteSpecialistArrivalRow(SaveSink& sink, const SpecialistArrivalRow& row) {
  ByteWriter& out = sink.Out();
  sink.WriteDefId(DefKind::kProfession, row.profession.value);
  WriteEntityId(out, row.unit);
  out.WriteU32(row.arrive_day);
}

// ---------------------------------------------------------------------------
// WeddingWaitRow — wedding_state.h (2026-09-14)
// ---------------------------------------------------------------------------

void WriteWeddingWaitRow(SaveSink& sink, const WeddingWaitRow& row) {
  ByteWriter& out = sink.Out();
  WriteEntityId(out, row.bride);
  WriteEntityId(out, row.groom);
  out.WriteU32(row.since_day);
}

WeddingWaitRow ReadWeddingWaitRow(LoadSource& source) {
  ByteReader& in = source.In();
  WeddingWaitRow row;
  row.bride = ReadEntityId<ResidentId>(in);
  row.groom = ReadEntityId<ResidentId>(in);
  row.since_day = in.ReadU32();
  return row;
}

SpecialistArrivalRow ReadSpecialistArrivalRow(LoadSource& source) {
  ByteReader& in = source.In();
  SpecialistArrivalRow row;
  row.profession = ProfessionId{source.ReadDefId(DefKind::kProfession)};
  row.unit = ReadEntityId<UnitId>(in);
  row.arrive_day = in.ReadU32();
  return row;
}

// ---------------------------------------------------------------------------
// NightOutingRow — night_trade_state.h (2026-09-15)
// ---------------------------------------------------------------------------

void WriteNightOutingRow(SaveSink& sink, const NightOutingRow& row) {
  ByteWriter& out = sink.Out();
  WriteEntityId(out, row.resident);
  out.WriteU8(static_cast<std::uint8_t>(row.trade));
  out.WriteU32(row.day);
  out.WriteFloat(row.position.x);
  out.WriteFloat(row.position.y);
  out.WriteU8(row.hour_out);
  out.WriteU8(row.hour_back);
}

NightOutingRow ReadNightOutingRow(LoadSource& source) {
  ByteReader& in = source.In();
  NightOutingRow row;
  row.resident = ReadEntityId<ResidentId>(in);
  row.trade = static_cast<NightTrade>(source.ReadEnumValue(0, kMaxNightTrade, "night trade"));
  row.day = in.ReadU32();
  row.position.x = in.ReadFloat();
  row.position.y = in.ReadFloat();
  row.hour_out = static_cast<std::uint8_t>(source.ReadEnumValue(0, 23, "night outing hour"));
  row.hour_back = static_cast<std::uint8_t>(source.ReadEnumValue(0, 23, "night outing hour"));
  return row;
}

// ---------------------------------------------------------------------------
// DistrictVisitRow — district_visit_state.h (2026-09-15)
// ---------------------------------------------------------------------------
// The face, kind and cause as their enum values: they are vocabulary of the
// core, not rows of a table, so there is no dictionary to go through.

void WriteDistrictVisitRow(SaveSink& sink, const DistrictVisitRow& row) {
  ByteWriter& out = sink.Out();
  out.WriteU32(row.arrive_day);
  out.WriteU8(static_cast<std::uint8_t>(row.face));
  out.WriteU8(static_cast<std::uint8_t>(row.kind));
  out.WriteU8(static_cast<std::uint8_t>(row.cause));
}

void WriteDistrictCarRow(SaveSink& sink, const DistrictCarRow& row) {
  ByteWriter& out = sink.Out();
  out.WriteU8(static_cast<std::uint8_t>(row.kind));
  out.WriteU8(static_cast<std::uint8_t>(row.phase));
  WriteEntityId(out, row.resident);
  out.WriteU64(row.arrive_tick);
  out.WriteU64(row.leave_tick);
}

DistrictCarRow ReadDistrictCarRow(LoadSource& source) {
  ByteReader& in = source.In();
  DistrictCarRow row;
  row.kind = static_cast<DistrictCarKind>(
      source.ReadEnumValue(0, kMaxDistrictCarKind, "district car kind"));
  row.phase = static_cast<DistrictCarPhase>(
      source.ReadEnumValue(0, kMaxDistrictCarPhase, "district car phase"));
  row.resident = ReadEntityId<ResidentId>(in);
  row.arrive_tick = in.ReadU64();
  row.leave_tick = in.ReadU64();
  return row;
}

DistrictVisitRow ReadDistrictVisitRow(LoadSource& source) {
  ByteReader& in = source.In();
  DistrictVisitRow row;
  row.arrive_day = in.ReadU32();
  row.face = static_cast<DistrictFace>(source.ReadEnumValue(0, kMaxDistrictFace, "district face"));
  row.kind = static_cast<DistrictVisitKind>(
      source.ReadEnumValue(0, kMaxDistrictVisitKind, "district visit kind"));
  row.cause = static_cast<DistrictVisitCause>(
      source.ReadEnumValue(0, kMaxDistrictVisitCause, "district visit cause"));
  return row;
}

}  // namespace core
