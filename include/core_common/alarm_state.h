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

  /// A field is GROWING a crop the stores will not hold. The warning before
  /// the loss — there is still a season in which to free a store or raise
  /// one, and that is exactly what separates this kind from the next one
  /// (boss, 2026-09-03: the player must tell "build now" from "cart it
  /// away" at a glance). Subject: `field`; `resource` = the crop's produce;
  /// `amount` = this field's own grams that would not fit.
  ///
  /// THE FIELDS SHARE ONE ROOM, and the test is against what is left of it
  /// after the other growing fields, not against the whole of it. Measured
  /// against the whole, three fields of fifty tonnes facing sixty each
  /// "fit" and nobody is warned (host, 2026-09-05). The amounts of the
  /// fields that overrun sum to the true overrun.
  ///
  /// IT BURNS UNTIL THE HARVEST IS RESOLVED — carried into a store or
  /// written off — and NOT until the field changes phase. It used to go out
  /// when the crop left kGrowing, and host measured what that looked like
  /// from outside: a median of four days of silence between the warning
  /// going dark and the load hitting the ground, every time. A signal that
  /// switches off just before the trouble does not read as silence, it
  /// reads as "it turned out fine" (host, 2026-09-05), and the player acts
  /// on the last state he was shown.
  ///
  /// THE ESTIMATE IS STILL THE GROWING FIELD'S. On the other two states the
  /// quantity is known better rather than worse — the part still standing
  /// (FieldRow::harvest_laid_share, the harvest by parts), and the weight of
  /// the heap — so there is
  /// something honest to burn on. Two questions that `phase == kGrowing`
  /// used to answer with one word: can this be estimated, and should this
  /// go on warning.
  ///
  /// THE RESOURCE IS THE CAUSE (0.37.50; boss-core-epoch1-resume-2026-09-30
  /// [64]): the crop, unless the crop fits its stores and only its straw
  /// does not — then `resource` is the straw, and the layer's advice is the
  /// stack (`alarm.harvest_will_not_fit.straw_tip`), not the granary or the
  /// clamp (`...tip`). Room by resource since 0.37.46: the straw's home is
  /// the stack.
  kHarvestWillNotFit,

  /// A field is holding produce already reaped and not yet carted
  /// (FieldRow::reaped_grams > 0): off the crop and in nobody's store. Since
  /// the harvest by parts (0.34.44) it stands from the first day of a reaping
  /// too, while the day's cut waits for the carts.
  /// Subject: `field`; `resource` = the produce; `amount` = the grams
  /// waiting.
  kHarvestWaitingOnField,

  /// The rotation assigned to a field asks for more seed than the stores
  /// hold — grain or potato of the crop it sows next (farming design §7:
  /// "an alarm at assignment, not in spring"). Stands from the day the
  /// rotation is set until the stores cover the norm, and again after a
  /// sowing that went short. The need is summed over every field of the
  /// same seed and set against the store once (0.34.45). Subject: `field`;
  /// `resource` = the seed; `amount` = the field's share of that seed's
  /// shortfall, in grams, by its need — one seed's alarms sum to its
  /// shortfall within a gram per field (each share is rounded, and never
  /// below one gram).
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

  /// A site has its materials and is waiting for HANDS: it is in
  /// ConstructionPhase::kBuilding and not one resident is assigned to it
  /// today. Subject: `unit`.
  ///
  /// WHY THIS EXISTS, and it is not about construction. kStoreFull tells the
  /// player to build a store. A probe did exactly that — `start_build`, no
  /// refusal, plot marked — and the site stood four hundred days with a crew
  /// of zero, because workers do not come to a site by themselves and a
  /// second order (`assign_work … construction`) says nothing about itself.
  ///
  /// A PIECE OF ADVICE ANSWERS FOR THE SUFFICIENCY OF THE ACTION IT NAMES.
  /// "Build a store", when a store is not built by one command, is not a
  /// hint but a trap — because it looks carried out (boss, 2026-09-05).
  ///
  /// THE CONDITION IS "NOBODY IS ON IT TODAY", not "nobody was ever
  /// assigned". The host's instrument settled that: it assigned six men
  /// once and the third store still stood empty, because yesterday's hands
  /// are in the fields today, on another site, or dead. An alarm that goes
  /// out on the first order goes out early and leads back to where it came
  /// from.
  ///
  /// kBuilding alone, and the two other labour phases are deliberately left
  /// out: a repair works meanwhile at its own level and a demolition is not
  /// a thing the player was advised to do — neither leaves a site standing
  /// that he could mistake for progress.
  kSiteWithoutCrew,

  /// NOBODY CAN GET THERE AND BACK IN A DAY: the road from the NEAREST
  /// dwelling is one the accountant will not send anybody down — longer than
  /// labor.csv travel_limit_hours, or leaving less than min_usable_hours of
  /// the daylight after the road there and back. Subject: `unit`; `amount` =
  /// the hours of road, one way, in game hours.
  ///
  /// THE ACCOUNTANT'S OWN TEST, since 2026-09-13. It was "the round trip does
  /// not fit in daylight", which is laxer than the assignment's travel limit:
  /// a site between the two stood crewless all year with no word of why.
  ///
  /// FROM THE NEAREST HOUSE AND NOT FROM THE VILLAGE'S MIDDLE, because
  /// distance is not a vice: a homestead two kilometres out is legitimate
  /// and reachable by its own household, and measuring it against the far
  /// side of the settlement would forbid people to spread out at all.
  ///
  /// IT WAS `kNoRoad` UNTIL 2026-09-05 — "the core has no roads, so the
  /// predicate yields nothing", a kind kept in the roster for the layer's
  /// map key. It never fired once. What made it measurable was not roads
  /// but HOURS: a site is out of reach when the day is too short for the
  /// walk, and that needs no road at all. The stub came alive without moving.
  ///
  /// A WARNING AND NOT A REFUSAL AT THE ORDER, for two reasons and the
  /// second is the stronger: distance is legitimate, and the window is
  /// SHORTER IN WINTER — so a site unreachable in December is reachable in
  /// June, and this goes out by itself when the day grows. A refusal would
  /// have to be revisited every morning or lie once and for ever.
  kSiteUnreachable,

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

  /// THE TEAM CANNOT RENEW ITSELF: the kolkhoz owns horses while its yard
  /// has not reached its SECOND step, the stable, and horses foal only
  /// under that roof (livestock design §5). Subject: `herd` (the team's
  /// first row); `amount` = the heads still alive. LIT FROM THE FOUNDING
  /// MORNING (production_alarms.cpp says why) — so it cannot say that the
  /// team has begun to age; kHerdAging says that.
  ///
  /// THE CONDITION IS A STATE AND NOT AN OUTCOME, because the outcome has
  /// no warning form. Measured: the team stands at 26 head on day 160 and
  /// at zero on day 168, and between 41 head and 26 there is nothing a
  /// player could read as a slope (core, 2026-09-05). This line said until
  /// 0.36.33 that the predicate was "the herd is now losing heads to age";
  /// the code lit it on the founding morning from the start.
  ///
  /// IT DOES NOT GO OUT WHEN A GROOM IS APPOINTED, and that is the whole
  /// reason it exists beside kYardWithoutGroom. That one clears on the
  /// appointment — correctly, it asked for a groom and got one — and the
  /// team goes on dying in silence. Silence then says a third thing, which
  /// the office knows nothing about: not "solved" and not "not yet", but
  /// "you did what was asked and it did not help" (office design §14a;
  /// boss, 2026-09-05). AN ALARM THAT GOES OUT IN ANSWER TO A CORRECT
  /// ACTION LIES, and lies worse than one that never lit: the player is
  /// not left in the dark, he is told he is done.
  ///
  /// AGAINST THE SECOND STEP, not against "no yard" and not against "no
  /// groom": the yard at step one is a pen, and a pen breeds nobody.
  kHerdWithoutStable,

  // -- the district: core_production ---------------------------------------------

  /// A position of the district's plan that the fields will NOT GROW ON ENOUGH
  /// HECTARES in one of the three years the rotation chains lay out (boss's
  /// decisions of 2026-09-13). Subject: `resource` — the position's produce;
  /// `amount` = the year it is short in, counted from the current one: 0 this
  /// year, 1 next, 2 the one after. Not grams.
  ///
  /// THE DISTRICT'S RATE READ BACKWARDS, NOT A FORECAST. The position is
  /// yield × worked arable × area share × plan share, so the hectares that pay
  /// it at a normal yield are worked arable × area share × plan share. The
  /// alarm stands while the chains grow the crop on fewer hectares than that
  /// in the year — year 0 priced off last year's worked arable, years 1 and 2
  /// off the area next spring's figure is priced off (the district's ratchet,
  /// NextPlanAreaHa; off the arable under chains today until 0.36.39, boss-
  /// core-epoch1-queue [31]) — and goes out when they grow at least that.
  ///
  /// A LOST SLOT GROWS NOTHING (0.36.39; boss-core-epoch1-resume [98]): a
  /// winter crop of year 1 whose window closed this autumn unsown counts no
  /// hectares from that day (amount 1), and from the turn as year 0's
  /// (question 278, WinterSlotLost; amount 0); so does a spring crop of year
  /// 0 not in the ground — on an idle field from the day its window closed,
  /// on a field ploughed for it from the day it could no longer ripen before
  /// the snow. A chain standing before its first season is read by the year
  /// its first slot grows in (plan_alarms.cpp, CropInYear), not slot-for-year.
  /// Before, the rye lost to its window paid the position until the harvest
  /// that never came, and the alarm stood 0 times of 135 seed-years before
  /// the eight rye failures that followed a lost slot. How much this
  /// field's fertility will fall short is not in it: that is the accountant's
  /// forecast, Epoch I has no specialists (society design §1a), and fertility
  /// is visible on the ground. The same shape as the seed alarm the registry
  /// settled: "аларм сразу при назначении — закрыть до посевной, а не узнать
  /// весной".
  ///
  /// IT WAS A CHECK OF PRESENCE until the same day, and presence lied: on seed
  /// 1933 a 3.5 ha field closed a missing potato against 4.63 ha owed, the
  /// alarm went out, and the plan failed anyway.
  ///
  /// WHY IT EXISTS: measured on seeds 1929 and 1936, the start layout's chains
  /// leave one year in three without oats while the district asks for oats
  /// every year, and in thirteen such years not one alarm or light said so
  /// before the verdict. A failure no signal foretold is a trap; this makes
  /// it a decision — the chairman sees it on the first day and the rotation
  /// is his lever. It stands from the first day of a world whose inherited
  /// layout has the gap.
  kPlanPositionUncovered,

  // -- timber: core_production ---------------------------------------------------

  /// NOBODY CAN GET TO THE MARKED STAND AND BACK IN A DAY: the ride from the
  /// NEAREST lived-in house to a stand marked for felling, with work still to
  /// do, is one the accountant will not send the brigade down — longer than
  /// labor.csv travel_limit_hours, or leaving less than min_usable_hours of
  /// the daylight after the ride there and back. Subject: `stand`; `amount` =
  /// the hours of the ride, one way, in game hours.
  ///
  /// THE RIDE IS THE LOG CART'S since 0.36.29 (boss [69], «не рубить то,
  /// что не вывезти»): a third of the forest within the team's six hours was
  /// past the log cart's, and fellers were sent where no log could come out.
  /// Past travel_limit_hours by the log cart, the accountant offers no felling
  /// at all (labor_system.cpp) — this alarm is then the chairman's only word
  /// of why; a dirt road laid there brings the stand back by itself.
  ///
  /// THE FELLING BRIGADE RIDES (time design §7, timber design §8a; boss,
  /// 2026-09-14, parcel 308): it goes out on the carts that will cart the
  /// logs, so the ride is measured at harness speed for the whole brigade,
  /// as the meadow cut's is. On foot the reachable groves ran out by years
  /// 13-26 on all nine seeds — 3188 of 11 362 m3 — and a stand marked beyond
  /// the road limit stood marked for the rest of the run with not one feller
  /// and not one word of why (core, parcel 307).
  ///
  /// A WARNING AND NOT A REFUSAL AT THE MARK, exactly as kSiteUnreachable and
  /// for its reasons: the window is shorter in winter, so a stand out of reach
  /// in December is within it in June, and this goes out by itself when the
  /// day grows. Ranked 13 in the design's roster (alarms.csv).
  kFellingUnreachable,

  /// FOOD LOCKED IN THE FUNDS WHILE A FAMILY GOES HUNGRY (econ's audit I6,
  /// Л1; alarms.csv `reserve_full_nothing_to_eat`, rank 2, «Еда заперта в
  /// фондах»): some family stands at the ration's threshold, nothing of a
  /// ration position is free in the stores, and the sealed funds — the seed,
  /// the plan's reserve, the fodder — hold some. The door is the chairman's
  /// own: unseal a fund (kUnsealFund) or switch the ration. One per such
  /// position. Subject: `resource`; `amount` = the grams held in the funds.
  kReserveFullNothingToEat,

  /// THE SOWING WILL NOT FIT (farming design, «Поздний сев»; alarms.csv
  /// `sowing_will_not_fit`, «Сев не успеть»): a spring field has entered the
  /// plough, and the team cannot finish its ploughing and harrowing by the
  /// last day its crop can be sown and still ripen before the snow. Past that
  /// day the field is left unsown and its seed stays in the fund — so this is
  /// said while the plough can still be stopped: people and horses on the
  /// sowing, or the field under fallow. The echo of kHarvestWillNotFit, one
  /// about TIME, the other about PLACE: the days are spent field by field in
  /// the order the fields must be sown, and the field that finds them gone is
  /// named. Capacity: adult horses, one norm-day each a day (measured, tests/
  /// run/sowing_window). Subject: `field`; `resource` = its crop's produce;
  /// `amount` = the SQUARE METRES that will not be sown, not grams.
  kSowingWillNotFit,

  /// THE HARVEST WILL NOT BE GATHERED BEFORE THE SNOW (boss seq 78; alarms.csv
  /// `harvest_will_not_be_gathered`, «Не успеть убрать до снега»): an annual
  /// that will ripen, or has, cannot be reaped by the snow at the village's
  /// reaping pace, and the snow will take what is left standing. The answer
  /// is the chairman's — an аврал on the reaping (register 220), hands off
  /// other work. The fields spend the days in the order they ripen; the one
  /// that finds them gone is named. Subject: `field`; `resource` = its crop's
  /// produce; `amount` = GRAMS the snow will take: the WHOLE field's crop, as
  /// LoseFieldToSnow takes it — a field still being reaped is lost entire, not
  /// by its unreaped share (boss seq 176; it named the share until
  /// 2026-09-19, less than the snow takes).
  kHarvestWillNotBeGathered,

  /// A PLAN POSITION WILL FALL SHORT AT THE TURN (boss seq 89; alarms.csv
  /// `plan_position_short`, core's proposed key): on the year's last day, a
  /// position's delivered grams plus what the turn's delivery can take from
  /// the stores stand below the met share (PositionDelivered). The turn
  /// ships the debt by itself, so a position merely not yet shipped is not
  /// this alarm — only one the stores cannot make whole. Subject: `resource`;
  /// `amount` = GRAMS short of the met share.
  kPlanPositionShort,

  /// A SHOP STANDS AND SAYS WHY (production units §8а, «нет свободных бочек —
  /// квашня стоит и говорит почему»; boss seq 184; alarms.csv
  /// `processing_stopped`, core's proposed key): a sauerkraut shop, smokehouse
  /// or the workshops' cooperage has something to work — its main input lies
  /// in the stores — and stands for want of `resource`: barrels for what it
  /// makes, or a second input (grocery for the salt, firewood for the smoke,
  /// boards or steel for a barrel), or ROOM for what it makes — `resource`
  /// is then the output itself (boss, host-econ-shops seq 11: «простой
  /// законный — тревога с причиной»; until 0.34.8 a full store stood the
  /// shop in silence, kStoreFull naming the store and not the shop). The
  /// room is counted after the inputs are out: cabbage leaving a store is
  /// room for its sauerkraut. Or it stands with nobody to work it: every
  /// post holder of its parent lives beyond the accountant's road rule
  /// (since 0.34.9; boss seq 13, production units §8а «Мастер цеха и
  /// дорога»). Nothing to work is no alarm. Subject: `unit`; `stop_reason`
  /// says which of the three (ProcessingStopReason); `resource` = what is
  /// missing, invalid for kTooFar; `amount` = 0, or the road for kTooFar.
  kProcessingStopped,

  /// A UNIT BEING TAKEN DOWN STILL HOLDS STOCK THE STORES HAVE NO ROOM FOR
  /// (boss, host-econ-shops seq 23; unit_state.h, kDemolishing): the
  /// evening's delivery through the stores' door left it on the site — no
  /// store that keeps it has room, or none keeps it at all. Nothing is lost:
  /// it waits there, and goes the evening room appears. Subject: `unit` (the
  /// site); `resource` = the resource most grams of which wait; `amount` =
  /// GRAMS of everything waiting on the site.
  kDemolitionStockWaiting,

  /// THE PIG SLAUGHTER WAITS: an October day on which a kolkhoz pig herd's
  /// slaughter is due and the stores have no room for its meat, so it does
  /// not happen today (HerdRow::autumn_slaughter_done; boss, host-econ-shops
  /// seq 26 on econ seq 25). The month's last day slaughters whatever the
  /// room. Subject: `herd`; `resource` = the meat; `amount` = GRAMS of meat
  /// the stores lack room for.
  kSlaughterWaitsForRoom,

  /// A PLANTING ZONE NOBODY CAN WALK TO (alarms.csv `planting_unreachable`;
  /// boss seq 21): a zone ordered by kPlantForest and not yet planted whose
  /// walk from the nearest lived-in house is past the accountant's road rule
  /// — longer than travel_limit_hours, or leaving less than min_usable_hours
  /// of the daylight after the walk there and back. The planters go on foot
  /// (labor_state.h, RidesOut), so the test is kFellingUnreachable's at
  /// walking speed. A warning and not a refusal at the order, for the same
  /// reason: a zone out of reach in December is within it in June. Subject:
  /// `stand`; `amount` = the hours of the walk, one way, in game hours.
  kPlantingUnreachable,

  /// SEED WITH NOWHERE TO LIE (alarms.csv `seed_has_no_room`, words STUB;
  /// boss, boss-core-epoch1-4 seq 34): the room a crop's missing seed needs
  /// is booked while its harvest is out (seed_room.h), and today the booking
  /// is holding another crop's heap on its field — the stores cannot take
  /// both, and the seed goes in first. The player's answer is a store. Without
  /// this line the booking would take his grain in silence. Subject:
  /// `resource` = the seed; `amount` = GRAMS of room booked for it.
  kSeedHasNoRoom,

  /// ALL THE MILK WENT TO THE DEBT (alarms.csv `milk_all_to_debt`, words
  /// STUB; boss, boss-core-epoch1-5 seq 7): the milk position's debt stands
  /// (PlanState::milk_debt > 0), so the cart took every litre at the last
  /// milking and the yards' morning issue had no milk. It happens when the
  /// herd gives less than the share — a cull, a disease, a sale. The design
  /// orders the debt paid before the issue (district §9), and this line is
  /// what lets the player see that fork. Subject: `resource` = the milk;
  /// `amount` = GRAMS of the debt.
  kMilkAllToDebt,

  /// THE DISTRICT'S GOODS LOAN IS OWED (alarms.csv `goods_loan_owed`, rank
  /// 24; boss, boss-core-epoch1-5 seq 15 and 30): PlanState::goods_loan_owed
  /// stands above nought for the resource. It is paid at the year's turn after
  /// the plan, and what the turn cannot pay carries on with the markup again,
  /// so this line is how the player sees a debt that grows. Subject:
  /// `resource`; `amount` = GRAMS owed, the markup included.
  kGoodsLoanOwed,

  /// A WINTER CROP THE CHAIN CANNOT SOW (alarms.csv `winter_crop_unsowable`,
  /// rank 25; fields design §7, the mark before the loss; boss, boss-core-
  /// epoch1-resume [34], [47] form (a)): the field's chain puts a winter crop
  /// right after a crop whose reaping opens no earlier than the last month
  /// of the winter crop's sowing — potato, cabbage, buckwheat or maize before
  /// rye on the shipped tables — so the plough cannot get in and the slot
  /// will be lost (question 278). The pair of kPlanPositionUncovered: said
  /// when the chain is laid, one to three years before the loss. Subject:
  /// `field`; `amount` = the YEAR the winter crop is lost in, counted from the
  /// current one: 1 or 2 inside the chain, 3 across its joint — the last slot
  /// followed by the first of the next round (0.36.24; boss-core-epoch1-
  /// resume [60]). 0 never: this year's winter crop was sown last autumn or
  /// is already lost, question 278's event. THE CRITERION IS core's STUB,
  /// named: the reaping's first month against the sowing's last, no days.
  kWinterCropUnsowable,

  /// THE TEAM HAS BEGUN TO AGE (seam key `herd_aging`, its alarms.csv row
  /// boss's export with this kind; livestock design,
  /// «Табун без конюшни кончается»: «первая состарившаяся лошадь поднимает
  /// подсказку»; boss, boss-core-epoch1-resume [79]): the oldest head of the
  /// kolkhoz's horses has reached the lower end of the horse's lifespan band
  /// (livestock.csv life_game_years_min) and the stable is not built. A NEW
  /// LINE and not kHerdWithoutStable louder: that one burns from day 0, and
  /// a signal built constant is scenery (boss's word on econ's measure). The
  /// start's team is drawn short of old age (genesis, 0.35.10), so this
  /// lights on the day the first head crosses it — about year two on the
  /// shipped tables, an estimate. Subject: `herd`, the row holding the
  /// oldest head; `amount` = the heads still alive in the team. A STATE: it
  /// goes out with the stable, and also for as long as every head past old
  /// age has died and the next has not yet reached it. LIMIT, the band's:
  /// its top is exact until the first cut, and a cut takes the oldest k of
  /// n as if the ages were even over the band (herd_age_band.h), so a row
  /// with young heads bought into it keeps an "old" top after its old horse
  /// died — the line stays lit, and the age death reads the same top.
  kHerdAging,

  /// «УПРЯЖЬ НА СЕНЕ» (0.37.8, save 109; boss-core-epoch1-queue [84], [90];
  /// econ canon-horses-oats.md §3): the team's work ration has been short of
  /// full for `kTeamOnHayDays` working days in a row — the horses pull on
  /// hay, and the ploughing goes slower (WorldState::traction_ration). Goes
  /// out on the first working day of the full ration, or when the team is
  /// gone; a day nobody works keeps it standing. On a carting day the
  /// plough's oats are held by rule (FeedAllowance, PloughFeedHold), so it
  /// can stand beside oats in the barn that are the plough's. OATS, NOT HAY: a team
  /// short of hay is kHerdStarving. Subject: `herd`, the first row of the
  /// kolkhoz's horses; `resource` the horse's first work-only feed (oats);
  /// `amount` the grams of it the streak lacked (TractionWatch).
  kTeamOnHay,

  /// «ЛОШАДЕЙ НЕ ХВАТАЕТ» (0.37.8, save 109; boss [67], [90]; econ
  /// canon-horses-oats.md §4): over the last seven days more than
  /// `kTooFewHorsesShare` of the harnessed assignment-days had no horse —
  /// carters on foot, the village carrying on its backs. Too few horses for
  /// the work, where kTeamOnHay is too little oats for the horses. Subject:
  /// `herd`, the first row of the kolkhoz's horses, INVALID when not a head
  /// is left (the case it exists for; a row of foals alone still names it); `amount` the harnessed
  /// assignments without a horse on a mean working day of the week, rounded — the teams short.
  ///
  /// AND IT SEES THE HAY (0.37.48; boss-core-epoch1-resume-2026-09-30 [49]
  /// p. 1; econ's novice-0.37.42 audit): the advice «buy a horse» was taken
  /// at its word — 22 -> 34 -> 49 horses by year 3 against a hay ceiling of
  /// 44-46, and the herds starved in year 4. When the farm's hay would not
  /// feed one more horse through a year — last year's cut or what lies now,
  /// the larger, against the kolkhoz herds' stall-season need with the new
  /// head — the advice turns: `resource` = the hay, `lamp` = 0, the line
  /// «сена на ещё одну лошадь не хватит — сначала луг и покос» (rpg's key,
  /// through boss). With hay enough: `resource` invalid and the lamp lit, as
  /// before. The subject is the herd in both.
  /// THE FORECAST'S SECOND READER SINCE 0.37.57 (boss [72] p. 2, [85], [86]):
  /// «the hay would not feed one more horse» is asked of kHerdHayShortAhead's
  /// own forecast with one adult more in the team — every kolkhoz herd, their
  /// offspring, to the cut of the next year — and not of a stall season's
  /// need at today's heads: that one saw no calves, and the novice's cows
  /// starved 31 -> 164 beside the horses it bought (host [13]). Short with
  /// the head: the advice turns as above; `resource` is the feed that runs
  /// out first.
  kTooFewHorses,

  /// «ТРАВА НА КОРНЮ — НЕ ЗИМНИЙ ЗАПАС» — THE ELDER'S ADVICE, NOT A LAMP
  /// (boss-core-epoch1-queue-2026-09-29 [25]; rpg carries the line: «Трава на
  /// корню — не зимний запас. Скосите до снега: сено и в кучах сохранится»):
  /// a month before the first snow by the climate, a share of the farm's
  /// meadow grass is still standing unmown. Hay lying in heaps is a store
  /// (0.37.11) and is not counted. Always `lamp = 0`: one line a season for
  /// the farm, and the "once" is the layer's — the core raises it for as long
  /// as the condition holds. Subject: `resource`, the hay (the registry's
  /// word — it has none for "the farm"; 0.37.21); `amount` = the unmown
  /// meadow, whole hectares. STUB: the share (30 %) and the date (the first
  /// snow's day — the day after the early edge read off the climate by
  /// farming.csv `early_snow_share` — and not a steady cover's) — core's
  /// numbers, measured by the alarm-days instrument.
  kMeadowUncutBeforeSnow,

  /// «ОКНО СЕВА ЗАКРЫВАЕТСЯ» — THE ELDER'S ADVICE, NOT A LAMP (boss-core-
  /// elder-facts-2026-09-29 [1]-[3]; fact `elder_warn_sowing_window`, rpg's
  /// line): a crop the chain names for this field's next sowing has its
  /// sowing window near its end and the field's work has not begun — the
  /// field idle, no plough, harrow or drill opened on it. Always `lamp = 0`;
  /// the "once" is the layer's. Subject: `field`; `resource` the crop's
  /// produce; `amount` = the whole days left to the window's end. STUB: how
  /// near is "near" — core's number (0.37.25). Beside kSowingWillNotFit, which
  /// speaks of a field whose preparation is under way and will not finish.
  kSowingWindowClosing,

  /// «НЕ ХВАТИТ НА N ГА — ЗАСЕЕМ МЕНЬШЕ» — A LINE OF THE BOOK, NOT A LAMP
  /// (boss-core-epoch1-resume-2026-09-30 [39], [40], [46]; alarms.csv
  /// `seed_area_short`, rank 31; rpg's `book.seed_area_short` (crop,
  /// hectares)): the seed of a crop is short for its sowing, this year's
  /// goods loan of it is taken and its cart has come — the district lends no
  /// second one, and the part of the field the seed does not cover stays
  /// unsown. The outcome, with no move left: while this stands kSeedShort's
  /// lamp is 0 (the loan was the lamp's move). Always `lamp = 0`. Subject:
  /// `resource` = the seed; `amount` = the SQUARE METRES its shortfall leaves
  /// unsown at the crop's sowing norm, not grams (as kSowingWillNotFit).
  kSeedAreaShort,

  /// «К УКОСУ СЕНА НЕ ХВАТИТ» — THE HERDS' YELLOW STAGE, A FORECAST AND NOT A
  /// FACT (boss-core-epoch1-resume-2026-09-30 [49] p. 2, [68], [70], [85],
  /// [86]; econ horse-spring-starvation §4, §4а; alarms.csv
  /// `herd_hay_short_ahead`, rank 32). The kolkhoz herds' fodder, drained
  /// day by day by the herd feeding's own order and ceilings, runs short
  /// before the NEAREST first scythes — this year's until its mowing begins,
  /// next year's from then on (0.37.95; boss, econ-boss-hay-term-2026-10-01
  /// [7]). Until then it looked from any day to the cut of the NEXT year: on
  /// 1 January a year and a half and two calvings on, and the canon's bot
  /// shed twenty cows a village on a shortage two haymakings away. PAST THE
  /// NEAREST SCYTHES THE LAMP IS SILENT. What it counts: the stores the herds
  /// may eat, the hay
  /// in the fields' heaps, the expected cut on its day (last year's, or in
  /// year 1 the meadows' ceiling × the mown share), the moves already made
  /// (feed lots on the road, a granary under construction, the night pasture
  /// ordered), and the need growing with the spring's offspring by the
  /// births' own rule (a billeted or hungry herd has none). Lights in spring
  /// as soon as the offspring are in the forecast; goes out as soon as the
  /// forecast is short no more — nothing is remembered.
  /// NOT kHerdStarving's lamp 0: kHerdStarving is the herd underfed NOW, a
  /// loss with the lamp lit; this is the window before it, and its `lamp`
  /// is always 0 — two positions, two kinds (boss [86] p. 1).
  /// Subject: `herd`, the first kolkhoz herd the forecast underfeeds;
  /// `resource` the feed that runs out FIRST (usually the hay — boss [86]
  /// p. 3); `amount` the HEADS the forecast cannot feed on its worst day,
  /// never more than the heads standing today (0.37.95);
  /// `days_ahead` the whole days to its first short day; `advice` the first
  /// move (AlarmAdvice).
  /// THE LADDER OF ITS ADVICE SINCE 0.37.121 (herd_forecast.h,
  /// AdviseOnShortFodder), the first that holds: kCutHay while a meadow
  /// stands in its cut; kBuyFeed with `advice_resource`, `advice_amount` when
  /// the lot's door would take the order today AND the purchase feeds a head
  /// more; kGranaryForFeed when a feed lot is refused only for want of a
  /// store; kReduceHerd. Behind the purchase and the granary `advice_more` =
  /// kReduceHerd names the heads still short in `amount_more`. FOR WHATEVER
  /// FEED RUNS OUT FIRST: until then a first short feed that was not the hay
  /// (straw, silage) had no advice, and the lamp burned 23 days of host's
  /// year 4 saying «trouble» and no move.
  kHerdHayShortAhead,

  /// «СТАДО МЁРЗНЕТ» — THE COLD'S RED (Livestock design, «Замерзание —
  /// метрика скота», «Числа лестницы — Эпоха I» — in force by boss-core-
  /// start-no-yards [15]; alarms.csv `herd_freezing`, rank 33): a kolkhoz
  /// herd whose cold nights' counter (HerdRow::cold_nights) stands at 1 or
  /// more — heads in a COLD place (a unit of a rung whose unit_levels.csv
  /// `warm_place` is 0 and that is not insulated: the cattle yard's open pen,
  /// the horse yard below insulation) on nights below the kind's threshold,
  /// IN THE CALENDAR'S WINTER ONLY (`livestock_cold_first_month`..
  /// `livestock_cold_last_month`, December to February; 0.37.69): out of it
  /// no night counts and the lamp cannot light.
  /// «Мёрзнет» from the first such night (milk × the kind's
  /// `freezing_produce_factor`, draught × `freezing_draught_factor` the day
  /// after); «замерзает» from `livestock_freezing_counter` (a share of the
  /// adults a day). The heads on billet stand outside the metric; the billet
  /// is what a roof has no room for, in every month (from 0.37.62 to 0.37.68
  /// it kept its places first in a frost month). Subject: `herd`;
  /// `amount` the heads in the cold. `advice` kInsulateStraw when the stores
  /// hold the straw of an insulation (construction.csv
  /// `insulation_livestock_straw_t`), kNone otherwise — the billet and the
  /// knife are the lamp's text, not a move (boss, 2026-10-01). `lamp` 1: a
  /// loss the player has a move against.
  kHerdFreezing,

  /// «К ЗИМЕ ПЛОЩАДКА НЕ УКРОЕТ N ГОЛОВ — ХЛЕВ ДО МОРОЗОВ» — THE COLD'S
  /// YELLOW, A FORECAST (the same design and threads; alarms.csv
  /// `herd_cold_ahead`, rank 34): in the autumn, the kolkhoz heads that
  /// will stand under a COLD roof when the cold's season opens — a herd up
  /// to its roof's room (what the roof does not hold stands billeted, warm).
  /// Subject: `herd`; `amount` those heads; `days_ahead` the days to the
  /// first day of `livestock_cold_first_month` — one date for the player,
  /// the first of December (0.37.69; it was the first cold night by the
  /// climate); `advice` kWarmYard — the cattle yard's warm barn, laid before
  /// the clay freezes (boss, 2026-10-01) — when the unit's next rung is
  /// warm, kInsulateStraw otherwise. `lamp` always 0.
  kHerdColdAhead,

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

/// @brief Why a kProcessingStopped shop stands — the seam's vocabulary
/// `processing_stop_reason`, its words the enumerators' snake_case (boss,
/// host-econ-shops seq 13: «причина „далеко“, слово выбери сам»). Appended,
/// never renumbered.
enum class ProcessingStopReason : std::uint8_t {
  kNone = 0,  ///< Not a kProcessingStopped alarm.
  kShortOf,   ///< `resource` is missing: a barrel, or a second input.
  kNoRoom,    ///< `resource` is the shop's output, and the stores have no room for it.
  /// Every post holder of the shop's parent lives beyond the accountant's
  /// road rule for today (geometry.h, RoadLeavesAWorkingDay): he does not
  /// set out. `amount` = the nearest holder's road one way, whole game hours
  /// rounded up; `resource` invalid.
  kTooFar,

  /// NOT A REASON: the count, so a consumer can static_assert its mirror.
  kProcessingStopReasonCount,
};

/// @brief The first move a forecast alarm names (boss-core-epoch1-resume-
/// 2026-09-30 [85], [86]) — the seam's vocabulary `alarm_advice`, its words
/// the enumerators' snake_case; the layer shows the move by the key. Only
/// the forecast kinds and the cold's red set it (kHerdHayShortAhead,
/// kHerdColdAhead, kHerdFreezing); every other alarm keeps
/// kNone. Appended, never renumbered.
enum class AlarmAdvice : std::uint8_t {
  /// No move the core can name — shown as such: a feed runs out first that
  /// no move of the player's brings (straw, silage).
  kNone = 0,

  /// The hay runs out first WHILE A MEADOW STANDS IN ITS CUT: the cut — more
  /// mowers, a meadow mown. Since 0.37.92 not named once the haymaking is
  /// over: a meadow marked then gives its hay next summer (kBuyFeed,
  /// kReduceHerd).
  kCutHay,

  /// Grain or compound feed runs out first, and the farm has no granary or
  /// no room left in one for feed: compound feed lies only in a granary, and
  /// a lot bought without one is refused at the door (kNowhereToStore; boss-
  /// host-horses-hay-03749 [10]) — «амбар под комбикорм». With a granary
  /// and room, the move is the district's lot of that `resource`, and it
  /// needs no word of its own.
  /// FOR THE HAY'S LAMP SINCE 0.37.121 ALSO WHILE THAT GRANARY IS BEING
  /// BUILT: the lot's door refuses until it stands (kNowhereToStore), so the
  /// move of that day is «finish the granary», not «buy»; `advice_more` =
  /// kReduceHerd with the heads short today in `amount_more`.
  kGranaryForFeed,

  /// The cattle yard's warm barn (rung 2) before the frosts — «утеплить
  /// скотный двор» (boss-core-start-no-yards [8]; the dictionary's
  /// `warm_yard`). Named by kHerdColdAhead.
  kWarmYard,

  /// Straw on the walls of the cold place the herd stands in — «утеплить
  /// площадку соломой», kInsulateUnit at the price of construction.csv
  /// `insulation_livestock_straw_t` and `_labor_days` (boss-core-start-no-
  /// yards, 2026-10-01; the dictionary's `insulate_straw`). Named by
  /// kHerdFreezing, and only while the stores hold that straw: the move a
  /// winter leaves when the clay of the warm barn is frozen.
  kInsulateStraw,

  /// The hay runs out first and NO MEADOW STANDS IN ITS CUT (the haymaking
  /// is over, or has not begun — since 0.37.137 also a meadow left in its
  /// cut's phase after the mowing's months: kCutHay is named inside them
  /// only): the district's lot of a feed the herds eat
  /// — «купить корм по лимиту» (boss, econ-boss-hay-term-2026-10-01 [2];
  /// 0.37.92; the dictionary's `buy_feed`).
  ///
  /// NAMED ONLY WHILE THE LOT'S DOOR WOULD TAKE THE ORDER TODAY (0.37.121;
  /// boss, host-boss-pin-0-37-109-2026-10-02 [11]: «совет не называет ход,
  /// которому дверь сегодня откажет»): the catalogue sells a feed lot open
  /// in this epoch, some store accepts every good of it, and the year's
  /// points cover it — the door's own question (district_limit.h,
  /// LimitLotRefusalToday). Until then it was named «while a store would
  /// take it today OR a site of its home is under way», and the door refused
  /// while that site stood unbuilt: 269 refusals of 497 advices followed on
  /// host's 27 villages. The alarm's `advice_resource` and `advice_amount`
  /// say WHAT AND HOW MUCH the year's points buy today (grams of the feed);
  /// `advice_more` = kReduceHerd with `amount_more` heads when the forecast
  /// is still short after that purchase.
  kBuyFeed,

  /// FEWER HEADS — «сократить стадо» (the dictionary's `reduce_herd`): the
  /// heads the fodder does not reach, to hand over or slaughter. The chairman
  /// decides it and the game does not do it for him (the human, 2 October
  /// 2026: «игра не должна сама убивать стадо»), so the game owes him the
  /// number. SINCE 0.37.142 THE RUNG BEFORE THE FEED (Alarm::hand_over_stock):
  /// as `advice` when the heads above the floors feed the rest, and when no
  /// purchase closes what is left — the district sells no feed, the points
  /// are spent, or what they buy leaves the herds short (a feed's share of
  /// the ration is capped, feed_links.csv max_share); as `advice_more`
  /// behind kBuyFeed or kGranaryForFeed with the heads above the floors in
  /// `amount_more`. Until 0.37.142 it stood behind the feed, with the heads
  /// left AFTER the purchase.
  /// THE NUMBER IS `hand_over_stock` AND `hand_over_horses` SINCE 0.37.137 —
  /// the least heads whose leaving feeds the rest, by the floors since
  /// 0.37.142 — and never the lamp's `amount`, which counts the heads unfed
  /// on the worst day (Alarm::hand_over_stock).
  ///
  /// UNTIL 0.37.121 IT COULD NOT BE NAMED AT ALL in a world with a feed lot in
  /// the catalogue: it stood behind «the catalogue sells no feed», a
  /// condition on the table and not on the world, and on host's 27 villages
  /// it sounded on no day while the herds starved.
  kReduceHerd,

  /// NOT A MOVE: the count, so a consumer can static_assert its mirror.
  kAlarmAdviceCount,
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

  /// For kinds that are about a timber stand (kFellingUnreachable): which
  /// one. Invalid otherwise.
  TimberStandId stand;

  /// For kinds that are about a resource: which one. Invalid otherwise.
  ResourceId resource;

  /// Grams, heads — the kind says which. 0 when the kind has no number.
  std::int64_t amount = 0;

  /// kProcessingStopped only: why the shop stands. kNone for every other
  /// kind.
  ProcessingStopReason stop_reason = ProcessingStopReason::kNone;

  /// THE PLAYER'S RED LAMP, 0 or 1 (boss-core-epoch1-queue-2026-09-29 [23]–
  /// [25], [35]–[36]; econ's complexity review, option «а»): 1 when the
  /// condition is a loss that comes without the player's move AND he has a
  /// move against it; 0 when it is a line for a window, the elder's advice,
  /// or the farm's own rule at work. The layer lights red for `lamp = 1`
  /// only and shows the rest where it belongs.
  ///
  /// THE CONDITION STAYS IN THE LIST WHOLE, with the same `amount`, whatever
  /// the lamp: the list has other readers — the canon chairman sizes his seed
  /// loan by kSeedShort's amounts (tests/run/common/limit_policy.h), host's
  /// probes and the core's own guards read the kinds — and a condition
  /// silenced to dim a lamp would have moved them (core [35]: two doors to
  /// one action). Each kind's rule is where its predicate raises it (0.37.20:
  /// kSeedShort, kHerdWithoutStable, kTeamOnHay, kStoreFull, kGoodsLoanOwed —
  /// core_production; kFamilyGoingHungry — core_residents; kSiteWithoutCrew —
  /// core_construction; kMeadowUncutBeforeSnow always 0); a kind that names
  /// none keeps the default, 1 — kTooFewHorses among them, by rule: a team
  /// short for the work is a loss the player has a move against.
  /// @note Not in the save: the list is collected afresh every step.
  std::uint8_t lamp = 1;

  /// kGoodsLoanOwed only: the campaign year (counted from 1, as
  /// CalendarState::date.year) whose harvest the debt is repaid from at the
  /// coming turn — the book's line «погашение с наценкой — из урожая {year}
  /// года» (rpg's `book.goods_loan_carried`; boss-core-epoch1-resume-2026-09-30
  /// [66]). The core's and not the layer's: a loan on the turn's own day, or
  /// a repayment the order of §6.1 puts off, part from «this year + 1», and a
  /// layer that counted it would be the rule's second home. 0 for every
  /// other kind (0.37.50).
  /// @note Not in the save, as `lamp`.
  std::uint16_t repay_harvest_year = 0;

  /// The forecast kinds only (kHerdHayShortAhead, kHerdColdAhead): whole game days from
  /// today to the forecast's first short day — «к весне», counted. 0 for
  /// every other kind (0.37.56).
  /// @note Not in the save, as `lamp`.
  std::uint16_t days_ahead = 0;

  /// The forecast kinds and kHerdFreezing: the first move the core names
  /// (AlarmAdvice). kNone for every other kind, for a forecast whose first
  /// short feed no move brings (0.37.56), and for a freezing herd when the
  /// stores hold no straw for an insulation (0.37.60).
  /// @note Not in the save, as `lamp`.
  AlarmAdvice advice = AlarmAdvice::kNone;

  /// kHerdHayShortAhead with `advice` kBuyFeed (0.37.121): the feed the
  /// year's points buy today and how much of it, in grams — the largest
  /// purchase the lot's door would take this morning (the feed lots of the
  /// catalogue open in this epoch that some store accepts, as many as the
  /// points cover, the best feed units a point first). Invalid and 0 for
  /// every other advice and kind.
  /// @note Not in the save, as `lamp`.
  ResourceId advice_resource;
  std::int64_t advice_amount = 0;

  /// kHerdHayShortAhead: THE SECOND MOVE, named with the first (0.37.121;
  /// boss, host-boss-pin-0-37-109-2026-10-02 [12] p. 1: «совет называет оба
  /// числа разом») — kReduceHerd with `amount_more` the heads the fodder
  /// still does not reach AFTER the first move (after the purchase of
  /// `advice_amount` landed on its day; behind kGranaryForFeed, with nothing
  /// bought). kNone and 0 when the first move closes the hole, when the
  /// first move is itself kReduceHerd (its heads are `amount`), and for
  /// every other kind.
  /// @note Not in the save, as `lamp`.
  AlarmAdvice advice_more = AlarmAdvice::kNone;
  std::int64_t amount_more = 0;

  /// kHerdHayShortAhead with kReduceHerd named — as `advice` or as
  /// `advice_more`: THE HEADS TO HAND OVER, AND THEY ARE NOT `amount`
  /// (0.37.137). `amount` is the lamp's own number — the heads the fodder
  /// does not reach on its WORST day, which on the day the hay is out is the
  /// whole herd. These are the LEAST heads whose leaving feeds the rest to
  /// the forecast's horizon, in the design's order (Livestock design §6):
  /// the productive stock before the draught — a kind's adults, the old end
  /// first as the hand-over's door takes them (kHandStock), then its young —
  /// and a working horse only when the whole of the stock does not close the
  /// shortage. `hand_over_stock` heads of the kinds that are not the horse,
  /// `hand_over_horses` of the horse; `amount_more`, behind another move, is
  /// their sum. THE DOOR NAMES A HERD AND TAKES ITS OLDEST: which herds the
  /// heads come from is the chairman's — these say how many of which.
  ///
  /// Until 0.37.137 the advice was `amount` itself: on host's 27 villages of
  /// 0.37.133 a chairman following it handed over the whole herd in every
  /// one — 111 heads by the median for a shortage of a tenth of the hay.
  /// 0 for every other advice and kind.
  ///
  /// THE ORDER IS BY FLOORS SINCE 0.37.140, and «the stock before the
  /// draught» above is WITHDRAWN (Livestock design §6 as boss rewrote it;
  /// boss-all-carts-carry-people-go-2026-10-02 [114], [121]). On host's 27
  /// villages of 0.37.138 the team bred 22 -> 60 in four years while the
  /// advice closed every year's hay with cows: three cows left by the median,
  /// the plan failed in twenty villages — a chairman who read the lamp ended
  /// worse than one who did not. The heads are named in this order, each
  /// step only as far as the shortage asks:
  ///  1. the adult horses above the ploughing's floor — the teams that
  ///     plough the plan's base of worked hectares (PlanState::
  ///     worked_ha_last_year) in farming.csv `plough_window_days`, times
  ///     farming.csv `plough_floor_margin` — in `hand_over_horses`;
  ///  2. the adult cows above the milk plan's floor — the highest milk
  ///     position ever named (PlanState::highest_due) over a cow's yield of
  ///     the closed year, times farming.csv `milk_floor_margin` — and every
  ///     head of the other productive kinds — in `hand_over_stock`.
  /// NEVER NOUGHT AND NOUGHT UNDER kReduceHerd (0.37.143): with nobody above
  /// the floors the move is not named — `advice` is kNone (or the feed's, or
  /// the granary's, with no `advice_more`), and the remainder stands in
  /// `below_floor_stock`, `below_floor_horses`. A layer shows «below the
  /// floors: N» under «no move».
  /// THESE HEADS COME BEFORE THE DISTRICT'S FEED (0.37.142; boss [138]): the
  /// hay lamp's ladder is the cut — the heads above the floors — the feed —
  /// the remainder below the floors. `advice` = kBuyFeed is named only with
  /// the floors exhausted, its `advice_amount` the LEAST purchase that closes
  /// the shortage with every head above the floors gone (they are in these
  /// two numbers beside it, `advice_more` = kReduceHerd); a purchase after
  /// which the herds are still short is not named at all.
  /// TWO STEPS SINCE 0.37.141, three in 0.37.140's contract: its first, «the
  /// horses above the harness peak of the year gone», was withdrawn before
  /// its body — every measure of the horses' occupancy followed the herd up
  /// (boss [128], [131]; herd_state.h, TractionWatch). The ploughing's floor
  /// does not: 13 teams, 18 kept, on the plan's 70 ha in every year of nine
  /// villages — none named of year 1's sixteen, six of year 3's twenty-four.
  /// THE FLOORS EXHAUSTED AND THE FODDER STILL SHORT: what is still to go is
  /// named APART, in `below_floor_stock` and `below_floor_horses` — the
  /// least heads below the floors, cows first — and is NOT in the two
  /// numbers above nor in `amount_more`. A chairman is told what the floors
  /// cost before he goes under them; the game does not go under them for
  /// him.
  /// @note Not in the save, as `lamp`.
  std::int64_t hand_over_stock = 0;
  std::int64_t hand_over_horses = 0;
  std::int64_t below_floor_stock = 0;
  std::int64_t below_floor_horses = 0;
};

/// @brief The subject id of an alarm as one number, for ordering: the id
/// its kind names (AlarmKind), read from whichever field that is. Two
/// alarms of one kind sort by this; kinds sort by their value. 0 for
/// kNone. Implemented in core_common (alarm_state.cpp).
std::uint32_t AlarmSubjectValue(const Alarm& alarm);

}  // namespace core

#endif  // CORE_COMMON_ALARM_STATE_H_
