/// @file
/// @brief The accountant's placement algorithm — the internal seam of
/// core_labor between the day model (labor_system.cpp) and the placement
/// itself (assignment.cpp).
/// @threading SINGLE_THREADED
/// Pure functions of their inputs, called from the labor sub-step of the
/// decisions slot on the sim thread. No world state, no tables, no RNG:
/// determinism is total ordering over the inputs, and unit tests feed the
/// structs directly.
///
/// Design source: society design §1 (what the accountant weighs and how his
/// level degrades the placement), time design §7 (the road limit), the boss
/// digest 2026-08-29 §2.4 (surplus workers idle and earn nothing). The
/// job-priority order and the crew caps are the core's own decisions,
/// manual/65-labor-model.md §4.

#ifndef CORE_LABOR_ASSIGNMENT_H_
#define CORE_LABOR_ASSIGNMENT_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "core_common/deadline.h"
#include "core_common/geometry.h"
#include "core_common/labor_state.h"
#include "core_common/quantities.h"

namespace core {

/// @brief One job opening of the day, in placement terms. The caller
/// (labor_system.cpp) builds these from fields and herds; the algorithm
/// never learns where they came from beyond the target ids it copies into
/// assignments.
struct AssignmentJob {
  WorkKind kind = WorkKind::kNone;

  FieldId field;  ///< Valid for field kinds; copied into WorkAssignment.

  HerdId herd;  ///< Valid for kHerdCare; copied into WorkAssignment.

  UnitId unit;  ///< Valid for kConstruction: the site. Copied into WorkAssignment.

  /// Valid for kFelling, and for kHauling of logs lying on a stand. Copied
  /// into WorkAssignment.
  TimberStandId stand;

  /// Valid for kExtraction, and for kHauling of a load lying on an extraction
  /// site. Copied into WorkAssignment.
  ExtractionSiteId extraction_site;

  /// Valid for kHauling of a timber lot at the district centre (0.36.17).
  /// Copied into WorkAssignment. Such a job is never done on foot: with no
  /// horse left in the day's pool nobody is placed on it (0.36.18,
  /// StopsWithoutHorse in assignment.cpp).
  LimitDeliveryId limit_delivery;

  /// Valid for kRoadWork: the piece of road under work (delivery 7e).
  /// Copied into WorkAssignment.
  RoadWorkId road_work;

  /// At most this many workers on this job at once; 0 = no cap beyond the
  /// demand ceiling below. Construction sites carry one — the build
  /// class's brigade (unit_levels.csv max_crew): without it a 250-day site
  /// takes every free hand in the village and "a couple of weeks for a
  /// brigade" becomes three days (construction design §8).

  Vec2 position;  ///< Where the work is; drives travel time.

  /// Game man-days of demand left (the seam value at morning). Caps the
  /// useful crew: nobody is placed beyond what the day can consume.
  float work_days_remaining = 0.0F;

  std::uint8_t max_crew = 0;

  /// True when this job goes out WITH A HORSE although its kind is not one
  /// of the two horse works: the meadow cut, mown and raked with the horse
  /// implements the district issued at the start (livestock design §5,
  /// farming design §5 — "scythes, sickles, HORSE MOWERS"). It changes two
  /// things and only two: the shoulder is measured at harness speed (time
  /// design §7 — "whoever rides out with a horse reaches farther than the
  /// man on foot"), and each worker placed takes one horse from the day's
  /// pool, like ploughing.
  bool harnessed = false;

  /// True for a cart load — a field's heap, a pit's dig, a store's transfer —
  /// whose riders are placed already and which is offered again for carriers
  /// ON FOOT alone: the hour-1 top-up lets the morning's walkers go with the
  /// windowless work (they are the last of the queue) and gives the heap back
  /// to them through this flag, so that it takes no second crew of horses
  /// (0.37.105; AssignmentParams::walker_share_of_cart_day).
  bool on_foot_only = false;

  /// THE BRIGADE GOES OUT ON ONE CART (farming design §6, «Дорога пешком
  /// съедает световой день»; the human's word of 2026-10-01 on econ's third
  /// proposal; boss-core-fields-daylight-decided-2026-10-01 [1], [4]): set
  /// for the reaping of the arable and for the sowing. When the queue reaches
  /// the job and a horse is left in the day's pool, the placement takes ONE
  /// for it — the cart that carries the brigade out and back — and every hand
  /// placed rides: the harness pace decides who may go and what his day is
  /// worth. With the pool dry they walk, as they all did until 0.37.89. The
  /// first hand placed holds the horse (his rides_horse); the others ride
  /// with him (work_seam.h, WorkRidesOut). The cart does not haul that day: a
  /// horse is counted once a day.
  ///
  /// THE CARTER'S PATTERN, AND SINCE 0.37.168 THE MOWER'S AND THE FELLER'S
  /// TOO (routing stage A, A4): until then a meadow's brigade rode whether or
  /// not it got its horse, and the fellers rode without taking one at all
  /// (RidesOut) — two other answers to one question. Now a meadow's mower is
  /// taken with its first hand like this cart, and the fellers ride the
  /// people's cart when the placement gives them one (PlanDayAssignments).
  bool brigade_cart = false;

  /// The brigade's cart is out already — a driver stands on this field from
  /// an earlier placement of the day (the morning's, when the top-up asks):
  /// the hands placed now ride with him and no horse is taken.
  bool cart_out = false;

  /// True for the ploughing and harrowing of a FALLOW whose next crop is a
  /// winter one: the ground is prepared for a sowing this same autumn, so the
  /// job has that sowing's window — but it ranks BELOW every job with a
  /// window of its own, open or overdue, and below the meadow cut in its
  /// window, and above work with none (boss, parcels 233 and 262: "хлеб в
  /// поле старше будущего посева"). With the window alone it outranked the
  /// potato harvest and sent potatoes under the snow.
  bool prepares_winter_crop = false;

  /// AND RISES TO ITS WINDOW'S TIER WHEN THE WINDOW IS CLOSING (0.37.165;
  /// boss, the queue thread [102], option (a)): fewer days are left of the
  /// winter crop's sowing window than the preparation still needs — this
  /// phase's norm-days over the village's draught horses, a day for each
  /// phase after it, and a day to spare. Then the job ranks as any job with
  /// a window (tier 0), and the carting of a heap no longer takes every
  /// horse in the last days the rye can be sown. Measured on 0.37.161
  /// (core-ryeprobe, seed 1931): the fallow for the rye waited unploughed
  /// days 24-30, unharrowed 34-39 while 15-17 horses carted, and the slot
  /// was lost; 10 of 18 failed plan rows of the canon were that.
  bool winter_window_closing = false;

  /// A HAULING JOB WHOSE LOAD'S TASK STANDS AT LEVEL 0 (routing stage B, B3;
  /// boss, the logistics thread [9]: «уровень 0 — впереди всего»): the groom's
  /// request for it is closed first — the job ranks ahead of every window
  /// (PlacementTier -1). Set by the labour sub-step from
  /// WorldState::logistics_tasks (core_common/logistics_state.h).
  bool logistics_urgent = false;

  /// True for the farm rule's zyab — the stubble ploughed for next spring's
  /// crop (FieldRow::autumn_furrowing; register 13; 0.37.18). It has no
  /// window (the tier of work with none) and goes FIRST in that tier: the
  /// reaping, the carting and the winter crop, all with windows, go before
  /// it, the building and the felling after (boss-core-epoch1-queue-2026-09-
  /// 29 [22]). STUB: the place, not a number of the design's.
  bool autumn_furrow = false;

  /// True for the carting of the hay lying mown at a meadow — a heap on land
  /// that is not arable — WHILE THE STORES HOLD THE HAY THE HERDS NEED AHEAD
  /// (0.37.131; LaborConfig::hay_cart_need_days). The job then has no window
  /// and goes LAST of the carts in that tier: a stand's logs, the district's
  /// lot and a dig's load are carted before it. A key among jobs of one
  /// kind, asked after the work kind — the building and the felling keep
  /// their places. With the stores short the job carries a field load's
  /// window instead and this is false.
  bool stacked_hay = false;

  /// True for ploughing and harrowing of a field whose crop carries a position
  /// of this year's plan (plan.due above nought for its resource). Inside one
  /// tier and one kind of window such a field is worked before the others,
  /// then the window's days decide (boss, boss-core-epoch1-5 seq 50; transport
  /// design §1, «Плуг — сначала на поле с позицией плана»): the potato, last
  /// in the queue by its window, stood unploughed to August on a team of
  /// seven while the barley was ploughed.
  bool plan_position = false;

  /// THIS JOB'S CALENDAR WINDOW, as a PAIR — the kind of answer and, where
  /// there is one, the number (core_common/deadline.h).
  ///
  /// | kDays, N | the window is open and closes in N days; 0 is "today" |
  /// | kOverdue, N | it closed N days ago — NOT a deadline, and never
  ///   compared with one |
  /// | kNotApplicable | no window at all: a building site |
  ///
  /// THE PLACEMENT FILLS IN THREE TIERS, and they are tiers and not numbers
  /// on one scale (boss, 2026-09-12): everything with an open window first,
  /// by days; then ALL the overdue work, in no particular order of its own,
  /// because between two jobs that both missed their window the age of the
  /// miss says nothing; then the windowless. Inside a tier the kind decides
  /// (care > harvest > sowing > plowing > harrowing), then the target id.
  ///
  /// A bare `std::uint8_t window_days_left` stood here until 2026-09-12,
  /// with 0 meaning "burning today" AND "closed months ago", and 255 meaning
  /// "no window". The queue put hopeless work first for as long as it
  /// existed, and no range check could see it: the sentinel WAS a legal
  /// value. Daily work that expires tonight — barn care — still passes
  /// kDays with 0, which is the one honest use of that zero.
  Deadline window = DeadlineNotApplicable();

  /// Grams the snow would take from this field's standing crop, set only for
  /// the reaping of an annual in the last days before the snow (boss seq 95,
  /// LaborConfig::harvest_snow_last_days); 0 otherwise. Between two such
  /// jobs with the same days left, the heavier goes first.
  Grams grams_at_risk = 0;

  /// Kilocalories of the standing crop the snow will take if this reaping is
  /// not done: set for the reaping of EVERY arable annual with a ripe crop
  /// (not only in the last days), 0 otherwise. The measure of the harvest
  /// rule 2 (farming design, «Страда: что пропадёт — убирают первым»; boss,
  /// core-boss-potato-crew-trace-2026-10-01 [2], [4]): between reapings with
  /// one edge the queue goes by the food saved per HAND-day —
  /// SavedPerHandDay below — and kilocalories are the grain equivalent up to
  /// the one factor every crop shares.
  float kcal_at_risk = 0.0F;

  /// Set among the reapings of the last days for a field the village cannot
  /// finish before the snow once the heavier finishable ones have spent their
  /// days (boss seq 103). Since the harvest rule 2 it is the TIE-BREAK only:
  /// between two reapings whose food saved per hand-day falls in one band
  /// (kSavedBandRatio) the one that can still be done goes first. As the
  /// first key it put wheat with 11.9 man-days left ahead of potato with
  /// 42.5 two days before the snow, and 23 hands left the potato nobody
  /// (seed 1936, year 1, day 39).
  bool beyond_the_snow = false;
};

/// @brief The width of one band of «food saved per hand-day», as a ratio:
///        two reapings within it count as equal and the tie-break decides
///        (farming design, the harvest rule 2: «в пределах 10 %»). STUB.
/// @note Bands are cut on a fixed logarithmic grid, not measured between the
///       two jobs compared: «within 10 % of each other» is not transitive and
///       cannot order three jobs (the queue's UB-001).
inline constexpr float kSavedBandRatio = 1.1F;

/// @brief One available worker, in placement terms.
struct AssignmentCandidate {
  /// Row index in the current residents table; echoed back in the result.
  std::uint32_t resident_row = 0;

  Vec2 home;  ///< Where the day starts and ends; drives travel time.

  /// Expected norm-days delivered over a full standard day at this
  /// worker's age, health, rest, education (computed by the caller from
  /// resident state; > 0).
  float efficiency = 1.0F;

  /// Current rest, 0-100. Low rest makes a worker a poor pick (experienced
  /// accountant) and predicts a walk-off.
  Metric rest = 70.0F;

  /// The agriculture skill blend, 0-100: what "skill" means for Epoch-I
  /// field work (education design §11; simple work weighs strength over
  /// diplomas, so the caller blends earned skill with stamina).
  Metric skill = 0.0F;

  /// The start-canon lock (livestock design §5): hosts a kolkhoz horse, so
  /// only horse works may take him. IsHorseWork(kind) tells which.
  bool horse_locked = false;

  /// Which home of AssignmentParams::road_km his day starts from;
  /// kNoHomeSlot for a caller that gave no road table.
  std::uint32_t home_slot = 0xFFFFFFFFU;
};

/// @brief AssignmentCandidate::home_slot of a candidate with no road table.
inline constexpr std::uint32_t kNoHomeSlot = 0xFFFFFFFFU;

/// @brief The day's placement parameters, from config and calendar.
struct AssignmentParams {
  /// TURNS TAKE TURNS. The candidates who tie on everything else used to be
  /// broken apart by their ROW, ascending, and rows are handed out at
  /// birth — so the queue was in birth order and never moved. Measured on
  /// the shipped tables, where placement_level is 0 and therefore EVERY
  /// candidate ties at a score of zero: the first fifth of the roster idled
  /// 36 % of its working day and the last three fifths idled 93-96 %. The
  /// same measurement by age reads as "the young never work", and that is
  /// the same fact wearing a face — the young are simply the late rows.
  ///
  /// So the tie is broken on the row PLUS THE DAY, modulo the roster: the
  /// queue advances by one every morning and everybody's turn comes round.
  /// It costs no state, it is the same for one worker and for many, and it
  /// is what a tallyman handing out orders does — the design says there are
  /// no foremen optimising output (society design §1).
  std::uint32_t rotation = 0;

  /// People in the roster, the modulus of that rotation. Zero disables it
  /// and leaves the old fixed order, which is what a world with no
  /// residents wants and what every unit test that does not care gets.
  std::uint32_t roster = 0;

  /// Hours of the daylight work window today (sunrise to sunset shrunk as
  /// the model dictates); a worker's usable hours are these minus twice
  /// his travel.
  float window_hours = 10.0F;

  /// Game hours of one-way travel per kilometre of straight-line distance
  /// for a HAND order: path factor / (real walking speed / kClockScale).
  /// The canonical 5 km/h at factor 1.0 gives 2.4 — which puts a 2-hour
  /// limit at ~830 m, the design's "on foot that is ~800 m" (time §7).
  float walk_hours_per_km = 2.4F;

  /// The same for a HARNESSED order: the plowman rides out with his horse
  /// instead of walking (decision 103), so plowing and harrowing reach
  /// farther in the same hours. 12 km/h at factor 1.0 gives 1.0.
  float harness_hours_per_km = 1.0F;

  /// One-way travel limit in game hours: a job farther than this from a
  /// worker's home cannot take him at all. Six hours by the network since
  /// decision 276 (four along the straight line by decision 109 before), one
  /// rule for a unit's staff and an open field alike (time design §7); the
  /// threshold and the day's output measure the SAME shoulder, which is why
  /// the two speeds above serve both (decision 103).
  float travel_limit_hours = 6.0F;

  /// Less daylight than this left after the road, and the job is not worth
  /// walking to at all. ASSUMPTION.
  float min_usable_hours = 1.0F;

  /// Hours of work behind one norm man-day: delivered norm-days =
  /// worked hours x efficiency / this.
  float standard_day_hours = 10.0F;

  /// Adult kolkhoz horses available today. Every worker placed on a horse
  /// work consumes one from this shared pool; the pool caps those crews.
  std::uint32_t draught_horses = 0;

  /// THE CARTER ON FOOT (0.37.105; core_common/haul.h, WalkerShareOfCartDay;
  /// manual/75-logistics.md §9). 1 while the settlement has no cart: every
  /// load's seam is written in a walker's days, and carriers go in the
  /// queue's own place, as they always did. BELOW 1 while it has carts, and
  /// then:
  ///  - the carting of a cart load — a field's heap, a pit's dig, a store's
  ///    transfer — takes HORSES ONLY in its place in the queue: the horses
  ///    gone, the heap is filled no further and the hands go to the next work
  ///    (until 0.37.105 it took every free hand on foot «until the expected
  ///    output covered the work», at a cart's price for a walker's day);
  ///  - AFTER THE WHOLE QUEUE the hands left with no work at all carry such
  ///    a load on foot, each counted at this share of a cart's norm-day — on
  ///    a rain day thirty idle hands bring in a tonne, and nobody is taken
  ///    off building or felling for it;
  ///  - a stand's logs and the district's lot are never carried on foot,
  ///    whatever this is.
  float walker_share_of_cart_day = 1.0F;

  /// THE LEAST TRIPS A DAY A WALKER IS SENT FOR (0.37.109; labor.csv
  /// walker_min_trips_per_day; boss, 2 October 2026: «пешком носят только
  /// туда, откуда за сутки выходит не меньше двух рейсов»): in the last pass
  /// above a hand goes to a load only if the STANDARD day — or the day's
  /// light where that is shorter — holds this many round trips of his walk
  /// to it: min(standard_day_hours, window_hours) / (2 x his one-way
  /// hours). The pits of the first pair stood three hours out one way and
  /// took 13 163 man-days in ten years for 20 kg a trip: walking, not work.
  /// THE DAY IS THE STANDARD ONE, NOT THE DAY'S LIGHT (0.37.112; until then
  /// it was window_hours, and June's sixteen hours passed the same pits the
  /// threshold was written against: 58 % of the walkers' days still went to
  /// them). A summer day gives a walker more trips, it does not move the
  /// place he is sent to. WHY THE MIN: a winter day of seven hours under a
  /// standard of ten would OPEN places its light does not hold two trips to
  /// (boss, 2 October 2026, [31]).
  /// THE TRIP IS PRICED BY HIS WALK FROM HOME, AND SAID SO: the store a load
  /// goes to is production's to know, and here it is taken to stand at the
  /// walker's door. 0: no threshold.
  float walker_min_trips_per_day = 0.0F;

  /// THE PEOPLE'S CART (transport design §1, «двое и больше — подвода»;
  /// routing stage A, A3; transport.csv people_cart `seats` and
  /// `min_walk_hours`): the hands it seats beside its driver, and the walk one
  /// way, game hours, beyond which a hand on a work that walks
  /// (labor_state.h, TakesThePeoplesCart) is carried. Seats 0: no cart, the
  /// day as before stage A.
  std::uint32_t people_cart_seats = 0;
  float people_cart_min_walk_hours = 0.0F;

  /// Placement quality 0-3 (society design §1): 0 = naive "whoever is
  /// there", 1 = skill and strength, 2 = plus road and fatigue, 3 = master
  /// (phase 1: as 2 — pair synergy is a STUB).
  std::uint8_t placement_level = 0;

  /// THE WAY BY THE ROADS (road_route.h; 0.36.2): effective km from each
  /// home slot to each job, walking and riding —
  /// road_km[((job * home_slots) + slot) * 2 + (riding ? 1 : 0)], riding by
  /// the job's own mode (a team, a cart, a log cart). The labour hour
  /// measures the day by the same way, so the choice and the day cannot
  /// differ. Empty: the straight line at the pace (a test with no world).
  std::vector<float> road_km;
  std::uint32_t home_slots = 0;

  /// THE DAY WITH A HORSE BEGINS AT THE HORSE YARD (horse_yard_road.h;
  /// 0.37.158): game hours on foot from each home slot to the yard, and
  /// effective km from the yard to each job by its riding mode. A hand who
  /// takes a horse (IsHorseWork or AssignmentJob::harnessed) is judged by
  /// walk + ride, the way the labour hour will measure his day. Both empty:
  /// the team is not stabled, or its yard is gone — from home, as before.
  std::vector<float> yard_walk_hours;
  std::vector<float> yard_ride_km;
};

/// @brief Index value meaning "left idle today" in the result.
inline constexpr std::uint32_t kNoJobAssigned = 0xFFFFFFFFU;

/// @brief Why the plan left what it left (labor_state.h IdleReason and
///        JobShortfall; boss-core-epoch1-resume [73]-[74]), asked of the
///        plan's own rules and not guessed after them.
struct PlacementDiagnosis {
  /// Per job: why its work was not covered; nothing when it was, or when
  /// it had no work left.
  std::vector<std::optional<JobShortfall>> shortfall;

  /// Per candidate: why he stands idle — the first reason that holds over
  /// the jobs left short, in the order of IdleReason, save that
  /// kUnexplained outranks every other (a contradiction is not hidden
  /// behind another job's reason); kWorkCovered when no job was left short.
  /// Nothing for the placed.
  std::vector<std::optional<IdleReason>> idle;
};

/// @brief Norm-days ONE hand is expected to deliver on `job` today: the mean,
///        over the candidates the job may take and the road lets go, of the
///        daylight left after the round trip, by his efficiency, over the
///        standard day — the same number the placement fills a crew by.
/// @param job_index The job's index in the caller's vector (AssignmentParams::
///                  road_km is indexed by it).
/// @return 0 when nobody can go. In October a field 1.4 km out leaves a
///         walker 2.5 hours of a 9.4-hour day: 0.2 of a norm (the print of
///         0.37.81).
float ExpectedNormPerHand(const AssignmentJob& job,
                          std::uint32_t job_index,
                          const std::vector<AssignmentCandidate>& candidates,
                          const AssignmentParams& params);

/// @brief The harvest rule 2's measure: kilocalories at risk over the
///        HAND-days the reaping still needs at today's expected output —
///        kcal_at_risk x ExpectedNormPerHand / work_days_remaining.
/// @return 0 for a job with nothing at risk, no work left or nobody to go.
float SavedPerHandDay(const AssignmentJob& job,
                      std::uint32_t job_index,
                      const std::vector<AssignmentCandidate>& candidates,
                      const AssignmentParams& params);

/// @brief The last tier of the placement queue: work with no window at all —
///        a building site, a felling, a dig, a planting, a road, the carting
///        of a district's lot, a stand's logs or a pit's load, the zyab, a
///        field's work whose crop gives it no window, the meadow's cut outside
///        its months.
inline constexpr int kWindowlessTier = 4;

/// @brief The tier of the placement queue `job` stands in, the first key the
///        queue is ordered by (AssignmentJob::window): 0 an open window, 1 an
///        overdue one, 2 the meadow's cut in its window, 3 the preparation of
///        a fallow for this autumn's winter crop, kWindowlessTier the rest.
///
/// WHY IT IS ASKED OUTSIDE THE QUEUE (0.37.100; district_lot's red on 0.37.99,
/// seed 1931, day 30): production's daily block opens a field's phase in
/// hour 0 AFTER the morning's placement, and the hour-1 top-up placed on it
/// only the idle and only the horses the morning left. So what the morning
/// gave to windowless work stayed there: five horses rode for the district's
/// lot while the harrowing of the rye's fallow, opened that dawn, stood the
/// day — «the fetch is windowless, the field's horse work goes first» held in
/// the morning's list alone. THE RULE THE TOP-UP KEEPS SINCE: when a job
/// stands with nobody on it at hour 1, the morning's placements on the
/// windowless tier are let go and placed again in one queue with what stands
/// uncrewed — the placement the morning would have made had it known. Before
/// sunrise nobody has worked an hour. Placements on the tiers above stay: the
/// morning's order among work WITH a window is not asked again.
/// @return -1 (a load whose task stands at level 0, logistics_urgent) to
///         kWindowlessTier.
int PlacementTier(const AssignmentJob& job);

/// @brief Places the day's workers over the day's jobs.
/// @param jobs       The openings; order irrelevant (the algorithm orders
///                   deterministically by urgency, then kind, then — between
///                   reapings with food at risk — by the band of
///                   SavedPerHandDay, then target id — never by input
///                   position).
/// @param candidates The workforce; order irrelevant likewise (ties break
///                   by resident_row, the stable identity).
/// @param params     The day's parameters.
/// @param rides_horse Optional, per candidate: 1 when his placement took a
///                   horse out of the pool (a ploughman or harrower always;
///                   a carter while one was left), else 0. A carter placed
///                   after the pool ran dry was judged on foot — reach and
///                   day's norm — and walks (WorkAssignment::rides_horse);
///                   except on a lot at the district, where none is placed.
///                   Resized to `candidates`; nullptr when not wanted.
/// @param road_blocked Optional, per job: 1 when it had work left, nobody was
///                   placed on it, and free workers were turned away by the
///                   road rule alone (time design §7) — the job the road
///                   stopped today. Resized to `jobs`; nullptr when not wanted.
/// @param rides_cart_with Optional, per candidate: the candidate index of the
///                   driver of THE PEOPLE'S CART he rides (routing stage A,
///                   A3), or kNoJobAssigned. The driver himself has
///                   rides_horse 1 and kNoJobAssigned here. Resized to
///                   `candidates`; nullptr when not wanted — the carts are
///                   given all the same, and rides_horse names their drivers.
///
/// THE PEOPLE'S CARTS ARE GIVEN AFTER THE QUEUE, from the horses it left
/// (transport design §1: «плуг → подводы → всадники», and §11 «Работник без
/// тягла — из остатка»): in the queue's order, to each job of a kind that
/// walks (TakesThePeoplesCart), its crew is the hands placed on it whose
/// walk one way is longer than people_cart_min_walk_hours, and — while the
/// job is short and seats are left — free hands the ride lets reach it,
/// judged by the ride. Two or more: a cart for each people_cart_seats + 1 of
/// them, while horses are left; the first of each cart drives. Seats beyond
/// the horses go to the longest walks, and the rest walk as they were
/// placed. Before the last pass of the carriers on foot, so a far job's
/// cart is offered the idle before a back load is.
/// @return Per candidate (same order as `candidates`): the index into
///         `jobs` he works today, or kNoJobAssigned — surplus hands idle
///         and earn nothing (a trudoden is a work norm, not attendance).
/// @note Pure and deterministic: equal inputs give the equal vector on any
///       platform. No RNG — even the naive level uses stable order, not
///       chance.
std::vector<std::uint32_t> PlanDayAssignments(
    const std::vector<AssignmentJob>& jobs,
    const std::vector<AssignmentCandidate>& candidates,
    const AssignmentParams& params,
    std::vector<std::uint8_t>* rides_horse = nullptr,
    std::vector<std::uint8_t>* road_blocked = nullptr,
    PlacementDiagnosis* diagnosis = nullptr,
    std::vector<std::uint32_t>* rides_cart_with = nullptr);

}  // namespace core

#endif  // CORE_LABOR_ASSIGNMENT_H_
