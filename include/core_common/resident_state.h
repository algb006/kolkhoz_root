/// @file
/// @brief ResidentRow — the per-person state: the layout for the whole game.
/// @threading PARALLEL_READONLY
/// Rows live in WorldState::residents (a StateTable) under the double-buffer
/// discipline: parallel phases 2 and 6 are split by FAMILY, and a worker
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
};

/// @brief Completed education, five steps (education design §2). Never lost.
enum class EducationStage : std::uint8_t {
  kNone = 0,    ///< Illiterate: the starting state of most Epoch-I adults.
  kPrimary,     ///< Primary school step.
  kSecondary,   ///< Secondary school step.
  kVocational,  ///< Tekhnikum / uchilishche, 3 years.
  kHigher,      ///< Institute; only after vocational.
};

/// @brief Public status (metrics design §2, group 3). Advances by own merit,
/// never by the parents'. STUB in phase 1: stays kNone until organizations
/// exist (project phase 3).
enum class SocialStatus : std::uint8_t {
  kNone = 0,  ///< No organization: children under 10 and unaffiliated adults.
  kPioneer,   ///< Ages 10-14.
  kKomsomol,  ///< Ages 14-26.
  kParty,     ///< From 18, for life.
};

/// @brief One resident. Plain data; invalid ids mean "no such relative".
/// @brief A standing appointment: which post, at which unit. Plain data;
/// both invalid means "holds no post", and the two are set and cleared
/// together — a profession with no unit, or a unit with no profession, is a
/// row the codec refuses.
struct PostAssignment {
  ProfessionId profession;

  UnitId unit;
};

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
  Metric satiety = 70.0F;  ///< STUB neutral until food arrives (stage 6).

  Metric health = 70.0F;

  Metric rest = 70.0F;  ///< STUB neutral until labor arrives (stage 5).

  Metric cold = 0.0F;  ///< 0 = warm; appears only when freezing (stage 6).

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

  Metric self_education = 0.0F;  ///< What was gained beyond the diploma, 0-100.

  // -- professionalism: three skills, schooled and earned (education §11) --
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

  Metric alcoholism = 0.0F;

  Metric crime_inclination = 0.0F;  ///< Recomputed seasonally from its formula.

  std::uint16_t offense_count = 0;  ///< Recorded offenses; the village remembers.

  Metric attitude_to_chairman = 50.0F;  ///< Personal, per resident (metrics §4).

  std::uint8_t has_passport = 0;  ///< 0/1. Kolkhozniks have none until Epoch III.

  /// Character traits bitmask (life-cycle §1: 2-4 per person). STUB: the
  /// trait roster and its effects arrive with dialogues (project phase 3);
  /// the field is laid out so saves and genesis are final.
  std::uint16_t traits = 0;
};

/// @brief The residents table type used by WorldState.
using ResidentTable = StateTable<ResidentId, ResidentRow>;

}  // namespace core

#endif  // CORE_COMMON_RESIDENT_STATE_H_
