/// @file
/// @brief SimEvent — the step's outbox: what happened this step, for the
/// presentation to show and the fast-forward to be interrupted by.
/// @threading PARALLEL_READONLY
/// The outbox lives in WorldState::step_events under the double-buffer
/// discipline, and every write is sequential: the step engine CLEARS it
/// right after the copy of `previous` into `current` (buffer-law rule 2,
/// core_sim/step.h), sequential sub-steps append as things happen in them,
/// and the events slot (phase 6) appends what the parallel phases produced,
/// folded in row order — a parallel phase itself never appends (buffer-law
/// rule 5: no cross-row accumulators, and the outbox is one). Between steps
/// the boundary reads the completed step's outbox and moves it into its own
/// log (core_boundary/session.h); nothing in the simulation reads it back.
///
/// NOT SAVED. This is the second block nothing in the simulation reads (the
/// ledger is the first, ledger_state.h), and unlike the ledger it is not
/// history: it holds one step's worth and is cleared the next. The save
/// codec neither writes nor reads it; a loaded world starts with an empty
/// outbox. What the presentation has not yet shown is the presentation's
/// concern, not the world's.
///
/// EVENTS ARE TRANSITIONS, ALARMS ARE CONDITIONS. "A resident died" is an
/// event: it happened at a tick and cannot be re-derived from the state
/// after. "The herd is underfed" is not an event — it is true for as long
/// as the state says so, and it is derived from the state on demand
/// (Alarm in core_common/alarm_state.h; office design §13). An emitter that
/// finds itself appending the same event every step is reporting a
/// condition and should stop.
///
/// SEVERITY IS PER INSTANCE. The office design (§14) sorts what happens
/// into what only the summary mentions, what earns one HUD line, and what
/// breaks a fast-forward (time design §1: fire, a die-off, an accident, a
/// grave illness, an inspector's arrival). The same kind lands in different
/// rows — a death of old age is routine, a death of hunger is not — so the
/// emitter states the severity on each event rather than the kind fixing
/// it. Kinds are appended, never renumbered: the presentation may keep an
/// event log of its own across builds.

#ifndef CORE_COMMON_EVENT_STATE_H_
#define CORE_COMMON_EVENT_STATE_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"

namespace core {

/// @brief How loudly the presentation should say it (office design §14).
enum class EventSeverity : std::uint8_t {
  kRoutine = 0,   ///< Summary material only; no notification.
  kNotable,       ///< One HUD notification; never breaks a fast-forward.
  kInterrupting,  ///< Breaks a fast-forward and notifies (time design §1).

  /// NOT A VALUE: the number of them, for a consumer's mirror. Values are
  /// appended BEFORE it.
  kEventSeverityCount,
};

/// @brief What happened. Grouped by the sub-step that emits it; the fields
/// each kind fills are named on the kind, the rest stay invalid / zero.
enum class EventKind : std::uint8_t {
  kNone = 0,

  // -- people: the demography sub-step of the decisions slot -------------
  kResidentBorn,     ///< resident, family.
  kResidentDied,     ///< resident, family; amount = cause code of the emitter.
  kResidentArrived,  ///< resident, family — a migrant who came to stay.
  kResidentLeft,     ///< resident, family — the outflow.
  kWedding,          ///< resident (the bride), family (the new household).

  // -- land: the production decisions sub-step ------------------------------
  kFieldPhaseChanged,  ///< field; amount = the new FieldPhase value.
  kFieldHarvested,     ///< field, resource, amount (grams the reaping laid into the heap).
  kFieldLost,          ///< field, resource, amount (grams the snow took standing; §6).

  // -- herds: the production decisions sub-step -----------------------------
  kHerdBorn,  ///< herd; amount = heads.
  kHerdDied,  ///< herd; amount = heads; severity says hunger from age.

  // -- labor: the assignments sub-step ---------------------------------------
  kWalkOff,  ///< resident — left work under the fatigue limit (unit rules §8).

  // -- the family exchange and the year ---------------------------------------
  kDistributionIssued,  ///< The monthly distribution went out (labor-payment §3).
  kRationIssued,        ///< family — the safety ration below the floor (§5).
  kYearClosed,          ///< The ledger rotated; amount = the year that closed.

  // -- the order book: the events slot sweep ---------------------------------
  //
  /// @no_emit the order is still the consumer's own: the sweep announces
  /// only terminal states (world.cpp, "accepted or active: still the
  /// consumer's"). A caller that issued an order knows it issued it, and
  /// hears back when it ENDS — telling it that its own order was accepted
  /// is an echo, not news. The two kinds stay in the enum because the state
  /// machine has those states and a numbering with holes in it is worse
  /// than a numbering with silences.
  kOrderAccepted,  ///< order.

  /// @no_emit the same reason as kOrderAccepted above: a state the issuer
  /// already knows it asked for.
  kOrderStarted,    ///< order.
  kOrderDone,       ///< order.
  kOrderRefused,    ///< order; amount = the OrderRefusal value; resource = the
                    ///< order's resource, for kMaterialsShort the first short line.
  kOrderCancelled,  ///< order.

  // -- units -------------------------------------------------------------------
  kUnitPaused,      ///< unit.
  kUnitResumed,     ///< unit.
  kUnitBuilt,       ///< unit (task A2).
  kUnitDemolished,  ///< unit (task A2); the id is dead after this step.

  // -- wear (task A5) ---------------------------------------------------------
  kUnitRepaired,  ///< unit — a repair finished; wear is back at 0.

  /// unit, family — one of the start's old houses fell at 100 wear (start
  /// design §4): the id is dead after this step, and its household stands
  /// without a house until the demography sub-step rehouses it the next day.
  /// family = the household that lived in it, invalid for an empty house;
  /// amount = the residents of that household at the fall, 0 for an empty
  /// one (2026-09-15, boss parcel 342). CARRIED ON THE EVENT because the unit
  /// row is gone by the time anyone reads it, and nothing else in the world
  /// says which family's house it was.
  kUnitCollapsed,

  // -- posts (task A7) --------------------------------------------------------
  kAppointed,  ///< resident, unit; amount = the ProfessionId value. Applied at the day's close.
  kDismissed,  ///< resident, unit; amount = the ProfessionId value he held.

  /// The post is empty and NOBODY ordered it: its holder died or left
  /// (resident, unit; amount = the ProfessionId value he held). Boss's
  /// decision of 2026-09-03, and its reasoning is worth keeping: an empty
  /// post is not a standing trouble the player must clear — the place may
  /// simply not be needed any more — so it is an EVENT and not an alarm.
  /// The trouble, when there is one, arrives with its own alarm: a yard
  /// whose groom is gone stops tending the herd and raises kHerdStarving.
  /// Seventy posts each raising an alarm would be noise; each announcing
  /// itself once, on the day it happens, is news.
  ///
  /// A DISMISSAL does not raise this — it already announces itself with
  /// kDismissed, and the same fact twice is the noise the rule above exists
  /// to avoid. This kind means "it emptied by itself".
  kPostVacated,

  /// unit — the kolkhoz yard: the groom is in place and the horses came
  /// off the private yards, all at once (livestock design §5); amount =
  /// heads. Once per campaign, and kNotable: the day a third of the village
  /// is free to work again.
  kHorsesStabled,

  /// The team went to night pasture for the first time (livestock design,
  /// «Ночное»); amount = heads out. Once per campaign and kNotable: it is the
  /// evening the summer nights stop costing hay, and it is one of the few
  /// scenes worth staying up for.
  ///
  /// THE FIRST NIGHT ONLY. The order is standing and the team goes out every
  /// night the three conditions hold; saying so nightly would be a journal
  /// nobody reads. Seam key `night_pasture_began`.
  kNightPastureBegan,

  // -- the district's plan (2026-09-12) --------------------------------------

  /// The economic year closed and the district was satisfied; amount = how
  /// many met years stand in a row. Raised at the year's turn, after the
  /// delivery it judges.
  kPlanMet,

  /// The economic year closed short on at least one position (epochs design
  /// §8, "сорванный план"); amount = how many failed years stand in a row.
  kPlanFailed,

  /// The failed years have reached the district's limit — the CONDITION of
  /// "Под суд" (epochs design §8), raised once, on the day the count
  /// reaches it. amount = that count.
  ///
  /// THE CORE STOPS HERE ON PURPOSE. The signal, the commission, the case
  /// and the courtroom are the presentation's (boss, 2026-09-12: "подача
  /// концов не твоя; твоё — условие и событие"), and a core that removed
  /// the chairman itself would be deciding how the game ends.
  kPlanTrialDue,

  // -- the fields the chairman takes off the map (2026-09-14) ----------------

  /// field — a kRemoveField was carried out and the field row is GONE; the
  /// id is dead after this step, as with kUnitDemolished. amount = 1 when the
  /// field was the start's reserve (FieldRow::start_reserve), 0 otherwise.
  ///
  /// THE AMOUNT IS THE QUEST'S DOOR. The first sub-item of the start quest
  /// closes on "the abandoned field let go" (fact start_reserve_field_removed;
  /// host/manual/95, №6), and after this step nothing in the world can say
  /// which field that was: the row that carried the mark is the row removed.
  /// So the event carries it, and the host reads the fact off the event
  /// rather than off a key it would have to remember from the start layout.
  kFieldRemoved,

  // -- the district's specialists of Epoch I (2026-09-14) --------------------

  /// resident, family, unit — a teacher or librarian the district sent has
  /// arrived, lives in his house as a household of one, and holds his post at
  /// `unit` from this step. amount = the ProfessionId value. kNotable. The
  /// host raises `first_teacher_arrived` off the first with a teacher's post.
  kSpecialistArrived,

  /// unit — on the first day of a month the district would have sent a
  /// specialist to `unit` and did not, because no free house stands for him
  /// (education design: "Нет жилья — не приезжает; месяц проходит. Подсказка —
  /// у старосты и в уведомлении, не молча"). amount = the ProfessionId value.
  /// kNotable; once a month per unit and post while the condition holds.
  kSpecialistNoHousing,

  // -- houses for families (2026-09-14; life-cycle §12, housing §20) ---------

  /// resident (the bride), family (her family) — a couple ready to marry found
  /// no free house and waits for one; amount = the groom's ResidentId value.
  /// Once per couple, on the day it starts waiting. kNotable. The host raises
  /// the start quest's `families_exceed_houses` off the first.
  kWeddingAwaitsHouse,

  /// family — its house is gone, no free house stands, the barrack rung is
  /// skipped (STUB), and in the warm season it pitches a tent on its old plot
  /// (housing design §20, the third rung). Once, when it moves into the tent.
  /// kNotable.
  kFamilyInTent,

  /// family — a family without a roof met the cold with nowhere to go and
  /// left the kolkhoz for good (§20, the fourth rung); amount = the members
  /// who left. Each of them is also a kResidentLeft. kInterrupting: the
  /// design calls the rung "heavy and irreversible".
  kFamilyLeftForNoHouse,

  // -- digging on the map (2026-09-14; construction design §3) --------------

  /// resource (clay, stone or sand) — an extraction site's stock has run out
  /// and it can never be marked again (boss, parcel 270: "восстановления
  /// нет"); amount = the ExtractionSiteId value. Once per site. kNotable.
  /// Seam key `extraction_site_exhausted`.
  kExtractionSiteExhausted,

  // -- the district comes (2026-09-15; characters design §2) ----------------

  /// A regular visit is announced `district_visit_notice_days` ahead ("известно
  /// заранее, за несколько дней"); amount = PackDistrictVisit of the face and
  /// the kind, found kNone. kNotable. Seam key `district_visit_announced`.
  /// An extraordinary visit is never announced.
  kDistrictVisitAnnounced,

  /// A face of the district has arrived, and the visit is computed: amount =
  /// PackDistrictVisit (district_visit_state.h) — face, kind, found, miss_by,
  /// gift, the seam's form of host 93 §4.1. Raised at the first tick of the
  /// arrival day. kInterrupting for an extraordinary visit ("утром у конторы
  /// стоит машина"), kNotable otherwise. Seam key `district_visit`.
  kDistrictVisit,

  // -- the organizations (2026-09-15; society design §3, §5) -----------------

  /// resident, family — the resident's SocialStatus moved: joined the
  /// pioneers, the komsomol or the party, or left one by age; amount = the new
  /// SocialStatus value (kNone when he left for nothing). Raised on a wave's
  /// day, on the campaign's first day, and when a specialist arrives a
  /// komsomol member. kRoutine. Seam key `social_status_changed`.
  kSocialStatusChanged,

  // -- the night trades (2026-09-15; crime design §7, §9) --------------------

  /// resident, family — he went out on his night trade in this hour; amount =
  /// the NightTrade value. Where he is and until when: the NightOutingRow of
  /// this night (night_trade_state.h). kRoutine: nobody in the village is
  /// told, and the chairman finds out by being there. Seam key
  /// `night_trade_outing`.
  kNightTradeOuting,

  // -- school (2026-09-15; education design §10) -----------------------------

  /// resident, unit — a child was enrolled in the school `unit` on the first
  /// day of the school year. kRoutine. Seam key `pupil_enrolled`.
  kPupilEnrolled,

  /// resident, unit — a pupil left the school `unit`; amount = the
  /// EducationStage he left with — kPrimary when the year was counted, kNone
  /// when he aged out without it or the school is gone. kRoutine. Seam key
  /// `pupil_left_school`.
  kPupilLeftSchool,

  // -- night posts (2026-09-15; crime design §11, time design) -----------------

  /// resident, unit — the holder of a night-shift post went on his shift at
  /// `unit` at sunset; amount = the ProfessionId value. Once a night per
  /// holder; night shifts only (an evening or a bath day would fill the
  /// journal every day). kRoutine. Seam key `post_shift_started`.
  kPostShiftStarted,

  // -- the day's heat (2026-09-15; camera design §4) ---------------------------

  /// The day's afternoon is at or above +25 (`hot_afternoon_c`), whatever the
  /// day is called; raised at the day's first tick; amount = the afternoon
  /// temperature in tenths of a degree. kRoutine. Seam key `hot_afternoon`.
  kHotAfternoon,

  // -- the raw-material leak (2026-09-15; crime design §7) -------------------

  /// The raw material carried off the kolkhoz stores this calendar month has
  /// reached `store_leak_complaint_kg` and there is no constable in the
  /// village: somebody comes to complain. amount = the month's grams. Once a
  /// campaign. kNotable. Seam key `store_leak_complaint`.
  kStoreLeakComplaint,

  // -- drinking (2026-09-15; crime design §6) ----------------------------------

  /// resident, family — his alcoholism crossed a band's edge (20, 40, 60) in
  /// either direction at the month's turn; amount = the lower edge of the band
  /// he is in now (0, 20, 40 or 60). kRoutine. Seam key
  /// `alcoholism_band_crossed`.
  kAlcoholismBandCrossed,

  // -- insulation (2026-09-15; unit rules §16) ---------------------------------

  /// unit — a kInsulateUnit site finished: the straw is spent and the unit
  /// is "warm" (UnitRow::insulated). kNotable. Seam key `unit_insulated`.
  kUnitInsulated,

  // -- the district MTS's column (2026-09-15; MTS design §1) ------------------

  /// unit = the field camp — the column reached the village and camps there
  /// (limit_state.h, MtsColumnState). kNotable. Seam key `mts_column_arrived`.
  kMtsColumnArrived,

  /// amount = whole hectares the column worked this season — it left: its
  /// limit is worked out or the field-work window closed. kNotable. Seam key
  /// `mts_column_left`.
  kMtsColumnLeft,

  /// No field camp stood by the end of the window: the column never came and
  /// the points are not returned. kNotable. Seam key `mts_column_not_arrived`.
  kMtsColumnNotArrived,

  /// herd; amount = head handed back to the district for limit points
  /// (kHandStock). Notable: the farm got smaller and the year's points got
  /// bigger, and both are things the chairman decided rather than suffered.
  /// Seam key `stock_handed_over`.
  ///
  /// AT THE END, AND THAT IS NOT TIDINESS. This kind was first written in
  /// beside the night pasture, where it belongs by subject — and every kind
  /// after it moved up one. The save's recorded section table caught it in
  /// the same minute: an event stored under the old numbering decodes as its
  /// neighbour, which is a silent wrong answer and not a refusal. Grouping by
  /// subject is what the blank lines and headings above are for; the ORDER is
  /// the wire format, and it only ever grows at this end.
  kStockHandedOver,

  /// ЭЛЕКТРИФИКАЦИЯ (era event 01, electricity design §3): the district is
  /// putting up the line and the substation, and the village may build its
  /// own network. Once a campaign, on the three blockers — accumulated limit
  /// points, a year lived, an office standing. Carries no subject: it is
  /// about the whole settlement. Interrupting, because the chairman's next
  /// season is a different one. Seam key `electrification_unlocked`.
  kElectrificationUnlocked,

  /// unit = the building that caught fire (fire design; quest_e1_13 «Огонь и
  /// вода», which opens on the first one). Interrupting: a fire is the sort
  /// of thing a fast-forward must not run past.
  ///
  /// IT IS NOT "A BUILDING WAS LOST". In Epoch I the fire is always put out
  /// — a stub over the design's promise that people usually get there in
  /// time — so what this announces is a scar and a repair to order, never a
  /// ruin. The day the extinguishing model arrives, this event keeps its
  /// meaning and a second one joins it for the building that went.
  /// Seam key `fire_broke`.
  ///
  /// APPENDED AT THE VERY END, after the electrification that came before it.
  /// It was first written in beside the hand-over, where it belongs by
  /// subject, and that would have moved every kind after it up one — the
  /// mistake this enum already made once today and the save's section table
  /// caught within the minute. Subject grouping is what the headings are
  /// for; the ORDER is the wire format.
  ///
  /// AND THE COMMENT HAS TO MOVE WITH THE ENUMERATOR, which is the second
  /// half of that lesson and cost its own finding. The kind was moved down
  /// here and this block was left where it stood, so it documented
  /// kElectrificationUnlocked instead — telling the reader that an event
  /// about the whole settlement carried a burnt building and a seam key
  /// `fire_broke`, while kFireBroke had no contract at all. Worse, the
  /// paragraph above it — «append at the very end» — then stood over the
  /// WRONG enumerator, so anybody obeying it where it stood would have
  /// appended between the two and re-made the very mis-decode it warns of.
  kFireBroke,

  /// resident, family: his cleanliness crossed `hygiene_disease_threshold`
  /// downwards, so he has lice or scabies (health design §3; quest_e1_14
  /// «Своя баня» opens on the first of them). Notable, not interrupting: it
  /// is unpleasant and catching, and it kills nobody.
  ///
  /// THE EVENT IS THE CAUSE BITING, NOT A DISEASE. The core does not model
  /// the illness and holds no row of `diseases.csv` — that table is declared
  /// as one the core has no business with, and this event is how the core
  /// keeps its own half of the bargain: it says the filth has reached the
  /// point where it costs something, and the illness stays off-screen, which
  /// is what the quest's brief asks for. Seam key `hygiene_disease`.
  ///
  /// A CROSSING AND NOT A CONDITION, so it cannot repeat every morning for
  /// the same man. «Первая» is not this core's word either: the host takes
  /// the first of these for `first_hygiene_disease`, as it takes
  /// `first_store_issue` from `distribution_issued`.
  kHygieneDisease,

  // -- samogon (2026-09-18; crime design §6-§7, registers 205-207) -----------

  /// The settlement's alcoholism — the mean of its men of 16 and over, taken
  /// at the month's turn — crossed 20 or 40; amount = the line, SIGNED: +20
  /// rose above it, −20 fell below. kNotable. Seam key
  /// `settlement_alcoholism_crossed`.
  kSettlementAlcoholismCrossed,

  /// resident — a supplied distiller hands over at his gate, in a random
  /// hour from sunset to lights-out, on an evening of a month in which he
  /// has raw material (register 206: «Продажа — вечером»). A scene's cue,
  /// not an account: what is paid moves at the month's turn (the purchase).
  /// kRoutine. Seam key `samogon_sale`.
  kSamogonSale,

  /// A whole month passed with the village's leak closed — every store of
  /// grain or potato under a sober watch on every day — and no distiller
  /// supplied in it (register 206). The fact `store_leak_closed_dry_month`
  /// that closes `quest_e1_22` is raised by the core through this; amount =
  /// the month's supply tag. kNotable. Seam key `store_leak_closed_dry_month`.
  kStoreLeakClosedDryMonth,

  /// A district cart has come to the gate and some of its goods fit in no
  /// store — no store of the village takes that resource, or those that do
  /// are full (boss seq 156, host's milk pass: a lot whose store was pulled
  /// down while it travelled). Said ONCE, on the cart's arrival day, one
  /// event per resource left on it; the cart waits and is offered again at
  /// every day's end, as before. resource = what waits; amount = GRAMS left.
  /// kNotable. Seam key proposed: `limit_goods_at_the_gate`.
  kLimitGoodsAtTheGate,

  /// The district's cart took the plan's debt off the fields' heaps on the
  /// day the snow settled, before the snow took them (register 242, boss
  /// seq 180): one event per resource; resource = what was taken; amount =
  /// GRAMS taken, all of them counted as delivered. kNotable. Seam key
  /// proposed: `district_took_from_field`.
  kDistrictTookFromField,

  /// family — a family with no roof, no free house, no barrack place, in the
  /// cold, comes to the chairman for the certificate to leave (housing §20
  /// step 4; «Без подписи председателя уехать нельзя»). Once per request;
  /// amount = the reason, 0 = no house. The answer is kAnswerLeaveRequest,
  /// and silence for `leave_request_answer_days` is a refusal. Interrupting:
  /// a family stands at the door. Seam key proposed: `leave_requested`.
  kLeaveRequested,

  /// family, unit = the house it is lodged in — refused its certificate, the
  /// family moves in with kin, or with the nearest neighbour (housing §20:
  /// «подселение»). Two families in one house is a live signal for the layer
  /// (washing, smoke, a crowd at the porch; look). kNotable. Seam key
  /// proposed: `family_lodged`.
  kFamilyLodged,

  // -- the trip to the district (district-trip.md; boss seq 206) ------------

  /// The chairman leaves for the district at 8:00, on his own trip or called
  /// «на ковёр». amount = the tick he is back. kInterrupting: the layer cuts
  /// to the road and rolls the day. Seam key proposed: `trip_departed`.
  kTripDeparted,

  /// He is back. kNotable. Seam key proposed: `trip_returned`.
  kTripReturned,

  /// His own trip booked for this morning did not go: a blizzard at 8:00.
  /// It does not count against the month. kNotable. Seam key proposed:
  /// `trip_cancelled`.
  kTripCancelled,

  /// The district's letter «на ковёр»: amount = the day he is called for,
  /// resource invalid; the cause is ChairmanState::summon_cause. kInterrupting
  /// — a day of the chairman's is taken, and he must see it coming. Seam key
  /// proposed: `summon_letter`.
  kSummonLetter,

  /// A blizzard on the summons' day: the summons moves to the next day.
  /// amount = the new day. kNotable. Seam key proposed: `summon_postponed`.
  kSummonPostponed,

  /// The plan bargained: resource = the position moved or dropped; amount =
  /// its new grams (0 for a replaced one — the crop that replaced it is its
  /// own position then). kNotable. Seam key proposed: `plan_traded`.
  kPlanTraded,

  /// TWINS ARE BORN (life cycle §4; register 245): «событие колхоза, а не
  /// только семьи». resident = the first child, family; amount = the second
  /// child's id value; the identical mark is ResidentRow::identical_twin.
  /// Emitted beside the two kResidentBorn. kNotable. Seam key proposed:
  /// `twins_born`.
  kTwinsBorn,

  /// THE DISTRICT'S AMBULANCE IS SENT (health «Скорая помощь из района»;
  /// register 236; boss seq 210): a resident's health fell below the line
  /// and the district learnt of it by itself — no call (district_car_state.h).
  /// resident, family; amount = the tick the car will stand at the house, 0
  /// while a blizzard holds it in the district. kNotable. Seam key proposed:
  /// `ambulance_sent`.
  kAmbulanceSent,

  /// The ambulance stands at the house and the patient is carried out; he is
  /// away in the district's hospital from this tick (ResidentRow::away_*).
  /// resident, family; amount = the day he is due back. kNotable. Seam key
  /// proposed: `ambulance_at_house`.
  kAmbulanceAtHouse,

  /// A resident is home from the district — with the milk cart in its season,
  /// else on foot from the border. resident, family; amount = the reason he
  /// was away (AwayReason). kNotable. Seam key proposed: `back_from_district`.
  kBackFromDistrict,

  /// A planting's crew finished: the zone is planted and grows from today
  /// (timber_planting.h). stand = the planting; amount = hectares × 100.
  /// kNotable. Seam key `forest_planted`.
  kForestPlanted,

  /// A planting grew to logs and may be felled like a grove. stand = the
  /// planting; amount = its standing m3, rounded. kNotable. Seam key
  /// `planting_matured`.
  kPlantingMatured,

  // -- the three ways a head is taken off, each said (boss, boss-core-epoch1-5
  //    seq 43 and 45; 0.35.3). They wrote one herd_culled and said nothing,
  //    and a probe could not tell a cull from a slaughter from a death.

  /// Young males over the herd's one sire went to meat as they matured
  /// (herd_life.cpp, RunMaturation). herd; amount = heads. kRoutine. Seam
  /// key `herd_males_culled`.
  kHerdMalesCulled,

  /// A yard held more of a group than its cap and no neighbour took the rest:
  /// it went to meat (herd_system.cpp). herd; amount = heads. kRoutine. Seam
  /// key `herd_surplus_slaughtered`.
  kHerdSurplusSlaughtered,

  /// The autumn pig slaughter took all but the sows and the sire
  /// (herd_life.cpp, RunAutumnSlaughter). herd; amount = heads. kNotable.
  /// Seam key `herd_autumn_slaughter`.
  kHerdAutumnSlaughter,

  /// field — the winter crop of the chain's slot for this year was not sown
  /// in its autumn window, so the slot lies fallow this year (fields design
  /// §7, «Озимая, не посеянная в своё окно, пропадает»; question 278,
  /// 0.36.13). amount = the crop's id (CropId value). Raised at the year's
  /// turn that brings the lost slot's year, once per field and year. kNotable.
  /// Seam key `winter_sowing_lost`. The data for the layout's mark "the winter
  /// crop was not sown — its window went"; showing it is the layer's.
  kWinterSowingLost,

  // -- the player's roads (delivery 7; road_draft.h). EACH COMES AFTER ITS
  // WRITE, in the same tick (ue's condition): Roads() already answers it.

  /// road — a road or path was laid (kLayRoad): a path or a dirt road at
  /// once, a gravel or asphalt one when its work is done (then this follows
  /// kRoadWorkFinished). kNotable. Seam key `road_laid`.
  /// @no_emit the laying comes with delivery 7c; until then kLayRoad is refused
  kRoadLaid,

  /// road — road work began on a piece (an upgrade, a gravel or asphalt
  /// laying, the demolition of a paved piece). amount = the target
  /// RoadSurface value (kNone for a demolition). kNotable. Seam key
  /// `road_work_started`.
  /// @no_emit road work comes with delivery 7e; until then no road work stands
  kRoadWorkStarted,

  /// road — the road work on a piece is done; the piece is of its new
  /// surface (or gone). amount = the RoadSurface value it has now.
  /// kNotable. Seam key `road_work_finished`.
  /// @no_emit road work comes with delivery 7e; until then no road work stands
  kRoadWorkFinished,

  /// road — a piece was demolished (kDemolishRoad): at once for a path and a
  /// dirt road, after its work for a paved one. The road id may name a road
  /// that no longer exists when the whole of it went. kNotable. Seam key
  /// `road_demolished`.
  /// @no_emit the demolition comes with delivery 7d; until then kDemolishRoad is refused
  kRoadDemolished,

  // Reserved for project phase 3 and appended by it: fire, epoch change,
  // the decision card (an inspector's arrival came as kDistrictVisit on
  // 2026-09-15). Named so the numbering is planned, not discovered.

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
  kEventKindCount,
};

/// @brief One thing that happened. Plain data, no text: the presentation
/// composes the sentence from the ids and the string tables. Every id that
/// a kind does not name stays invalid; `amount` means what the kind says.
struct SimEvent {
  /// The tick of the step that emitted it — the calendar tick of `current`
  /// after phase 1, which is the tick the completed state will carry.
  Tick tick = 0;

  EventKind kind = EventKind::kNone;

  EventSeverity severity = EventSeverity::kRoutine;

  ResidentId resident;

  FamilyId family;

  UnitId unit;

  FieldId field;

  HerdId herd;

  OrderId order;

  /// For kinds that move a resource: which one. Invalid otherwise.
  ResourceId resource;

  /// Grams, heads, a phase value, a refusal code — the kind says which.
  std::int64_t amount = 0;

  /// The goods lot of the district's limit an order named (OrderRow::lot):
  /// set on kOrderDone, kOrderRefused and kOrderCancelled of every order,
  /// invalid for an order that names none (host door request no. 4). The
  /// order's row is swept the same step its event is emitted, so the event is
  /// the only place a reader can still see which lot was ordered.
  LimitLotId lot;

  /// The timber stand an event is about: kForestPlanted, kPlantingMatured
  /// (0.34.35). Invalid on every other kind.
  TimberStandId stand;

  /// The road an event is about: kRoadLaid, kRoadWorkStarted,
  /// kRoadWorkFinished, kRoadDemolished (delivery 7a); and, on the three order
  /// events, the road the order named (OrderRow::road). Invalid otherwise.
  RoadId road;
};

/// @brief The outbox type used by WorldState: one step's events, in the
/// order they were appended, which is deterministic because every appender
/// is sequential.
using StepEventLog = std::vector<SimEvent>;

}  // namespace core

#endif  // CORE_COMMON_EVENT_STATE_H_
