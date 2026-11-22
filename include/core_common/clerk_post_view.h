/// @file
/// @brief The tally clerk's post as the office reads it (#589; society.md «Учётчик в Эпохе I
///        обязателен», «Учётчик умер или уехал»): who holds it, since when, why it stands
///        vacant and the morning the chairman's order fills it; and the ranked candidates -
///        game's «Назначить учётчика» card and the Тетрадь's first line.
/// @threading PARALLEL_READONLY
/// Plain data, built between steps by a pure read (ISession::ClerkPost, ClerkCandidates).

#ifndef CORE_COMMON_CLERK_POST_VIEW_H_
#define CORE_COMMON_CLERK_POST_VIEW_H_

#include <cstdint>
#include <vector>

#include "core_common/calendar.h"
#include "core_common/ids.h"
#include "core_common/resident_state.h"

namespace core {

/// @brief Who put the clerk in the post (save 226). Stored: append only.
enum class ClerkFilledBy : std::uint8_t {
  kAutomatic = 0,       ///< The first morning's appointment, the core's own (UpkeepClerkMorning)
  kChairmanOrder,       ///< The chairman's own kAppoint
  kOrderWrittenForHim,  ///< The 3rd morning's kAppoint the core wrote for him on a vacancy
  kClerkFilledByCount,  ///< NOT A VALUE: the count, for codecs and mirrors
};

/// @brief Why the post stands empty, or that it does not.
enum class ClerkVacancy : std::uint8_t {
  kInPost = 0,      ///< A clerk holds the post.
  kNeverAppointed,  ///< Before the first morning, or no unit carries the post.
  kDied,            ///< The clerk died.
  kLeft,            ///< The clerk moved out or his family left the village.
  kUnitGone,        ///< The post's unit was taken down (VacatePostsWithNoUnit).

  kClerkVacancyCount,  ///< The number of values; never a vacancy (the boundary's tables).
};

/// @brief The post's record in WorldState (save 219): the facts the holder's row does not
///        carry - when he took it, or why and since when it stands empty.
struct ClerkRecord {
  SimDay since_day = 0;
  ClerkVacancy vacancy = ClerkVacancy::kNeverAppointed;
  SimDay vacant_since_day = 0;
  /// The holder last seen in post - so a vacancy can tell «died» from «left» when his row
  /// is already gone.
  ResidentId last_clerk;
  /// WHO PUT THE HOLDER IN THE POST (save 226; game's office point 2 «учётчик назначен»: the
  /// done kAppoint is removed in its step, so the fact has to live here). Written the step he
  /// is first seen in post; kept while he holds it.
  ClerkFilledBy filled_by = ClerkFilledBy::kAutomatic;
  /// 1 between the 3rd morning's order written for the chairman and the step its holder is
  /// seen in post, so that holder reads kOrderWrittenForHim, not the chairman's own order.
  std::uint8_t written_order_pending = 0;
};

/// @brief The post today.
struct ClerkPostView {
  /// False from a bare engine: say «not measured», never «no clerk».
  bool measured = false;

  /// The holder; invalid while vacant.
  ResidentId clerk;

  /// The post's unit: the church store in Epoch I, the office once built.
  UnitId unit;

  /// The day the holder took the post.
  SimDay since_day = 0;

  /// Who put him there (ClerkRecord::filled_by): the office's «назначен председателем, день
  /// since_day» is kChairmanOrder. Meaningful only while in post.
  ClerkFilledBy filled_by = ClerkFilledBy::kAutomatic;

  ClerkVacancy vacancy = ClerkVacancy::kNeverAppointed;

  /// While vacant: the day it became so.
  SimDay vacant_since_day = 0;

  /// While vacant: the morning the chairman's order appointing the first candidate is
  /// written into the book, if he appoints nobody before it (the 3rd morning).
  SimDay auto_appoint_day = 0;

  /// The clerk's level: 0, STUB until «Рост ступени стажем» is switched on.
  std::uint8_t level = 0;
};

/// @brief One candidate, as the rule ranks him.
struct ClerkCandidate {
  ResidentId resident;
  EducationStage education_stage = EducationStage::kNone;
  /// Biological years.
  float age_years = 0.0F;
  /// He holds another post now: appointing him would empty it.
  bool holds_other_post = false;
};

/// @brief The ranked candidates: those with no other post first, then by education
///        (highest first), then the older within working age, then the lower id; post
///        holders after all the free ones. The first row is the one the rule appoints.
struct ClerkCandidatesView {
  /// False from a bare engine.
  bool measured = false;
  std::vector<ClerkCandidate> rows;
};

}  // namespace core

#endif  // CORE_COMMON_CLERK_POST_VIEW_H_
