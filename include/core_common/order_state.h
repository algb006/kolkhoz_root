/// @file
/// @brief OrderRow — the chairman's order book: what came across the
/// boundary as a command, and how far the simulation has taken it.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::orders under the double-buffer discipline, and
/// every write is sequential.
///
/// WHO WRITES, and what is wired TODAY. The step engine APPENDS issued rows
/// and MARKS cancellations before phase 1, in arrival order (buffer-law
/// rule 2, core_sim/step.h). The consuming half is wired for EVERY kind
/// since 2026-09-12:
/// THERE ARE FIVE CONSUMERS NOW, and each settles its own kinds to kDone or
/// kRefused IN THE STEP THE ROW IS READ — never to kAccepted or kActive,
/// except where a kind says otherwise:
///   * core_construction — kBuildUnit, kStartBuild, kUpgradeUnit,
///     kDemolishUnit, kRepairUnit, kInsulateUnit;
///   * core_production — kPauseUnit, kResumeUnit, kUnsealFund, kSetRotation,
///     kMarkFelling, kMarkExtraction, kOrderLimitLot, kRemoveField,
///     kGrazeAtNight, kHandStock and kDeliverPlan;
///   * core_labor — kAssignWork, kReleaseWork, kAppoint and kDismiss, the
///     last two applied at the day's close rather than at once;
///   * core_residents — kTakeNightTrader, kSetRation and kSetIssueNorm (since
///     2026-09-18);
///   * core_world — kAdvanceEra (since 2026-09-18), in the events slot, where
///     the readiness is scored.
/// This list was three consumers and short by seven kinds on 2026-09-18,
/// when the fourth consumer was added and the list counted rather than
/// appended to: every kind below names its consumer, and that is the
/// contract; this list is its index.
/// The events slot (phase 6) then emits every terminal row's event and
/// REMOVES the row, so the book is empty again by the end of the step that
/// settled it.
///
/// NOTHING IS UNCONSUMED ANY MORE, and this paragraph named kSetRotation as
/// the last one until 2026-09-12. It got its consumer that evening — the
/// player's lever for telling a field what to grow, and the reason
/// ninety-three of the start's hundred and sixty-three hectares had lain
/// unworked through every thirty-year run the project had measured: work is
/// opened off the rotation, and nothing could give a field one after genesis.
///
/// The sweep's kNoConsumer is not dead for that. It is the guard for a kind
/// ADDED WITHOUT A CONSUMER, which is the mistake that otherwise leaves no
/// trace at all — an order accepted, answered by nobody, and quietly gone.
///
/// This paragraph said "wired for the construction kinds and only for those"
/// until 2026-09-12, listing the pause verbs among the unconsumed while the
/// kind entries below named core_production as their consumer — the file
/// disagreeing with itself, which is how a reader comes away certain of the
/// wrong half. A CONSUMER LIST IS A CONTRACT, and one short by two modules
/// is the shape in which a fourth consumer lands unchallenged. It was short
/// by two kinds that morning and by one that evening — the delivery that
/// gave kSetRotation a consumer announced the fact in the paragraph below
/// and left the list two lines above unchanged (analysis, 0.17.96). A list
/// repaired by hand stays short until the next reader counts it.
/// Parallel phases never touch the table — an order is a structural fact,
/// and structure changes only in sequential slots (buffer-law rule 6).
///
/// WHY AN ORDER IS STATE AND NOT A MESSAGE (project phase 2, task A1;
/// manual/70-boundary.md). The design defers almost everything the chairman
/// decides: a reassignment takes effect from the next working day, a unit
/// stops when its cycle ends, a construction site opens when the crew
/// arrives — and while an order waits it is visible ("status: pending",
/// time design §11) and may be cancelled without trace. A thing that waits,
/// is shown and can be taken back is state: it must survive a save, be
/// copied with the buffers and compared by the determinism check. So a
/// command from the presentation does not DO anything; it puts a row in
/// this book, and the subsystem whose rules apply reads the book when its
/// moment comes. The boundary itself knows two verbs — issue and cancel —
/// and no order semantics at all.
///
/// LIFECYCLE, and who moves a row (one consumer per kind, named on the kind):
///
///     issued ──► kPending ──► kAccepted ──► kActive ──► kDone
///                   │             │            │
///                   │             │            └──► kRefused (rule broke mid-way)
///                   │             └──► kRefused | kCancelled
///                   └──► kRefused | kCancelled
///
///   * kPending   — appended by the engine this step; nobody has read it.
///   * kAccepted  — the consumer validated it and waits for its moment
///                  (the next morning, the end of a cycle). Cancellable.
///   * kActive    — execution began; from here the work is finished, not
///                  taken back (time design §11: "работа началась — только
///                  довести до конца"). Not cancellable.
///   * kDone, kRefused, kCancelled — terminal. The events slot emits the
///                  matching event and removes the row in the same step.
///
/// A row still kPending when the events slot of the step it was applied in
/// runs has no consumer: the events slot refuses it with
/// OrderRefusal::kNoConsumer (core_world/world.cpp, SweepOrderBook). That is
/// how an order kind whose mechanic has not arrived yet answers — with a
/// refusal the presentation can show, never with silence. It is live, and
/// the work kinds meet it every time they are issued until task O3.
///
/// THE ONE APPENDER. Only the step engine appends to this table, and it
/// appends the staged rows in the order they were staged. Ids are therefore
/// predictable — next_id_value, then +1, +2 — and the boundary hands the
/// caller the id a row WILL carry at the moment of issue, before the step
/// that creates the row has run. A subsystem that wanted to raise an order
/// of its own (an Epoch-III specialist's proposal, delegation design §6)
/// would need a different table; this one is the chairman's.
///
/// SAVED. A campaign saved with an order waiting must resume with it
/// waiting: the row is a section of the save format, learned by the codec in
/// task O1 of project phase 2 (VERSION_SAVE 3, core_save/save_rows.cpp).

#ifndef CORE_COMMON_ORDER_STATE_H_
#define CORE_COMMON_ORDER_STATE_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/geometry.h"
#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

/// @brief What the chairman ordered. Each kind names the subsystem that
/// consumes it; a kind no wired subsystem consumes is refused by the events
/// slot (see @file). Values are stable: kinds are appended, never renumbered
/// or reused — a journal written by an older build must still read.
enum class OrderKind : std::uint8_t {
  kNone = 0,

  /// Put `resident` on `work` at `field` / `herd` from the next working day
  /// (time design §11: one job per day, a change takes effect after the
  /// current day). The chairman's standing order outranks the accountant's
  /// morning placement for that resident — a boss decision of 2026-08-31
  /// grounded in delegation design §7 (management by exception), with its
  /// bounds fixed there: the order holds until kDone or a kReleaseWork,
  /// not for one day; and it revokes nothing — one overridden placement
  /// leaves the accountant's delegation as it was. Consumer: core_labor.
  kAssignWork,

  /// Release `resident` from a standing kAssignWork order: back to the
  /// accountant's placement from the next working day. Consumer: core_labor.
  kReleaseWork,

  /// Stop production at `unit` — at once if no cycle is running, otherwise
  /// when the running cycle ends (unit rules §5). Consumer: core_production.
  ///
  /// AND A STARTED BUILDING OR A DEMOLITION (construction design §6; the
  /// human's word of 2026-09-14, "стройку и снос можно ставить на паузу"): a
  /// site of a new unit, an upgrade or a module in kDelivering or kBuilding,
  /// or a unit in kDemolishing. On pause the crew is released at the day's end,
  /// the phase and the share done stay, nothing is carried to the site or away
  /// from the demolition, and the materials already reserved stay the site's —
  /// no saw and no other building takes them. A marked plot is not work and is
  /// not paused (refused kRuleForbids).
  ///
  /// ONLY A UNIT THAT PRODUCES (since 2026-09-18; units rules §5, «Производственный
  /// юнит можно остановить»): a STANDING unit outside the production and
  /// livestock classes is refused kNotEligible — a school or a house has no
  /// production to stop, and a pause there only stopped its wear. Work at a
  /// site pauses whatever the class. kResumeUnit is never refused for it.
  kPauseUnit,

  /// Resume a paused `unit`; work restarts the next day. Consumer:
  /// core_production. A resumed building is NOT re-checked against its
  /// recipe (construction design §6): what it reserved is still its own.
  kResumeUnit,

  /// Set the three-year rotation of `field` to rotation_year0..2 (farming
  /// design §7). An empty slot is a FALLOW year and is a legal thing to
  /// order; ALL THREE EMPTY is the chairman taking his word back, and the
  /// field goes back to unassigned (land_state.h, rotation_assigned). The
  /// crop named FIRST is the one the next sowing puts in the ground, whatever
  /// month the order arrives — the chain is a cycle and the consumer sets its
  /// phase. Refused with kNoSuchSubject for a field that is not there,
  /// kNoSuchCrop for a slot naming a crop this build does not carry, and
  /// kWrongLand for a meadow, which is mown where it grew and is never sown
  /// at all. Consumer: core_production.
  kSetRotation,

  /// MARK a new unit of `unit_type` at `position`: pegs and string, the
  /// plot taken, nothing spent (construction design §6). The row appears at
  /// level 0 with ConstructionPhase::kMarked and waits for kStartBuild.
  /// Refused with kGateClosed when the type's gate is shut, kTooClose when
  /// another unit's plot overlaps (unit rules §9), kNoSuchSubject for an
  /// unknown type. Consumer: core_construction.
  kBuildUnit,

  /// Demolish `unit`. A marked site goes at once and for free; a built
  /// unit is emptied first (unit rules §14 — its stock goes to the stores
  /// by the instant-delivery stub, the rest is lost, as the design says)
  /// and then dismantled with labour (construction design §12). Refused
  /// with kNotEmpty while a household lives or a herd stands there.
  /// Consumer: core_construction.
  kDemolishUnit,

  /// START the works on a marked site (`unit`): construction design §6 —
  /// "the works begin only on the chairman's command", never by themselves
  /// when materials appear. Refused with kRuleForbids unless the unit is
  /// kMarked; kMaterialsShort when a line of the recipe is not in the village
  /// in full (construction design §6). Started, the recipe is carried onto the
  /// site the same tick and is the site's from then on. Consumer:
  /// core_construction.
  kStartBuild,

  /// Raise `unit` to its next level (unit rules §11). No marking phase — the
  /// plot is already there — so the site starts delivering at once. The
  /// unit keeps working at its current level meanwhile. Refused with
  /// kRuleForbids at the top of the ladder or while another site is in
  /// progress on it, kGateClosed when the next level's era has not come,
  /// kMaterialsShort when the next level's recipe is not in the village in
  /// full (kStartBuild's rule). Consumer: core_construction.
  kUpgradeUnit,

  /// Repair a standing unit (task A5; construction design §11, unit rules
  /// §15). Names `unit`. Opens a site on it — kDelivering for the spare
  /// parts, then kRepairing for the labour — and the unit works meanwhile.
  /// Cost scales with the wear at the moment of the order: a neglected
  /// repair is dearer, in parts and in man-days alike. Refused when the
  /// unit is not built, is a site already, has nothing to wear (has_wear
  /// = 0) or nothing worn (wear = 0), or is one of the start's old houses,
  /// which the canon says cannot be repaired, only replaced (housing design
  /// §10). Decided in the step it is read, like the other four.
  kRepairUnit,

  /// Appoint `resident` to the post `profession` at `unit` (task A7;
  /// manual/74-posts.md). NOT kAssignWork: that one puts a person on a
  /// day's work at a field or herd and outranks the accountant for as long
  /// as it stands; this one gives a person a PLACE — the groom of this
  /// yard, the storekeeper of this granary — that survives the day and the
  /// save and is his until a kDismiss. The two look alike and must not be
  /// merged: one names a job, the other a role. Validated in the step it
  /// is read and then kAccepted, cancellable, until the day's close, when
  /// the labor sub-step applies it — "a change of post only after the
  /// working day" (time design §11). A resident holding another post is
  /// MOVED by this order; a second kAppoint for a resident whose first is
  /// still kAccepted is kConflictsWithActive. Refused: no such resident
  /// (kNoSuchSubject); outside the post's age band, the wrong sex for it,
  /// or below its education threshold — all columns of professions.csv
  /// (kNotEligible); no such unit (kNoSuchSubject, as in construction — a
  /// named thing that is gone answers the same whichever book it lies in);
  /// a site, or a unit type and level that
  /// carry no such post per unit_staff.csv (kRuleForbids); every slot at
  /// the unit taken, or a `single_post` post already held anywhere in the
  /// village (kNoVacancy). Consumer: core_labor.
  kAppoint,

  /// Dismiss `resident` from the post he holds — the same kind of order as
  /// the appointment, at boss's insistence (2026-09-03): a post nobody can
  /// be removed from is a post for life, and the player notices on the
  /// first groom who takes to drink. Same timing as kAppoint: kAccepted,
  /// applied at the day's close. Refused with kRuleForbids when the
  /// resident holds no post. Consumer: core_labor.
  kDismiss,

  /// UNSEAL A FUND (resources design §6): take `amount` of `resource` out of
  /// the sealed `fund` and let the automation touch it. The design's own
  /// emergency door — "нечем кормить людей — можно взять из резерва плана и
  /// даже из семенного" — and it is a door the chairman opens by NAMING A
  /// FIGURE, because the same paragraph calls it "осознанный выбор, а не
  /// незаметная утечка".
  ///
  /// ONE VERB FOR BOTH FUNDS, not two: the design gives the same door to the
  /// plan reserve and to the seed fund, and a second verb for the second
  /// fund would be one decision with two homes (boss, 2026-09-12).
  ///
  /// THE CORE INVENTS NO CONSEQUENCE. It subtracts, and that is all: the
  /// grain becomes issuable, so come the delivery there is less in the store
  /// and the plan falls short, or come the sowing there is less seed. The
  /// design already calls those consequences natural and deferred, and
  /// anything added here would be a punishment nobody wrote down.
  ///
  /// Settled in the step it is read, like the pause verbs. Consumer:
  /// core_production.
  kUnsealFund,

  /// MARK `volume_m3` of `stand` for felling (timber design §8a; terrain
  /// design §7, "разметка, порубка, вывоз"). The crew fells it, the logs lie
  /// on the stand as a load, and the ordinary carting brings them in. The
  /// chairman names A VOLUME, because the core keeps a stand's stock and not
  /// its trees: "these trees" on the layer's map is "this many cubic metres"
  /// here.
  ///
  /// AS MANY FELLINGS AT ONCE AS THE CHAIRMAN MARKS (the human's word of
  /// 2026-09-14; time design §11): refused with kConflictsWithActive only
  /// while THIS stand still has timber marked. Refused
  /// with kNoSuchSubject for a stand that is not there, and kRuleForbids for a
  /// volume that exceeds the stand's unmarked stock. A volume that is not
  /// positive NEVER REACHES THE CONSUMER through the session: the boundary's
  /// shape check refuses it at issue (core_boundary/session.cpp, ShapeIsValid)
  /// and the caller gets no order at all. The consumer refuses it too, with
  /// kRuleForbids, for an order staged past the boundary (host found the
  /// header saying only the second, 2026-09-13).
  ///
  /// A STAND WITH NO STOCK IS NORMAL at the start: every old-forest square
  /// begins empty and gains only the trunks that fall each year (timber
  /// design §8a); marking one refuses with kRuleForbids until they have.
  ///
  /// Settled in the step it is read, so the order itself is past cancelling
  /// at once. TAKING THE MARK OFF BEFORE THE CREW STARTS — terrain design §7,
  /// "отменил — значит передумал целиком" — has no verb yet (STUB): it wants
  /// its own order, the way kReleaseWork answers kAssignWork, and none was
  /// ordered. Consumer: core_production.
  kMarkFelling,

  /// BUY one `lot` of the district's limit catalogue (district design §1;
  /// boss, parcels 208 and 211). Its points leave LimitState::points at once,
  /// and the goods travel on the district's cart (§4): they reach the stores
  /// limit_delivery_days plus a seeded delay of 0..limit_delivery_delay_days_max
  /// later (limit_state.h). Refused with kNoSuchSubject for a lot the catalogue
  /// does not carry; kGateClosed for a lot of a later epoch; kRuleForbids for
  /// a lot that is not goods (machines, people and "choice" have their own
  /// windows — STUB), for a lot with no price, and for one none of
  /// whose resources has an amount yet; kLimitShort when the points left this
  /// year are fewer than the price. Settled in the step it is read.
  /// Consumer: core_production.
  ///
  /// STOCK IS BOUGHT HERE TOO, and by this same kind rather than one of its
  /// own — boss's word of 2026-09-16: «Пол — поле существующего приказа, а
  /// не новый род приказа». A livestock lot spends its points the same way
  /// and then travels differently: no cart, no unloading, the head simply
  /// stands under a roof on its day (limit_state.h, LivestockArrivalRow).
  /// It refuses with kRuleForbids while the tables carry no head count for
  /// it — the piglet and chick batches, whose size nobody has written.
  ///
  /// THE SEX IS THE CHAIRMAN'S AND TRAVELS ON THE ROW, in a field this
  /// contract names and the implementation adds: `male`, 0/1, read only when
  /// the lot's `sex_choice` says the order names it, and ignored to 0
  /// otherwise. It is not added in this contract because OrderRow is a WIRE
  /// SHAPE — the save codec asserts both its size and its arity — so the
  /// field, its codec and VERSION_SAVE move together or the tripwires fire,
  /// which is exactly what they are for.
  kOrderLimitLot,

  /// TAKE `field` OFF THE MAP (construction design §12, "Поле, сад — ничем:
  /// мгновенно и бесплатно"; start canon §2, the reserve field). The row goes
  /// in the step the order is read, nothing is spent, and kFieldRemoved goes
  /// out with the start reserve's mark. The land under it is simply free: no
  /// rule in the core keeps a unit off a field, so nothing has to be released.
  ///
  /// REFUSED WHERE THE DESIGN SAYS BREAD STANDS, and only there — "игра
  /// просто не даст удалить поле, на котором стоит хлеб" (production units
  /// design §5): kNotEmpty while the field is in kHarvest (ripe and not yet
  /// reaped) or while reaped grain still lies on it waiting for the carts
  /// (reaped_grams > 0 — "убрано и вывезено" is the state that frees it).
  /// A field ploughed, sown or growing IS removed, and what was put into it
  /// is lost ("растёт, но урожая ещё нет — да, с потерей вложенного").
  ///
  /// The farming design's lock — "с первой вспашки и до конца круга контур
  /// заблокирован" (§5) — is read as a lock on REDRAWING a contour (redraw,
  /// split, merge), which is what that paragraph lists, and not on removal,
  /// which two other chapters allow with the loss named. This reading is the
  /// core's and was put to boss with the delivery (2026-09-14).
  ///
  /// Refused with kNoSuchSubject for a field that is not there, and kWrongLand
  /// for a meadow: the design removes "поле, сад", and a meadow is grass that
  /// grew there, not a contour anybody drew. Consumer: core_production.
  kRemoveField,

  /// MARK `amount` grams of `extraction_site` for digging (construction
  /// design §3; boss, parcel 270): free and at once, like marking a field.
  /// The crew digs it, the dug mass lies on the site as a load, and the
  /// ordinary carting brings it to its home (resource_stores.csv). The
  /// chairman names A MASS, for the reason a felling names a volume: the core
  /// keeps a site's stock, not its spadefuls.
  ///
  /// ONE MARK A SITE: refused with kConflictsWithActive while the site still
  /// has a mark the crew has not finished. Refused with kNoSuchSubject for a
  /// site that is not there; kRuleForbids for an amount that is not positive
  /// (the boundary's shape check refuses it at issue as well) or exceeds the
  /// site's unmarked stock — AN EXHAUSTED SITE IS NEVER MARKED AGAIN, nothing
  /// grows back. Settled in the step it is read; taking a mark off has no verb
  /// (STUB, as for felling). Seam key `mark_extraction`. Consumer:
  /// core_production.
  kMarkExtraction,

  /// Insulate a standing unit with straw (unit rules §16, "Эпоха I числами";
  /// boss, parcel 364). Names `unit`. Who may be insulated is DERIVED, there
  /// is no column: a heated type (unit_types.csv has_heating = 1) or a level
  /// with room for animals (unit_levels.csv livestock_capacity_head > 0).
  /// The straw and the man-days by kind: housing 2 t / 2, a livestock unit
  /// 6 t / 5, any other heated unit 3 t / 3 (construction.csv). Opens a site
  /// on the unit like a repair — kInsulating, the straw brought and then the
  /// labour — and the unit works meanwhile; at the end the straw is spent,
  /// UnitRow::insulated is set and kUnitInsulated said. Refused with
  /// kNoSuchSubject for a unit that is not there; kRuleForbids when it is not
  /// built, is a site already, may not be insulated, is insulated already or
  /// the tables carry no straw; kMaterialsShort when the village has not the
  /// straw in full at the order (kStartBuild's rule). The start's old houses
  /// may be insulated: straw is exactly the first winter's measure, and the
  /// "replaced, not improved" of housing §10 is about repair. Decided in the
  /// step it is read. Seam key `insulate_unit`. Consumer: core_construction.
  kInsulateUnit,

  /// TAKE THE TEAM TO NIGHT PASTURE (livestock design, «Ночное — единственный
  /// выпас, и он ночной»; boss, parcel 63). Names no subject: there is one
  /// team and one floodplain, and the chairman's decision is whether the
  /// summer nights are spent grazing or in the yard.
  ///
  /// IT IS A STANDING ORDER AND NOT ONE NIGHT. «Табун впервые уведён в
  /// ночное, и с этого лета уводится каждое»: given once, it holds while the
  /// three conditions hold and stops when one of them fails — which is what
  /// makes it a decision rather than a nightly chore.
  ///
  /// THE THREE CONDITIONS ARE THE DESIGN'S, all of them, and each refuses
  /// with kRuleForbids because the repair for every one is the calendar or a
  /// building and never a different order:
  ///   * SUMMER, and summer here means the school holidays — June, July,
  ///     August (education design §10). Not the pasture season, which runs
  ///     five months: the children are what make the night pasture possible,
  ///     and they are at school for two of those five.
  ///   * THE TEAM GATHERED IN ONE PLACE, which is the collective yard doing
  ///     its work (ChairmanState::horses_stabled). «Пока лошади стоят по
  ///     личным дворам, уводить некого и некому.»
  ///   * CHILDREN OF THE SENIOR SCHOOL BAND, who guard it. Their number is a
  ///     requirement and not yet a cost: measured 2026-09-17, a teenager in
  ///     Epoch I has no second occupation at all — work begins at sixteen and
  ///     school stops for the holidays — so the night pasture is today the
  ///     only thing that band can do. The competition the design describes
  ///     (кружки, тимуровцы, страда) arrives with those, and it will cost
  ///     what it costs then without this rule changing.
  ///
  /// Seam key `graze_at_night`. Consumer: core_production.
  kGrazeAtNight,

  /// HANDS HEAD BACK TO THE DISTRICT FOR LIMIT POINTS — the way out of a herd
  /// there is nothing else to do with (livestock design, «Лишних лошадей
  /// сдают райкому»). `herd` names the herd, `amount` how many head.
  ///
  /// IT EXISTS BECAUSE THE PURCHASE DID AND THE WAY BACK DID NOT. The
  /// district sells a head on the limit and, until this order, the village
  /// had no verb to be rid of one: from Epoch II machinery displaces the
  /// team, and a horse nobody drives still eats oats, hay, a stall and a
  /// groom's days. «Сдача — способ выйти из тягла не убийством, а сделкой.»
  ///
  /// THE OLDEST GO FIRST, and the order does not name which head. A head is
  /// not an entity in this model — a herd is counts and one summed age — so
  /// naming one would need a field, a byte in two codecs and a save format,
  /// bought for an interface rather than for a mechanic. Two arguments of
  /// the design point the same way: an old animal «дороже в содержании и
  /// дешевле при сдаче — держать её до последнего дня не обязательно», and
  /// micromanagement is meant to FALL from epoch to epoch, not to start in
  /// the first with the chairman picking horses by name.
  ///
  /// THE HEAD LEAVES AT ONCE: no cart, no waiting, unlike the purchase whose
  /// head takes days to come. The asymmetry is the design's and deliberate.
  ///
  /// Refusals of its own: kLastSire (the herd's last breeding male stays),
  /// kNoSuchSubject (no such herd), kNotEligible (a yard's own animal is not
  /// the chairman's to sell, and a kind the district takes only by the batch
  /// has no per-head price).
  ///
  /// Seam key `hand_stock`. Consumer: core_production.
  kHandStock,

  /// THE CHAIRMAN TOOK A NIGHT TRADER AT IT (crime design §7, §9): `resident`
  /// names the man, and from this step he keeps no trade. The year's turn
  /// hands the trade out again to whoever it draws — «Выбывший не замещается
  /// до следующего перелома», and the hunt is yearly by design (boss,
  /// 2026-09-18).
  ///
  /// IT EXISTS BECAUSE THE WORLD HAD NO WAY DOWN. Alcoholism rises by +2
  /// while the village has any distiller, and nothing in the core ever set a
  /// trade back to none: the host judged catches that reached nobody, and a
  /// run with 38 catches drew the same curve as one with 11 (host, 0.7.250).
  /// «Взял последнего — метрика по селу поползла вниз» had no door to walk
  /// through.
  ///
  /// THE CATCH IS NOT JUDGED HERE. Whether the chairman stood near enough,
  /// at the right hour, unseen, is host's rule (host_script/night_catch.h),
  /// and a second copy of it here would be one rule with two houses. The
  /// core answers only what it alone knows: is there such a man, and does
  /// he keep a trade. Any of the three trades — a poacher taken stops
  /// carrying fish as a distiller taken stops distilling (§9).
  ///
  /// Refusals: kNoSuchSubject (no such resident), kNotEligible (he keeps no
  /// trade).
  ///
  /// Seam key `take_night_trader`. Consumer: core_residents.
  kTakeNightTrader,

  /// THE CHAIRMAN TAKES THE VILLAGE INTO THE NEXT ERA (epochs §6, «Механика
  /// самого перехода»; epochs §8, «Переход в Эпоху II в сборке Эпохи I —
  /// выбор игрока, когда готовность выполнена»). No target fields.
  ///
  /// Done when the readiness as of the last year's turn holds: both indices
  /// above their thresholds for three years running, then the six blocks.
  /// Refused otherwise, and the refusal names the FIRST that is not met, in
  /// that order — the indices first, because they are step 1 of the design's
  /// transition and the blocks step 2, and the blocks in the order of their
  /// one complete list (epochs §6, «Отдельные пороговые условия»): kIndicesNotHeld,
  /// kNoOwnTraction, kWinteringNotClosed, kOfficeNotRepaired,
  /// kFoodVarietyShort, kSocialObjectsShort, kUnitsBelowLevel. In Epoch II
  /// and later: kNotEligible — the build is Epoch I only (the human, 18
  /// September 2026: «пока делаем ТОЛЬКО эпоху 1»).
  ///
  /// NO GENERAL MEETING, AND NOT AS A STUB. The design's step 2 has the
  /// meeting that may refuse, and epochs §8 (line 438 on 2026-09-18) says it
  /// is not in this build: «собрания в сборке нет: оно и его отказ „при
  /// низком авторитете" относятся к сезону перехода, а он в сборку не
  /// входит». A decided absence, not an unwritten rule — there is nothing to
  /// take off until the transition season is built.
  ///
  /// IT REPLACES THE POPULATION DOOR. Until 2026-09-18 core_residents moved
  /// the era itself when the village reached 500 people (UpdateEpoch), and a
  /// second door to one action is how a rule hung on the first is walked
  /// round in silence: 500 is the design's growth target, not a gate.
  ///
  /// Seam key `advance_era`. Consumer: core_world (the readiness is there).
  kAdvanceEra,

  /// THE MINIMUM RATION, SWITCHED BY THE CHAIRMAN (labor-payment §5, «Кто
  /// включает — председатель, для конкретной семьи или для всех сразу»;
  /// econ's audit M3, Л1). `enable` 1 switches on, 0 off.
  ///   * `family` INVALID — the village-wide automatic ration, the checkbox
  ///     (ChairmanState::ration_auto): every family that sinks to the
  ///     threshold is given the ration.
  ///   * `family` VALID — the decision for that yard (FamilyRow::ration_granted):
  ///     given the ration at the threshold whatever the checkbox says.
  /// Either way the ration still waits for the family's satiety to reach
  /// `ration_satiety_threshold`: the switch says WHO may have it, the
  /// threshold says WHEN.
  ///
  /// WHY IT EXISTS: until 2026-09-18 the ration was a table constant armed
  /// for everyone, and «паёк платит цену скупой выдачи за игрока» (econ,
  /// measured: at half norms the auto ration issued 102 times more, and the
  /// village's satiety stayed inside the spread). While the ration answered
  /// for the player, the issue norms had no price.
  ///
  /// Refusals: kNoSuchSubject (no such family), kRuleForbids (already so —
  /// a repeat means the chairman is looking at something stale, as with
  /// the pause). Seam key `set_ration`. Consumer: core_residents.
  kSetRation,

  /// THE ISSUE NORM OF ONE POSITION OF THE BUNDLE (labor-payment §3; econ's
  /// audit M1, Л1): `resource` is the position, `amount` its grams per
  /// trudoden from the next distribution on; 0 strikes the position out of
  /// the bundle. The other positions keep theirs (WorldState::issue_norms).
  ///
  /// WHY IT EXISTS: the norm was a table constant, and the table's own
  /// comment called it «нормы председателя». Without it none of the three
  /// sides of «накормить / сдать / посеять» was in the player's hands.
  ///
  /// Refusals: kNotEligible (the resource is not food — hay and straw are
  /// fed through the fodder table, not the bundle). The boundary refuses a
  /// norm over kMaxIssueNormGrams (1000 kg a trudoden, a typo bound) by
  /// shape. Seam key `set_issue_norm`.
  /// Consumer: core_residents.
  kSetIssueNorm,

  /// «СДАТЬ СЕЙЧАС» (econ's audit M2 in its minimum form, Л1; district §9):
  /// what is still owed of the plan's position `resource` — or of every
  /// position when `resource` is invalid — leaves the stores now, as much as
  /// they hold. «Держать до срока» is the default and needs no order: the
  /// year's turn ships whatever is still owed. A shipment that moves the
  /// grain out early frees the store and ends its rot in the kolkhoz's
  /// hands; what is shipped cannot be handed out after.
  ///
  /// `amount` > 0 (since 2026-09-18; district §1, overfulfilment): ship that
  /// many grams of the position `resource`, OVER what is owed as readily as
  /// under it — the surplus is the overfulfilment the district pays for in
  /// limit points. `amount` 0 is «the whole debt», as before. A resource the
  /// plan asks nothing of is not a position: refused until the milk package
  /// brings deliveries without one.
  ///
  /// Refusals: kNoPlanYet (the spring's figure is not named yet),
  /// kRuleForbids (nothing left the stores: nothing owed, none of it there,
  /// or the resource is no position of the plan). The boundary refuses a
  /// negative amount, and an amount with no resource, by shape. Seam key
  /// `deliver_plan`. Consumer: core_production.
  kDeliverPlan,

  // Reserved, appended by their tasks and named here so the numbering is
  // planned rather than discovered: nomenclature (unit rules §6), transport
  // as part of orders (root decision 155, task A4), delegation (Epoch II).
  //
  // APPEND BEFORE kOrderKindCount. That is the whole rule, and it is a fact
  // about this enum rather than an instruction about two other files.
  //
  // It used to be the instruction: "raise kMaxOrderKind in
  // core_save/save_rows.cpp AND in core_boundary/journal_codec.cpp". Two
  // homes, one rule, and the journal's copy sat at kDemolishUnit for two
  // whole tasks after A2 appended two kinds — found by a design pass rather
  // than by anything that runs, and a journal carrying kStartBuild would
  // have been refused on decode. A rule whose only mechanism is a comment is
  // not a mechanism, it is an intention, and an intention fails no check
  // because it takes part in none (boss, 2026-09-04).
  //
  // The count is safe to add even though ShapeIsValid in
  // core_boundary/session.cpp switches over this enum WITHOUT a default: a
  // new REAL kind is still a compile error there until it is handled, which
  // is the tripwire that note was protecting. The sentinel is handled once,
  // beside kNone, and refused for the same reason.
  //
  // The same holds over OrderStatus and OrderRefusal.

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kOrderKindCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kOrderKindCount,
};

/// @brief Where an order stands. Terminal statuses are removed by the events
/// slot in the step they are reached, after the matching event goes out.
enum class OrderStatus : std::uint8_t {
  kPending = 0,  ///< Applied this step; no consumer has read it yet.
  kAccepted,     ///< Validated, waiting for its moment. Cancellable.
  kActive,       ///< Execution began. Finished, never taken back.
  kDone,         ///< Terminal.
  kRefused,      ///< Terminal; `refusal` says why.
  kCancelled,    ///< Terminal; cancelled while kPending or kAccepted.

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kOrderStatusCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kOrderStatusCount,
};

/// @brief Why an order was refused. The presentation turns the code into a
/// string (db/strings.db); the core never carries text.
enum class OrderRefusal : std::uint8_t {
  kNone = 0,
  kNoConsumer,           ///< No wired subsystem handles this kind (yet).
  kNoSuchSubject,        ///< A required id names no live entity.
  kNotEligible,          ///< The subject may not do this (a child at the plough).
  kConflictsWithActive,  ///< The one-active-task rule (time design §11).
  kRuleForbids,          ///< Some other rule of the consumer; its event says which.

  /// The unit type's gate is shut (unlocks design §1): its era has not
  /// come, or it opens by an event, quest or unit that has not happened.
  /// The presentation names the reason in words FROM ITS OWN COPY of
  /// unit_types.csv — the refused order's unit_type says which type, the
  /// type's `gate` and `gate_ref` say what opens it. The core carries no
  /// text and no dictionary (unlocks design §4; 70-boundary.md §7).
  kGateClosed,

  /// The plot would overlap another unit's: two units cannot stand closer
  /// than the sum of their radii (unit rules §9). The refusal event names
  /// the neighbour in SimEvent::unit.
  kTooClose,

  /// Demolition refused because something lives here: a household in a
  /// house, a herd at a barn (unit rules §14 — "the living is not
  /// demolished"; the stock, by contrast, is moved out, not refused).
  kNotEmpty,

  /// Appointment refused because every slot of that post at that unit is
  /// taken (task A7; the staff table of the design db says how many —
  /// one groom per farm, and it is the whole reason the refusal has a
  /// name of its own: "no room" at a post is a fact the presentation
  /// shows, not a rule it has to guess).
  kNoVacancy,

  /// The field cannot carry this kind of work AT ALL, and never will: a
  /// meadow is mown and never ploughed, harrowed or sown (land design, the
  /// meadow branch of the production day).
  ///
  /// It named unraised land too until 2026-09-12, and that reading is gone
  /// with the land kind behind it: the design says an overgrown field is a
  /// LOOK and not a state, ploughed by the same norm as any other ground
  /// (farming design, «Вспашка одна на любую землю»: "целина, залежь,
  /// задерневшее поле, пашня, которую держат двадцать лет подряд — норма
  /// вспашки одна").
  ///
  /// THE QUOTATION HERE WAS WRONG UNTIL THE SECOND ANALYSIS PASS CHECKED IT
  /// against the document — close in substance, invented in form, and inside
  /// quotation marks, which is the part that makes it a defect rather than a
  /// paraphrase. A reader searching the design for the sentence would not
  /// have found it.
  ///
  /// THE POINT IS THE WORD "NEVER", AND IT IS WHY THIS HAS A NAME OF ITS
  /// OWN rather than falling into kRuleForbids. An order whose field has
  /// simply moved on to another phase today is REFUSED BY NOTHING and stands
  /// — the phase comes round again, and "no work today" is honest silence.
  /// This one never comes round. An accepted order that can never fire, and
  /// says nothing, is exactly "do not punish the unforeseeable" (root rules
  /// §7): the chairman sees a man standing idle and can learn the reason
  /// from nowhere, because the reason is permanent and has no sign.
  ///
  /// The refused order carries the field and the work kind already, so the
  /// presentation reads WHICH field and WHICH work off the order row and
  /// needs no dictionary from the core — the same shape as kGateClosed.
  /// EXCEPT FOR ITS NEWEST CALLER: a kSetRotation row names a field and
  /// carries WorkKind::kNone, so there the presentation has the field and
  /// nothing else to read. The word "never" still fits — a meadow is mown
  /// where it grew and can carry no sowing ever — but the sentence above
  /// describes the work orders only (analysis, 0.17.96).
  kWrongLand,

  /// The district has not named this year's plan yet, so a SHARE of it does
  /// not exist to open.
  ///
  /// RAISED BY AN EMPTY `plan.due` AND NOT BY THE CALENDAR, which is wider
  /// than the window it was written for. In a running campaign that window
  /// is the eight game days of forty-eight between the year's turn, where
  /// JudgePlan clears the vector, and the first day of spring, where
  /// AnnouncePlan fills it again (boss, 2026-09-12; district design §9). But
  /// a settlement that worked NO ARABLE last year, or whose tables name no
  /// plan positions or a zero share, is announced an empty plan and gets this
  /// answer all year — which is still TRUE, and still the most useful thing
  /// that can be said, but a reader expecting "only in February" would be
  /// wrong about it.
  ///
  /// THE CROPS IN THE ROTATION SLOTS ARE NO LONGER THE TEST, and this
  /// sentence named them until 2026-09-13: the norm came off the crop
  /// standing in each field's year0, so a settlement that sowed nothing owed
  /// nothing. It is off the area worked last year and the district's own
  /// positions now, which is what makes "sowing less does not owe less" true
  /// (production_system.cpp, AnnouncePlan).
  ///
  /// A NAME OF ITS OWN BECAUSE THE OTHER TWO READINGS ARE BOTH FALSE. The
  /// fund is not empty — the grain is in the stores where it always was. The
  /// rule does not forbid it — unsealing the reserve in a hungry winter is
  /// exactly what the design says a chairman may do. What is missing is the
  /// NUMBER the share is taken from, and a chairman told "rule forbids"
  /// would go looking for a rule that does not exist.
  ///
  /// And the move it teaches is a real one: wait for the spring
  /// announcement. A refusal a player can act on is the difference between
  /// difficulty and irritation (root rules §7 — a problem must be
  /// preventable, and an unintelligible one cannot be).
  kNoPlanYet,

  /// A slot of a rotation names a crop this build's table does not carry.
  ///
  /// SPLIT OFF kNoSuchSubject ON 2026-09-12, and for the reason kGateClosed
  /// and kWrongLand have names of their own: THE TWO CASES HAVE DIFFERENT
  /// REPAIRS. No such field — choose another field. No such crop — choose
  /// another crop. One code answering for both tells the chairman that
  /// something was missing and leaves him to guess which, which is "do not
  /// punish the unforeseeable" turned inside out (root rules §7).
  ///
  /// THE ORDER ROW CARRIES ALL THREE SLOTS AND THE REFUSAL NAMES NO ONE OF
  /// THEM. The presentation can say "one of these crops is not in this
  /// build" and can find it by checking the three against its own copy of
  /// crops.csv, which is exactly the check the core just made — but the core
  /// does not hand it the answer. Naming the slot would be a field on the
  /// refusal, and nothing has asked for one yet.
  ///
  /// IT IS A LAYER AND A CORE DISAGREEING ABOUT THE CROP TABLE, never a
  /// player's mistake: the boundary checks the SHAPE of an order and the
  /// roster is core_production's knowledge. An empty slot is not this — it
  /// is a fallow year, and three of them are the chairman taking his word
  /// back (land_state.h, rotation_assigned).
  kNoSuchCrop,

  // A REFUSAL APPENDED HERE NEEDS NOTHING DONE IN core_save: kMaxOrderRefusal
  // is derived from the count below and raises itself. This comment used to
  // say the opposite — "means raising kMaxOrderRefusal in save_rows.cpp" —
  // and it was stale: the hand-written constant it names has not existed
  // since the MEM-002 fix. A reader obeying it would have gone looking for a
  // number that is not there, and the honest way to obey it is to write one,
  // which is the very drift the counts were introduced to end.
  //
  // WHAT IT DOES COST is outside this tree: the graphics layer keeps a
  // MIRROR of these reasons — the mirror lives over there, and the fact that
  // it exists is recorded here, in core_save/save_rows.cpp, which says the
  // layer once "had eight refusal reasons against our nine and had no way to
  // notice". So appending one is a boundary event and the layer has to be
  // told. Old saves keep opening either way — their values stay inside the
  // widened range.

  /// A MODULE WITHOUT ITS PARENT (unit rules §11, "Модули"; boss, parcel
  /// 198): kBuildUnit of a module type found no parent unit it could go on —
  /// none of the parent type standing sound (built, not dead, not paused),
  /// or the position is not inside such a parent's plot. Also the answer to
  /// kStartBuild and kUpgradeUnit of a module whose parent has since stopped
  /// standing sound. Named apart from kRuleForbids because the chairman's
  /// remedy is a different building, not a different figure.
  kNoParent,

  /// kOrderLimitLot: the lot costs more points than are left this year.
  /// Named apart from kRuleForbids because the remedy is the next year's
  /// grant or a cheaper lot, not a different order (boss, parcel 211).
  kLimitShort,

  /// kStartBuild, kUpgradeUnit: some line of the level's recipe is not in the
  /// village IN FULL — the stores, the heaps and what already lies on the site
  /// together (construction design §6, "старт проверяет материалы — и
  /// называет, чего не хватает"; the human's word of 2026-09-14). The works do
  /// not start. WHAT is short, line by line, is answered by the construction
  /// door MaterialsShortFor (the session's too), not carried on the row: the
  /// order row is a fixed-width record and a recipe is a list. Seam key
  /// `materials_short`.
  kMaterialsShort,

  /// kBuildUnit field_camp: the plot lies on arable land — the camp stands
  /// "не на пашне" (MTS design §1; boss, parcel 449). Seam key `on_arable`.
  kOnArable,

  /// kBuildUnit field_camp: no field lies within the camp's reach of its
  /// centre — "близко к полям", 1 km (boss, parcel 449; STUB). Seam key
  /// `too_far_from_fields`.
  kTooFarFromFields,

  /// kOrderLimitLot on a LIVESTOCK lot: the village has nowhere to put the
  /// head, counting the roofs AND the private yards (district design §1,
  /// «если ставить некуда — строка недоступна»; livestock design, «нет крыши
  /// или нет мест»). Seam key `no_room_for_stock`.
  ///
  /// A NAME OF ITS OWN, and not kRuleForbids, because the repair is a
  /// different one and a real one: build a byre, a stable or a house, or
  /// wait. kRuleForbids would send the chairman looking for a rule, and the
  /// rule is not the obstacle — the room is.
  ///
  /// THE YARDS COUNT TOWARDS IT, which is what makes this refusal narrow
  /// enough to be safe. A head with no roof is billeted at the private yards
  /// and always was (herd_system.cpp, RunBilleting) — that is what the start
  /// canon does with all sixteen horses — so the village runs out of room
  /// only when the roofs AND the yards are full together. A ceiling that
  /// counted roofs alone would refuse the first horse of a farm that had just
  /// lost its team, which is the deadlock this window exists to open.
  kNoRoomForStock,

  /// kHandStock: the herd's last breeding male, and handing him over would
  /// leave the village with a herd that cannot breed and no way back but
  /// buying one. Seam key `last_sire`.
  ///
  /// THE MIRROR OF A GUARD THE PURCHASE ALREADY HAS. The district asks which
  /// sex when it sells a head, and the design says why in as many words:
  /// «иначе хозяйство могло бы остаться без производителя и без всякого
  /// способа это исправить — а безвыходных ситуаций мы не делаем»
  /// (livestock design). A door out of one dead end that opens the way into
  /// another is not an exit, so the way back carries the same guard as the
  /// way in.
  ///
  /// It refuses only the LAST one: a herd with two sires may hand one over,
  /// and a kind the table gives no males at all (`males_share == 0`, the
  /// goat) never meets this refusal, because it has no sire to be the last.
  kLastSire,

  // THE SEVEN REFUSALS OF kAdvanceEra (2026-09-18), one per unmet condition,
  // so that the presentation names WHAT is missing without a field on the
  // row: the order carries no subject, and the refusal byte is the only
  // place the answer can ride. Boss chose seven words over one word and a
  // number (thread boss-core-epoch1-next, seq 17). The readiness state
  // (readiness_state.h) holds the figures behind each.

  /// Both indices have not stood above their thresholds for three years
  /// running. Seam key `indices_not_held`.
  kIndicesNotHeld,

  /// No horses of the farm's own and no repair base. Seam key
  /// `no_own_traction`.
  kNoOwnTraction,

  /// The wintering did not close in each of the last two years. Seam key
  /// `wintering_not_closed`.
  kWinteringNotClosed,

  /// The office is not standing at wear 1 per cent or less. Seam key
  /// `office_not_repaired`.
  kOfficeNotRepaired,

  /// The village did not eat the era's number of food categories in all four
  /// seasons of the last year. Seam key `food_variety_short`.
  kFoodVarietyShort,

  /// Fewer than four of the era's six social objects stand. Seam key
  /// `social_objects_short`.
  kSocialObjectsShort,

  /// Some unit stands below the level the era asks of it. Seam key
  /// `units_below_level`.
  kUnitsBelowLevel,

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kOrderRefusalCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kOrderRefusalCount,
};

/// The largest issue norm a kSetIssueNorm may carry, grams per trudoden: a
/// bound on a TYPO, not on a decision. A thousand kilograms a trudoden is
/// past anything a trudoden could buy of any position.
///
/// IT WAS TEN KILOGRAMS in 0.32.7 and 0.32.8, on the claim "five times
/// food.csv's largest position" — and food.csv gives milk 30 kg a trudoden, so the
/// chairman could not even order milk's own table norm back (host, 0.32.8,
/// the "milk ×2" arm refused 7 of 7). A bound on a quantity must sit above
/// the table that feeds it; food_config.cpp static_asserts that its parse
/// ceiling for `issue_kg_per_trudoden` passes this bound.
inline constexpr Grams kMaxIssueNormGrams = 1000 * kGramsPerKilogram;

/// @brief Which of the sealed funds an order unseals (resources design §6).
/// The ladder has three rungs and only the top two are sealed: the kolkhoz
/// fund is what the automation already spends, so there is nothing in it to
/// unseal.
enum class FundKind : std::uint8_t {
  /// Not a fund order. The value every other kind of order carries.
  kNone = 0,

  /// The seed fund — next year's sowing. Unsealing it risks the spring.
  kSeed,

  /// The plan reserve — what is still owed to the district out of this
  /// year's harvest. Unsealing it risks the autumn's delivery.
  kPlanReserve,

  /// The fodder fund — the working stock's year of feed grain, oats and
  /// barley (resources design §6, third rung; boss's decision of
  /// 2026-09-12). Unsealing it risks the SPRING SOWING, and risks it
  /// slowly: horses that wintered on hay alone still plough, they plough
  /// for longer, and the chairman learns the price of his decision from the
  /// calendar rather than from a window.
  ///
  /// It sits BELOW the plan on purpose: in a poor year the district takes
  /// first and the horse goes thin before the delivery falls short.
  kFodder,

  /// NOT A VALUE: the count, for the codecs' range check. Append before it.
  kFundKindCount,
};

/// @brief One order. Plain data; `kind` says which target fields are read,
/// the rest stay at their invalid defaults. Sized for the save codec's
/// tripwire like every row.
struct OrderRow {
  OrderKind kind = OrderKind::kNone;

  OrderStatus status = OrderStatus::kPending;

  OrderRefusal refusal = OrderRefusal::kNone;

  /// For kAssignWork: the kind of work. kNone otherwise.
  WorkKind work = WorkKind::kNone;

  /// For kAppoint: which post (task A7). It stands here, among the flavour
  /// fields and not down with the targets, for the same reason `work` does —
  /// both say WHICH KIND of a thing the order is about, not which thing —
  /// and because the four leading bytes leave a hole exactly this wide.
  ///
  /// THE ROW IS 64 BYTES SINCE 2026-09-12, not the 48 this sentence claimed
  /// until then: kUnsealFund's fund, resource and amount took it past the
  /// alignment boundary. Corrected here rather than left as a stale aside
  /// because BOTH hand-counted wire lengths stand on this prose — the save
  /// codec's tripwire and the journal's kOrderBytes — and a comment naming
  /// the old size is the second home of a fact whose first home moved.
  ProfessionId profession;

  /// The completed tick the order was issued after (boundary stamp at the
  /// moment of IssueOrder); the row is applied at the start of the step
  /// that follows. The journal replays by this tick.
  Tick issued_tick = 0;

  // -- targets, by kind ------------------------------------------------------
  ResidentId resident;  ///< kAssignWork, kReleaseWork, kAppoint, kDismiss.

  UnitId unit;  ///< kPauseUnit, kResumeUnit, kDemolishUnit, kAppoint; kAssignWork at a unit.

  FieldId field;  ///< kAssignWork (field kinds), kSetRotation.

  HerdId herd;  ///< kAssignWork (kHerdCare).

  UnitTypeId unit_type;  ///< kBuildUnit.

  CropId rotation_year0;  ///< kSetRotation; invalid = fallow that year.

  CropId rotation_year1;

  CropId rotation_year2;

  Vec2 position;  ///< kBuildUnit.

  /// kUnsealFund: which fund is being opened. kNone on every other kind.
  FundKind fund = FundKind::kNone;

  /// kUnsealFund: what is being taken out of it.
  ResourceId resource;

  /// kUnsealFund: how much, in grams; kMarkExtraction: how much to dig, in
  /// grams. THE FIGURE IS THE CHAIRMAN'S and the
  /// order carries it rather than meaning "as much as is needed": resources
  /// design §6 calls the unsealing "осознанный выбор, а не незаметная
  /// утечка", and a door that opens by itself to whatever width is wanted is
  /// the leak that sentence refuses.
  Grams amount = 0;

  /// kMarkFelling: the stand to fell on.
  TimberStandId stand;

  /// kMarkFelling: how much of the stand's stock to fell, cubic metres. The
  /// chairman's figure, for the same reason `amount` is.
  float volume_m3 = 0.0F;

  /// kOrderLimitLot: the catalogue row to buy.
  LimitLotId lot;

  /// kOrderLimitLot on a LIVESTOCK lot: 0/1, whether the head is to arrive
  /// male. The chairman's choice and not a draw — livestock design: «При
  /// заказе у райкома пол выбирается. Иначе хозяйство могло бы остаться без
  /// производителя и без всякого способа это исправить — а безвыходных
  /// ситуаций мы не делаем.»
  ///
  /// READ ONLY WHEN THE LOT ASKS FOR IT (LimitLotDef::sex_choice). A batch —
  /// piglets, chicks — comes mixed, and nobody chooses the sex of ten chicks;
  /// the field is taken as 0 there rather than refused, because a value the
  /// order was never meant to carry is not the chairman making a mistake.
  std::uint8_t male = 0;

  /// kMarkExtraction: the site to dig on; the mass is `amount`, in grams.
  ExtractionSiteId extraction_site;

  /// kSetRation: the yard the decision is for; invalid = the whole village.
  FamilyId family;

  /// kSetRation: 1 switches on, 0 off. A byte of its own rather than `male`
  /// or `amount` borrowed: a seam field read under two meanings is how a
  /// layer ends up sending a sex where a switch was meant.
  std::uint8_t enable = 0;
};

/// @brief The order book type used by WorldState.
using OrderTable = StateTable<OrderId, OrderRow>;

/// @brief The batch the session has staged and the engine has not applied
/// yet: issued rows in issue order, then ids to cancel. Between two steps
/// this is the ONLY state that is neither in the completed world nor in
/// the journal — and a campaign is saved between steps, on pause, after
/// the player has handed out the day's orders. A save that dropped it would
/// punish the player for the unforeseeable (root principle), so the save
/// format carries it as a section beside the world (core_save/save.h) and
/// the session hands it back on load (core_boundary/session.h,
/// StagedBatch / ReplaceWorld). Boss decision of 2026-08-31, project phase
/// 2; the same struct is what ISimulation::StageOrders takes, as two spans.
struct StagedOrders {
  std::vector<OrderRow> issued;

  std::vector<OrderId> cancelled;
};

}  // namespace core

#endif  // CORE_COMMON_ORDER_STATE_H_
