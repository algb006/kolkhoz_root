/// @file
/// @brief ResidentRow — the per-person state: the layout for the whole game.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::residents (a StateTable) under the double-buffer
/// discipline: parallel phases 2 and 5 are split by FAMILY, and a worker
/// owning a family owns the resident rows of its members — disjoint sets,
/// interleaved in the dense table, which is race-free (distinct objects).
/// Membership is discovered through the `family` field, which is the one
/// field of an unowned current row a worker may read (buffer-law rule 4).
/// What makes that legal is not that it never changes — a wedding moves a
/// resident to a new household — but that it is written ONLY in the
/// sequential decisions slot, so it stands still for the whole of every
/// parallel phase. Structural changes (births, deaths, departures,
/// marriages) happen there and nowhere else.
///
/// Design sources: metrics design §2 (two rows: current state and
/// inclinations), life-cycle §1 (attributes and derived values), education
/// §2/§4/§5/§11, crime §2/§6. Derived values — labor productivity, carrying
/// ability, the infant/schoolchild/worker category, grandparent status — are
/// NOT stored: they are pure functions of what is here (state model law).
///
/// Ages: the state stores only birth_day. Biology runs ~4x faster than the
/// calendar; the factor is a balance parameter (tables/life.csv), never
/// hardcoded. Biological age = game years since birth x that factor.
///
/// Stage plan for the fields (everything is laid out now; later stages only
/// start WRITING what stage 3 keeps at neutral defaults):
///   stage 5 (labor): the `work` block and `rest` move daily; earned skills
///   creep with practice;  stage 6: satiety and cold move with food and
///   heating;  project phases 2-3: statuses, offenses, attitude, traits,
///   passport get their mechanics.

#ifndef CORE_COMMON_RESIDENT_STATE_H_
#define CORE_COMMON_RESIDENT_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/labor_state.h"
#include "core_common/quantities.h"
#include "core_common/state_table.h"

namespace core {

enum class Sex : std::uint8_t {
  kFemale = 0,
  kMale = 1,

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kSexCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kSexCount,
};

/// @brief Completed education, five steps (education design §2). Never lost.
enum class EducationStage : std::uint8_t {
  kNone = 0,    ///< Illiterate: the starting state of most Epoch-I adults.
  kPrimary,     ///< Primary school step.
  kSecondary,   ///< Secondary school step.
  kVocational,  ///< Tekhnikum / uchilishche, 3 years.
  kHigher,      ///< Institute; only after vocational.

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kEducationStageCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kEducationStageCount,
};

/// @brief Public status (metrics design §2, group 3). Advances by own merit,
/// never by the parents'. Moved by the waves of core_residents/membership.h
/// (boss, parcel 334); until that body lands it stays kNone.
enum class SocialStatus : std::uint8_t {
  kNone = 0,  ///< No organization: children under 10 and unaffiliated adults.
  kPioneer,   ///< Ages 10-14.
  kKomsomol,  ///< Ages 14-26.
  kParty,     ///< From 18, for life.

  /// NOT A VALUE, and never written to a save or read from one: the
  /// codecs range-check 0..kSocialStatusCount-1 and this is what they check against.
  /// Values are appended BEFORE it — that is the whole rule, and it is a
  /// fact here rather than an instruction somewhere else. A length
  /// written out by hand beside an enum drifts, and four of them already
  /// had (journal_codec.cpp).
  kSocialStatusCount,
};

/// @brief The quiet trade a resident keeps at night (crime design §7, §9;
/// "Ночной промысел в ядре Эпохи I — числами"). Assigned by the core at the
/// year's turn (core_residents/night_trade.h); kept until he is gone, as the
/// chairman's orders against it are a later door. Stored in a save: append
/// only.
enum class NightTrade : std::uint8_t {
  kNone = 0,
  kDistiller,  ///< Distils at home and sells at his gate.
  kNetFisher,  ///< One of the pair who net the lake and the river pools.
  kHunter,     ///< The one who shoots in the forest at night.

  /// NOT A VALUE: the count, for the codecs' range check and a mirror.
  kNightTradeCount,
};

/// @brief A standing appointment: which post, at which unit. Plain data;
/// both invalid means "holds no post", and the two are set and cleared
/// together — a profession with no unit, or a unit with no profession, is a
/// row the codec refuses.
struct PostAssignment {
  ProfessionId profession;

  UnitId unit;
};

/// @brief One resident. Plain data; invalid ids mean "no such relative".
struct ResidentRow {
  // -- identity and kinship ------------------------------------------------
  FamilyId family;  ///< The household this person lives in.

  ResidentId mother;  ///< Invalid for the starting generation.

  ResidentId father;  ///< Invalid for the starting generation.

  ResidentId spouse;  ///< Invalid = unmarried or widowed.

  Sex sex = Sex::kFemale;

  /// Day of birth in campaign days — SIGNED: the starting generation was
  /// born before day 0 (an old-timer's birth lies hundreds of days in the
  /// negative). The only stored age fact (see @file).
  std::int32_t birth_day = 0;

  // -- current state (metrics design §2, "what is with him now") -----------
  /// Written by the family meal (family_meal.cpp) since stage 6; read by
  /// health and, since 2026-09-18, by the worker's output (labor_day.h,
  /// FedFactor). It said "STUB neutral until food arrives" until that day,
  /// long after the food had arrived.
  Metric satiety = 70.0F;

  Metric health = 70.0F;

  Metric rest = 70.0F;  ///< STUB neutral until labor arrives (stage 5).

  Metric cold = 0.0F;  ///< 0 = warm; appears only when freezing (stage 6).

  /// Personal cleanliness, 0..100 (health design §3). Beside health and
  /// satisfaction rather than inside either, and it feeds both — «вшивое село
  /// довольным не бывает».
  ///
  /// IT FALLS BY ITSELF and rises from ONE thing here, which is a stub over
  /// the design and not against it. The design gives four risers — the
  /// bathhouse, a yard's own bathhouse, clean water nearby, and soap with a
  /// change of linen — and this core has none of the machinery for three of
  /// them: no model of a resident VISITING a unit, no water supply at all,
  /// and no soap as a line of its own (it is inside the `consumer_goods`
  /// bundle). So a standing bathhouse raises the whole village, which is the
  /// part the design does have — «баня растит» — with «кто именно и как часто
  /// ходит» left out and named.
  ///
  /// The fall is whole: plain time, doubled by dirty work, and more above
  /// +25. Below `hygiene_disease_threshold` the village gets lice and scabies
  /// — and THE CORE DOES NOT MODEL THE DISEASE. `diseases.csv` and
  /// `disease_severity.csv` are declared as tables the core has no business
  /// with, while `first_hygiene_disease` is declared the core's to raise, and
  /// the two are reconciled exactly here: the core owns the CAUSE and says
  /// when it bites; the illness stays off-screen, as the quest's own brief
  /// asks («болезнь остаётся за кадром, в кадре — причина»).
  Metric hygiene = 60.0F;

  /// STUB — A CONSTANT WEARING A METRIC'S CLOTHES. Drawn once in genesis
  /// (50..70) and never written again by anything: measured 2026-09-17 over
  /// the whole tree, the only two writers are genesis and the save codec.
  /// Hunger does not move it, and neither does anything else.
  ///
  /// AND THREE CONSUMERS ALREADY READ IT, which is why this mark is worth
  /// more than the two beside it. `rest` and `cold` say they are stubs and
  /// are read as stubs; `mood` said nothing, so it reads as live:
  ///   * core_labor/labor_day.cpp — a PivotFactor in the day's output,
  ///     standing in the same row as health, rest, age and skill. Thirty-
  ///     three years of a resident's work are scaled by a number drawn on
  ///     the day the kolkhoz was founded;
  ///   * core_residents/membership.cpp — the Komsomol gate;
  ///   * and the Party gate beside it.
  ///
  /// It is not a hole in the design: metrics design §2 names the seven
  /// inputs in as many words — «реакция на всё сразу: сытость, здоровье,
  /// жильё, отдых, отношение председателя, события в селе, положение дел в
  /// стране» — and gives no weights. So this is a system not built yet,
  /// with its inputs already named, and the weights are boss's to set.
  ///
  /// WHAT MUST HAPPEN BEFORE THE MARK COMES OFF: the three readers above
  /// start answering differently the moment this begins to move, and the
  /// first of them decides a village's output. Whoever fills it in re-reads
  /// the labour curve with it, rather than discovering afterwards that the
  /// farm's productivity acquired a new driver.
  Metric mood = 60.0F;

  // -- today's work (labor_state.h; stage 5) -------------------------------
  /// Written only by the labor sub-step of the decisions slot: assigned in
  /// the morning, drained hourly, closed out at day end. kind == kNone means
  /// idle today — including a fatigue walk-off, which clears the kind but
  /// keeps worked_norm_days_today until the close-out pays it.
  WorkAssignment work;

  // -- the post (task A7; manual/74-posts.md) --------------------------------
  /// The post this resident HOLDS — a standing appointment, not today's
  /// work: the groom, the storekeeper, the timekeeper are appointed and
  /// keep their place until dismissed (phase-2 plan §4, A7). One post per
  /// person, because one work per day (time design §11: no moonlighting).
  /// Written only by the labor sub-step, at the day's close, from a
  /// kAppoint or kDismiss order accepted earlier that day (time design §11:
  /// "a change of post — only after the working day"); cleared with the
  /// row when the resident dies or leaves. Invalid profession = holds none.
  ///
  /// What holding a post DOES today: the holder is not in the accountant's
  /// morning pool and is placed first on his own unit's daily work when the
  /// core models it — the yard's herd care for the groom. A post whose work
  /// the core does not model yet keeps its holder reserved and idle (STUB,
  /// named in the post table's own terms). The groom is the post this stage
  /// exists for: the day after one is appointed, the herd day stables the
  /// horses (herd_state.h, production).
  PostAssignment post;

  // -- education (education design §2, §4, §5) -----------------------------
  EducationStage education_stage = EducationStage::kNone;

  float education_grade = 0.0F;  ///< Final grade of the finished stage, 1.0-5.0; 0 = none.

  float current_grade = 0.0F;  ///< While studying, 1.0-5.0; 0 = not studying.

  /// The school he is enrolled in (core_residents/schooling.h); invalid when
  /// he is not a pupil. The host's `enrolled_in`.
  UnitId school;

  Metric self_education = 0.0F;  ///< What was gained beyond the diploma, 0-100.

  // -- professionalism: three skills, schooled and earned (education §11) --
  //
  // STUB, ALL SIX, AND THE LABOUR MODEL ALREADY LEANS ON THEM. Nothing in the
  // core writes any of them: they are zero here, carried through a save and
  // read back, and `core_save/save_rows.cpp` is their only other mention —
  // except that `core_labor/labor_day.cpp` READS them, blending earned and
  // schooled into what a day of work is worth. So productivity is computed
  // from six constants, and the man who has ploughed for thirty years is
  // exactly as good at it as the boy beside him.
  //
  // THE MARKER IS HERE FOR THE REASON THE ONE ON attitude_to_chairman IS:
  // unmarked, in a file where stubs are marked, these read as working, and a
  // script asking "who is the best agronomist" gets a constant back and no
  // warning (host, 2026-09-13, who found them by listing every seam field the
  // core never writes). The difference is that this pair of readers makes them
  // look alive from the inside too — there IS an arithmetic, and it runs.
  //
  // What moves them is not decided here: skills belong to the education
  // mechanics, which are a later phase.
  Metric skill_agriculture_schooled = 0.0F;

  Metric skill_agriculture_earned = 0.0F;

  Metric skill_technic_schooled = 0.0F;

  Metric skill_technic_earned = 0.0F;

  Metric skill_admin_schooled = 0.0F;

  Metric skill_admin_earned = 0.0F;

  // -- inclinations (metrics design §2, "what he is by nature") ------------
  Metric intellect = 50.0F;  ///< Born with; random with a weak parental pull.

  Metric stamina = 50.0F;  ///< Born with; never changes.

  Metric optimism = 50.0F;  ///< Born with; never changes.

  Metric ideology = 50.0F;  ///< Forms from childhood conditions, locks at 16.

  Metric sportiness = 0.0F;  ///< Acquired: grows from age 7 with training.

  Metric sport_inclination = 0.0F;  ///< Hidden urge to train (metrics design §2).

  // -- social and other (crime design §2, §6; metrics design §2a, §4) ------
  SocialStatus social_status = SocialStatus::kNone;

  /// The night trade he keeps (NightTrade); kNone for nearly everyone.
  NightTrade night_trade = NightTrade::kNone;

  /// A distiller's supply: the month (NightTheftTally month index + 1) in
  /// which his last night at the stores brought any raw material; 0 never.
  /// «Самогонщик без сырья этого месяца не продаёт» (crime design §7;
  /// register 206): the +2, the purchase and the sobriety all ask whether
  /// a SUPPLIED distiller is in reach, not whether a distiller exists.
  std::uint32_t distiller_supplied_month = 0;

  /// Days of the current calendar month on which he went out to work (hours
  /// away above zero) — counted by the labor day close, read and cleared at
  /// the month's turn by
  /// the drinking rule (core_residents/alcoholism.h). 0..kDaysPerMonth.
  std::uint8_t days_worked_this_month = 0;

  Metric alcoholism = 0.0F;

  /// THE CHAIRMAN'S TALK (kTalkToSport; register 223; save 69): the last day
  /// he goes to the field and the reading hut because he was talked into it,
  /// whatever his age and drinking; 0 when no talk stands. The month's turn
  /// reads it on its own day: in force while that day is at or before this
  /// one (core_residents/sport.h, Goes). A SimDay, spelt out so this header
  /// does not take in the calendar.
  std::uint32_t talk_until_day = 0;

  /// THE TWIN (life cycle §4, «Двойня»; register 245; save 78): the other
  /// child of the same birth, invalid for a single birth. Set on both.
  ResidentId twin;

  /// AWAY IN THE DISTRICT (district_car_state.h; save 79): the day he is
  /// home again, 0 when he is in the village. With `away_until_hour` it names
  /// the tick; the last `away_walk_hours` of it he walks in from the border
  /// (0 when he comes with the milk cart). While away he is nobody's worker,
  /// eats nothing from the larder and is not judged by the village's
  /// metrics; his family still counts him. STUB: that is the contract — no
  /// code writes or reads these four fields yet, so every resident is home
  /// (district_car_state.h says why). A SimDay and an hour spelt out, so
  /// this header does not take in the calendar and the row keeps its
  /// four-byte alignment.
  std::uint32_t away_until_day = 0;

  Metric crime_inclination = 0.0F;  ///< Recomputed seasonally from its formula.

  std::uint16_t offense_count = 0;  ///< Recorded offenses; the village remembers.

  /// How this person regards the chairman, 0..100 (metrics §4).
  ///
  /// STUB, AND IT NEVER MOVES: nothing in the core writes it. It is set to 50
  /// here, carried through a save and read back, and that is the whole of its
  /// life — `core_save/save_rows.cpp` is its only other mention anywhere.
  ///
  /// THE MARKER IS HERE BECAUSE ITS ABSENCE COST SOMEBODY A DAY. The line used
  /// to read "Personal, per resident (metrics §4)" with no STUB while the
  /// fields around it carried one, and in a file where stubs ARE marked an
  /// unmarked field reads as working — fairly. host wrote a real Epoch I scene
  /// whose entry condition asks whether an adult trusts the chairman enough to
  /// come of his own accord, loaded it clean, and it did not fire once in 120
  /// days: the best attitude in the whole village, on any day, is exactly 50.0
  /// (host, 2026-09-13).
  ///
  /// A comment describing the INTENT as the ACTUALITY is the mirror of the
  /// project's own rule that a note calling live work absent reads as leave to
  /// skip it. This direction is the dearer of the two — a note calling absent
  /// work live reads as leave to BUILD ON IT, and somebody did.
  ///
  /// What it becomes is not decided here: whether attitude moves in Epoch I,
  /// and on what, is boss's open question №23. Until he answers, a reader who
  /// needs a chairman's standing has nothing to read.
  Metric attitude_to_chairman = 50.0F;

  std::uint8_t has_passport = 0;  ///< 0/1. Kolkhozniks have none until Epoch III.

  /// 0/1: identical twins (life cycle §4) — always of one sex, one figure
  /// (height_deviation, build_deviation) and, for the layer, «один набор
  /// жителя на двоих»: they differ by clothes and character only. 0 for
  /// fraternal twins and single births. Set on both.
  std::uint8_t identical_twin = 0;

  /// The hour of `away_until_day` he is home (see away_until_day).
  std::uint8_t away_until_hour = 0;

  /// The last hours of his absence spent walking in from the border.
  std::uint8_t away_walk_hours = 0;

  /// Why he is away (AwayReason); kNone when he is in the village with no
  /// car coming. kAwaitingAmbulance is the one reason that is NOT away: he
  /// lies at home for the car (away_in_district.h tells the two apart).
  std::uint8_t away_reason = 0;

  /// Character traits bitmask (life-cycle §1: 2-4 per person). STUB: the
  /// trait roster and its effects arrive with dialogues (project phase 3);
  /// the field is laid out so saves and genesis are final.
  std::uint16_t traits = 0;

  // -- the body (figure design; boss, 2026-09-06) ---------------------------

  /// How this person's height differs from the base of their sex, AS A
  /// FRACTION: 0.04 is four percent taller. Drawn once at birth and never
  /// changed.
  ///
  /// A FRACTION AND NOT CENTIMETRES, and that is the decision the whole
  /// field rests on. A tall child MUST grow into a tall adult; in
  /// centimetres the deviation would have to be recomputed at every step of
  /// the age ladder, and the person would jerk on each transition. A
  /// fraction carries across the steps by itself, because it is a fact about
  /// the person and not about their current size.
  ///
  /// Small on purpose: sigma is 3.7 % and the draw is cut at 2.5 sigma, so
  /// the whole population lives inside 9.25 % — INCLUDING the inherited
  /// half-spread a child adds to its parents' mean, which is held to the
  /// same band rather than allowed to walk outward from it. The knobs are
  /// world_params.csv rows, not constants here, and their PRODUCT is capped
  /// as well as each of them: two legal knobs multiplied out to three would
  /// otherwise make a person shorter than nothing (core_common/body.h).
  ///
  /// The first version promised this band and did not hold it: half a
  /// spread on top of an already-cut mean reached 13.9 % in one generation
  /// and crept on from there. A bound only the comment believes in is worse
  /// than none.
  float height_deviation = 0.0F;

  /// The same for width: the permanent half of stoutness. Sigma 7 %.
  ///
  /// THE OTHER HALF IS NOT STORED ANYWHERE, and that is deliberate
  /// (boss, 2026-09-06): condition — whether a person is well fed today —
  /// is read by the layer from satiety and health, which it already
  /// receives. A hungry village looks hungry without a field of its own,
  /// which is what a live signal means.
  float build_deviation = 0.0F;
};

/// @brief The residents table type used by WorldState.
using ResidentTable = StateTable<ResidentId, ResidentRow>;

}  // namespace core

#endif  // CORE_COMMON_RESIDENT_STATE_H_
