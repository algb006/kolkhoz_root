/// @file
/// @brief Alarm — a condition standing in the completed state that the
/// chairman must see until it passes. The roster of kinds, and the one
/// struct every kind fills.
/// @threading PARALLEL_READONLY
/// Plain data, no mutable state of its own. Alarms are NOT part of
/// WorldState: they are derived from a completed state between steps, by
/// the subsystems whose rules they are (ISimulation::CollectAlarms,
/// core_sim/step.h), and handed to the presentation by the session
/// (core_boundary/session.h, ActiveAlarms). Nothing in the simulation
/// reads them; nothing stores them; a save carries none, and a loaded
/// world stands its alarms again the moment the session asks.
///
/// EVENTS ARE TRANSITIONS, ALARMS ARE CONDITIONS (event_state.h). "The
/// store is full" is true for as long as the state says so, and the player
/// wants it in front of them for exactly that long — an alarm hangs in its
/// group of the office and clears itself when the trouble passes (office
/// design §13, §14). An emitter that appends the same event every step is
/// reporting a condition and belongs here instead.
///
/// WHO COMPUTES. A predicate is a rule of some subsystem — what a store's
/// capacity is, how much seed a rotation needs — and rules live with their
/// configuration (subsystem law, manual/52-state-model.md §1). So every
/// subsystem that owns alarms answers CollectAlarms over the completed
/// state with its own config, the assembled simulation fans out to them in
/// the fixed order of the decisions slot, and the session sorts the union
/// by kind and then by subject id (manual/72-storage-and-alarms.md §3).
/// The boundary itself computes none: it knows no game rule, and copying
/// capacities and norms into it would give every rule a second home.
///
/// THE SANCTIONED WAY OUT OF A PHASE. Phase code does not log (core_log
/// contract; DEADLOCK-001 in claude/analysis/OPEN_ITEMS.md): a warning
/// written to a file from inside a step is I/O on the hot path, and the
/// player never sees it. A condition worth a warning is worth an alarm,
/// which the player does see — so a LogWarning inside a phase is, from
/// this file on, a predicate that has not been written yet.
///
/// NAMES ARE WHAT THE PLAYER READS. Each kind becomes a string of
/// db/strings.db (`alarm.<snake_case of the kind>.title`, boss's to
/// create), and a name must say what the trouble is so that a translation
/// can too: kStoreFull, not kWarn17. Kinds are appended, never renumbered
/// — the presentation may keep its own map across builds.

#ifndef CORE_COMMON_ALARM_STATE_H_
#define CORE_COMMON_ALARM_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/quantities.h"

namespace core {

/// @brief What the trouble is. Grouped by the subsystem whose predicate
/// answers it; the SUBJECT of each kind — the one id that names what it
/// is about and that orders alarms of the kind — is named on the kind,
/// the other ids stay invalid. `resource` and `amount` say "of what" and
/// "how much" where the kind has an answer.
enum class AlarmKind : std::uint8_t {
  kNone = 0,

  // -- stores and the land: core_production ------------------------------------

  /// A numbered store is at its capacity (the level's tonnage, unit rules
  /// §11; manual/72-storage-and-alarms.md §2). Nothing more can be put in
  /// until something is taken out or the store grows. Subject: `unit`;
  /// `amount` = the capacity in grams. Outline-bounded stores (a heap, a
  /// stack) never raise it: they have no number to be full against.
  kStoreFull,

  /// A field is GROWING a crop the stores will not hold: the expected yield
  /// is larger than all the free room together. The warning before the
  /// loss — there is still a season in which to free a store or raise one,
  /// and that is exactly what separates this kind from the next one (boss,
  /// 2026-09-03: the player must tell "build now" from "cart it away" at a
  /// glance). Subject: `field`; `resource` = the crop's produce;
  /// `amount` = the grams that would not fit.
  kHarvestWillNotFit,

  /// A field is holding produce already reaped, because the stores had no
  /// room for it at payout (FieldRow::reaped_grams > 0). The trouble has
  /// happened: the load is off the crop and in nobody's store, and the
  /// field retries the delivery every day until something is taken out.
  /// Subject: `field`; `resource` = the produce; `amount` = the grams
  /// waiting.
  kHarvestWaitingOnField,

  /// The rotation assigned to a field asks for more seed than the stores
  /// hold — grain or potato of the crop it sows next (farming design §7:
  /// "an alarm at assignment, not in spring"). Stands from the day the
  /// rotation is set until the stores cover the norm, and again after a
  /// sowing that went short. Subject: `field`; `resource` = the seed;
  /// `amount` = the shortfall in grams.
  kSeedShort,

  /// A kolkhoz herd went underfed today and is still underfed
  /// (HerdRow::unfed_days > 0): produce is already down, deaths begin past
  /// the config threshold (manual/66-food-model.md). The condition that
  /// starved sixteen horses beside two thousand tonnes of grain without a
  /// word (balance/69-reconciliation.md §3 D1). Subject: `herd`;
  /// `amount` = the head count.
  kHerdStarving,

  // -- people: core_residents ----------------------------------------------------

  /// A family's satiety has fallen to the floor at which the kolkhoz owes
  /// the safety ration (manual/66-food-model.md §5; labour-payment design
  /// §5). The THRESHOLD is the ration's, but the alarm is not a report that
  /// the ration is running: it stands whether or not the chairman switched
  /// the ration on, because it speaks of the trouble and not of the
  /// treatment (boss, 2026-09-03). A month on the ration is itself the
  /// open statement that the farm is not feeding its people.
  /// Subject: `family`.
  kFamilyGoingHungry,

  // -- sites: core_construction ------------------------------------------------

  /// A site is delivering (ConstructionState kDelivering) and the stores
  /// cannot supply what its recipe still lacks — the instant-delivery stub
  /// brings what there is, and the site would otherwise wait for ever in
  /// silence. Subject: `unit`; `resource` = the first material short in
  /// recipe order; `amount` = the grams short of it.
  kSiteWithoutMaterials,

  /// The site is not reachable by road (unit rules §12). STUB: the core
  /// has no roads, so no site is ever unreachable and the predicate yields
  /// nothing; the kind is in the roster so that the presentation's map has
  /// its key before the roads arrive. Subject: `unit`.
  kNoRoad,

  // -- posts: core_labor (task A7) -----------------------------------------------

  /// The kolkhoz yard stands built and has no groom, while the kolkhoz
  /// horses still stand at private yards and a third of the village is
  /// tied to them (livestock design §5: "the groom was not appointed —
  /// nothing happened", and the alarm is named there). Subject: `unit`
  /// (the yard); `amount` = the horses waiting. Clears the moment the
  /// appointment is applied — BEFORE the horses move, so the groom's one
  /// idle morning makes no sound (boss's condition, 2026-09-03); never
  /// returns after the horses are stabled.
  kYardWithoutGroom,

  // Appended by later tasks and phases: children out of school, sewage,
  // logistics falling behind. Named so the numbering is planned, not
  // discovered.

  /// NOT A KIND, and never a value anybody stores or sends: the count, so a
  /// CONSUMER can static_assert the length of its own mirror.
  ///
  /// That is the whole reason, and it is why this one was missing. The
  /// counts were first asked of the SERIALIZABLE enums — the ones whose
  /// codecs range-check — but the reason was mirrors, and the two sets are
  /// not the same set. These are exactly the tables that have already
  /// drifted silently once, and they were the ones left without a guard
  /// (boss, 2026-09-04). Narrowing a rule by a property that was not its
  /// reason looks like tidiness and works like a hole.
  kAlarmKindCount,
};

/// @brief One standing condition. Which fields are meaningful is fixed by
/// the kind (see AlarmKind); the rest are invalid / zero. Two alarms with
/// the same kind and subject are one alarm — a predicate yields each
/// subject at most once.
struct Alarm {
  AlarmKind kind = AlarmKind::kNone;

  ResidentId resident;

  FamilyId family;

  UnitId unit;

  FieldId field;

  HerdId herd;

  /// For kinds that are about a resource: which one. Invalid otherwise.
  ResourceId resource;

  /// Grams, heads — the kind says which. 0 when the kind has no number.
  std::int64_t amount = 0;
};

/// @brief The subject id of an alarm as one number, for ordering: the id
/// its kind names (AlarmKind), read from whichever field that is. Two
/// alarms of one kind sort by this; kinds sort by their value. 0 for
/// kNone. Implemented in core_common (alarm_state.cpp).
std::uint32_t AlarmSubjectValue(const Alarm& alarm);

}  // namespace core

#endif  // CORE_COMMON_ALARM_STATE_H_
