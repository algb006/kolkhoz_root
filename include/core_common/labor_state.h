/// @file
/// @brief WorkKind and WorkAssignment — the per-resident labor state.
/// @threading PARALLEL_READONLY
/// The assignment block is embedded in ResidentRow (resident_state.h) and
/// follows its discipline: in phase 1 every write is sequential — the labor
/// sub-step of the decisions slot assigns in the morning, drains hourly and
/// closes accrual at day end, all on the sim thread. WHICH BUFFER A PARALLEL
/// PHASE READS IT FROM follows from where that phase sits relative to the
/// decisions slot (phase 3), and both answers are in use: the needs phase
/// (2) runs BEFORE the block is written this step and must read `previous`,
/// or it would see the previous step's values under a name that promises
/// today's (core_residents/family_meal.cpp takes WorkedHeavy that way and
/// says why); the metrics phase (6) runs AFTER, and reads `current` for the
/// rows its own item owns (core_residents/household_plot.cpp reads
/// hours_away_today), which is buffer-law rule 4 and not an exception to it.
///
/// Design sources: time design §6-§8 and §11 (the workday by the sun, the
/// road limit, the fatigue walk-off), society design §1 (the accountant's
/// placement), labor-payment §2 (a trudoden is a WORK NORM, not attendance:
/// an assigned but idle worker earns nothing — the boss digest of
/// 2026-08-29 §2.4 confirms), livestock design §5 (a resident hosting a
/// kolkhoz horse takes only horse work until the kolkhoz yard exists).
///
/// The model itself — what generates jobs, how the day window and the norm
/// conversion work — is the core's own decision, manual/65-labor-model.md.

#ifndef CORE_COMMON_LABOR_STATE_H_
#define CORE_COMMON_LABOR_STATE_H_

#include <cstdint>

#include "core_common/ids.h"

namespace core {

/// @brief What a resident is assigned to today. The four field kinds mirror
/// the working phases of FieldPhase (land_state.h); kHerdCare is the daily
/// barn work at a unit-standing herd; kConstruction is a day on a site
/// (unit_state.h, ConstructionState). Values are also the row keys of the
/// work-rate table (tables/labor.csv): the trudoden rate and the hardness of
/// each kind are balance data, never code. Kinds are APPENDED, never
/// renumbered: the value is saved (VERSION_SAVE) and indexes the rate
/// table, the ledger's per-kind array and the report's name list — a kind
/// added here is added in those three places in the same commit.
enum class WorkKind : std::uint8_t {
  kNone = 0,      ///< Not assigned today (or walked off; see ResidentRow docs).
  kPlowing,       ///< Horse work: needs an adult kolkhoz horse teamed in.
  kHarrowing,     ///< Horse work, like plowing.
  kSowing,        ///< By hand in Epoch I.
  kHarvest,       ///< By hand in Epoch I; the heaviest window of the year.
  kHerdCare,      ///< Feeding, milking, mucking at a unit-standing herd.
  kConstruction,  ///< Building, raising or taking down a unit (task A2).

  /// Carrying a load from where it lies to where it belongs (task A4;
  /// transport design §2 and §9). NOT a subsystem of its own — decision 155
  /// keeps transport inside the work orders, and this is what that means in
  /// the core: hauling is a work kind, a hauler is a resident with an
  /// assignment, and the load is state that was already there.
  ///
  /// Two carriers, and only two (boss, 2026-09-03). ON FOOT, at walking
  /// speed, carrying what a person carries — the age band, stamina and
  /// sportiness, cut by health, rest and hunger (transport §2). WITH A
  /// HORSE, at harness speed, carrying a cart's 750 kg: the cart comes with
  /// the draught animal and is not counted separately, because the canon of
  /// the start hands out horses and horse tackle to the same yards together.
  /// The hand cart of §9 is deliberately not modelled — it would need an
  /// inventory per household, which is the micromanagement the design avoids.
  kHauling,

  /// Felling what the chairman marked on a timber stand (timber design §8a,
  /// 2026-09-13): any adult, and only as many at once as there are tools in
  /// the stores (`timber_tools_per_feller` each, not spent — a STUB of wear).
  /// All the year round, no window. Target: WorkAssignment::stand.
  kFelling,

  /// Work at a PRODUCING UNIT, turning its raw material into its item (unit
  /// rules §4; timber design §8b, 2026-09-13): the sawmill's sawing today,
  /// the bakery's baking tomorrow. One kind for every producing unit, because
  /// what differs between them — the item, the rate — is the unit's table
  /// data, and a work kind per unit type would be a class per unit type
  /// (core rule §11). Done by the holders of the post the parent unit carries
  /// for it (the farm craftsman of the utility yard), no more at once than
  /// the unit's places. Target: WorkAssignment::unit, the producing unit.
  kUnitWork,

  /// Digging what the chairman marked on an extraction site — clay, stone or
  /// sand (construction design §3; boss, parcel 270): any adult, no more at
  /// once than there are tools in the stores (`extraction_tools_per_worker`
  /// each, not spent — a STUB of wear, as for felling). All the year round,
  /// no window. Target: WorkAssignment::extraction_site. Seam key
  /// `extraction`.
  kExtraction,

  /// Planting a forest zone (timber design §2; boss, boss-core-epoch1-3 seq
  /// 15): windowless, drains the planting stand's work_days_remaining.
  /// Target: WorkAssignment::stand. Walks: saplings are no cartload. Seam
  /// key `planting`.
  kPlanting,

  /// NOT A VALUE: the number of them, for a consumer's mirror. Values are
  /// appended BEFORE it.
  kWorkKindCount,
};

/// @brief The number of work kinds, as a plain integer for array sizes.
///
/// DERIVED, not written out. Until 2026-09-04 this line said `= 8` — a
/// length kept by hand beside the enum it counts, which is the one thing
/// the terminator convention exists to abolish. Six places size arrays and
/// range-check bytes on it, and every one of them would have kept working,
/// quietly measuring the wrong enum, the day a ninth kind was appended.
/// Found while giving WorkKind the terminator it was missing.
inline constexpr std::uint32_t kWorkKindCount =
    static_cast<std::uint32_t>(WorkKind::kWorkKindCount);

/// @brief True for kinds that harness a horse: the crew is capped by adult
/// kolkhoz horses, and residents hosting a kolkhoz horse at their yard are
/// assignable ONLY to these kinds (start canon, livestock design §5).
constexpr bool IsHorseWork(WorkKind kind) {
  return kind == WorkKind::kPlowing || kind == WorkKind::kHarrowing;
}

/// @brief True for kinds whose road to work is measured at HARNESS speed for
/// the whole brigade, whether or not a horse is free for each worker (time
/// design §7, "Какая работа едет, а какая идёт"). The horse works ride; so
/// does felling, which goes out on the carts that will cart the logs (timber
/// design §8a; boss, parcel 308). It takes no horse out of the day's pool —
/// "едет не значит лошадь каждому".
///
/// ONE ANSWER FOR THE REACH AND FOR THE WORKING DAY. The assignment decides
/// who may be sent by this road and the labour hour decides how much of the
/// day is left after it; asked two different ways, a feller would be sent
/// three kilometres out by the ride and then walk it, with no day left
/// (time design §7: "Порог и норма считают одно и то же плечо").
///
/// A KIND ANSWERS FOR A KIND. The meadow cut shares kHarvest with the hand
/// harvest of a strip and rides where that walks, so the question of an
/// assignment is WorkRidesOut (work_seam.h), which asks this and the field.
constexpr bool RidesOut(WorkKind kind) {
  return IsHorseWork(kind) || kind == WorkKind::kFelling;
}

/// @brief How many of the settlement's people can be put to work, and how
/// many of those are standing about — the two numbers behind the HUD's red
/// "idle" count (task A8, asked for by `ue` through boss).
///
/// WHY THE CORE COUNTS THIS AND NOT THE LAYER. The presentation can see a
/// birthday and reach "sixteen or over" on its own; what it cannot see is
/// the RULE. The working age is a cell in life.csv, not a constant — move
/// it and a layer that hard-coded sixteen goes on drawing the old number
/// with no sign that anything changed. The same goes for every other reason
/// a man is out of the pool: no household to start the day from, a post that
/// already answers for him, and whatever the next stage adds. **One home for
/// the rule, and it is the module that applies it every morning.**
struct WorkforceCount {
  /// People the accountant could place today: of working age, with a home
  /// to leave from. A post holder counts here — he is employed, not idle.
  std::uint32_t employable = 0;

  /// Of those, the ones with no work assigned right now. Read it after a
  /// day's first tick and it is the morning's leftovers; read it at night
  /// and the day has been cleared, so it is everybody. The number means
  /// "standing about AT THIS MOMENT", which is what a HUD shows.
  std::uint32_t idle = 0;
};

/// @brief The assignment block of one resident. Plain data.
/// Exactly one target id is valid, matching the kind: a field for the four
/// field kinds, a herd for kHerdCare, a unit for kConstruction, a stand for
/// kFelling, an extraction site for kExtraction, none for kNone. kHauling has
/// THREE possible targets — a field, a stand or an extraction site, whichever
/// the load lies on — and exactly one of them is valid. Travel time and
/// eligibility are NOT stored — they are pure functions of positions and state (state model law:
/// derived values are recomputed, never cached in state).
struct WorkAssignment {
  WorkKind kind = WorkKind::kNone;

  /// 1 when the day's placement put a horse under him (save format 88): a
  /// ploughman's or harrower's, always; a carter's, while one was left in the
  /// pool. A carter with none walks, and the road, the day and the pace are
  /// measured on foot (WorkRidesOut). STORED, not recomputed, and against the
  /// law above for a reason: which carter got the last horse is the
  /// placement's decision in its own order, and no reading of the state
  /// afterwards can tell it (boss, boss-core-topup-horses seq 2).
  std::uint8_t rides_horse = 0;

  FieldId field;  ///< Valid for the field kinds; invalid otherwise.

  HerdId herd;  ///< Valid for kHerdCare; invalid otherwise.

  UnitId unit;  ///< Valid for kConstruction: the site; invalid otherwise.

  /// Valid for kFelling, and for kHauling of logs lying on a stand; invalid
  /// otherwise (timber_state.h).
  TimberStandId stand;

  /// Valid for kExtraction, and for kHauling of a load lying on an extraction
  /// site; invalid otherwise (extraction_state.h).
  ExtractionSiteId extraction_site;

  /// Norm-days of output delivered since the day started, in game man-days
  /// of the assigned kind. Accumulated hourly while working; converted into
  /// trudodni on the family account at day close (rate x delivered), then
  /// reset. Survives a mid-day walk-off: partial output is still paid.
  float worked_norm_days_today = 0.0F;

  /// Game hours spent away from home today: the round trip is added at the
  /// first worked hour, the worked hours accumulate on top. The family's
  /// household_hours are 24 - sleep - the average of this over the members
  /// who went out (household design §1), so it must survive a walk-off,
  /// which clears `kind` and with it the target the road could be recomputed
  /// from. Reset at day close together with the assignment.
  float hours_away_today = 0.0F;

  /// Game hours of the road one way today, measured ONCE — at the first hour
  /// the labour hour looks at this assignment — by the way the work travels
  /// (road_route.h, work_seam.h WorkTravelMode; 0.36.2, save 93). Negative:
  /// not measured yet today. The labour hour asked it every hour until then,
  /// and a way by the network costs a query a straight line did not; the
  /// resident's activity reads the same number, so the two cannot differ.
  float travel_hours = -1.0F;
};

}  // namespace core

#endif  // CORE_COMMON_LABOR_STATE_H_
