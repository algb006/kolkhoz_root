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

#include "aggregate_arity.h"
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
static_assert(sizeof(ResidentRow) == 172,
              "ResidentRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<ResidentRow>() == 38,
              "ResidentRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(FamilyRow) == 56 + kAmountsSize,
              "FamilyRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<FamilyRow>() == 15,
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
// And 80 is MEASURED, not reasoned: 76 was the obvious answer from adding a
// byte to 72 plus padding, and it was wrong. A size guessed to satisfy a
// guard teaches the guard the guess.
static_assert(sizeof(FieldRow) == 80, "FieldRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<FieldRow>() == 24,
              "FieldRow gained or lost a field — update the codec and VERSION_SAVE");
// 2026-09-06: the stink radius pushed the row from 48 + amounts to 56 +
// amounts. The pause byte before it had landed in padding and moved nothing,
// which is the whole reason both asserts stand here.
static_assert(sizeof(UnitRow) == 56 + kAmountsSize,
              "UnitRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<UnitRow>() == 10,
              "UnitRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(HerdRow) == 64, "HerdRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<HerdRow>() == 18,
              "HerdRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(OrderRow) == 64, "OrderRow changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<OrderRow>() == 18,
              "OrderRow gained or lost a field — update the codec and VERSION_SAVE");
static_assert(sizeof(WorkAssignment) == 24,
              "WorkAssignment changed — update the codec and VERSION_SAVE");
static_assert(AggregateArity<WorkAssignment>() == 6,
              "WorkAssignment gained or lost a field — update the codec and VERSION_SAVE");

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

constexpr std::uint8_t kMaxFieldPhase = static_cast<std::uint8_t>(FieldPhase::kFieldPhaseCount) - 1;
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
  out.WriteFloat(row.mood);

  out.WriteU8(static_cast<std::uint8_t>(row.work.kind));
  WriteEntityId(out, row.work.field);
  WriteEntityId(out, row.work.herd);
  WriteEntityId(out, row.work.unit);
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
  out.WriteFloat(row.alcoholism);
  out.WriteFloat(row.crime_inclination);
  out.WriteU16(row.offense_count);
  out.WriteFloat(row.attitude_to_chairman);
  out.WriteU8(row.has_passport);
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
  row.mood = in.ReadFloat();

  row.work.kind = static_cast<WorkKind>(source.ReadEnumValue(0, kMaxWorkKind, "work kind"));
  row.work.field = ReadEntityId<FieldId>(in);
  row.work.herd = ReadEntityId<HerdId>(in);
  row.work.unit = ReadEntityId<UnitId>(in);
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
  row.alcoholism = in.ReadFloat();
  row.crime_inclination = in.ReadFloat();
  row.offense_count = in.ReadU16();
  row.attitude_to_chairman = in.ReadFloat();
  row.has_passport = in.ReadU8();
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

  out.WriteFloat(row.household_hours);
  out.WriteFloat(row.plot_ratio_sum);
  out.WriteU16(row.plot_ratio_days);
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

  row.household_hours = in.ReadFloat();
  row.plot_ratio_sum = in.ReadFloat();
  row.plot_ratio_days = in.ReadU16();
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
  // The day this meadow was last mown (2026-09-06). History the simulation
  // cannot rederive: from today alone there is no telling a meadow standing
  // since spring from one cut a week ago, and the flowering the layer paints
  // is exactly that difference.
  out.WriteU32(row.last_mown_day);
  // The judgement itself is saved beside the day it comes from, exactly as
  // weather_state is saved beside its counters: a world just loaded has to
  // be paintable before it has stepped once.
  out.WriteU8(row.in_flower ? 1U : 0U);
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
  row.last_mown_day = in.ReadU32();
  row.in_flower = source.ReadEnumValue(0, 1, "meadow in flower") != 0;
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

  // Wear (task A5): the building's own age, which nothing can rederive.
  out.WriteFloat(row.wear);
  // The pause byte went into padding the row already had, so sizeof(UnitRow)
  // did NOT move and the tripwire above stayed silent — the same trap task
  // A7 sprang with OrderRow::profession. The wire grew all the same, and
  // that is what VERSION_SAVE counts.
  out.WriteU8(row.paused);
  // The dead byte of a start placement (2026-09-12) went into the same
  // padding, and the tripwire stayed silent for the third time running —
  // which is the documented usual outcome, not the surprise. VERSION_SAVE
  // is 20 for it.
  out.WriteU8(row.dead);
  out.WriteFloat(row.stink_radius_m);
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
  row.wear = in.ReadFloat();
  row.paused = in.ReadU8();
  row.dead = in.ReadU8();
  row.stink_radius_m = in.ReadFloat();
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

  out.WriteU16(row.billeted_count);
  out.WriteFloat(row.unfed_days);
  out.WriteU8(row.disease_stage);
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

  row.billeted_count = in.ReadU16();
  row.unfed_days = in.ReadFloat();
  row.disease_stage = in.ReadU8();
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
  return row;
}

}  // namespace core
