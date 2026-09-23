/// @file
/// @brief The district's people who come to the kolkhoz — who, on what kind of
/// visit, and what the visit found (district characters design §2, "Эпоха I
/// числами"; the seam's form, host/manual/93-script-fact-doors.md §4.1; boss,
/// parcels 188, 200 and 324).
/// @threading PARALLEL_READONLY
/// Plain data. The table of visits on their way is written only by the
/// production sub-step of the decisions slot (phase 3) on the sim thread — a
/// row added when a visit is announced or called, removed on the day it
/// arrives — and read by anyone between steps. SAVED (save format 38).
///
/// ONE EVENT WITH ITS OUTCOME, NOT EIGHT FACTS. The core says that a face of
/// the district came, on which kind of visit, and what it found; which of the
/// script's facts that moment is — `karasev_inspection_completed`,
/// `zhernova_found_polushkina_miss` and the rest — is the host's reading of the
/// event (boss, parcel 188). "Итоговая реплика никогда не предшествует расчёту
/// проверки": the outcome is computed here and handed out as words, so no
/// script has to compute an inspection a second time.
///
/// WHAT A VISIT FINDS (since 2026-09-18; district_visit.cpp, InspectVisit):
/// kDiscrepancy when the stores hold more than the accumulation limit. A face
/// that counts the stores then seizes the surplus (SeizeAboveLimit) and the
/// chairman is summoned «на ковёр» (SummonCause::kAuditDiscrepancy). Until
/// 0.34.29 this header said a finding was "always kNone", a week after it had
/// stopped being so, and host wrote an audit on the word. The trial's
/// shortfall trigger (epochs §8, a SHORTAGE of 20 % against the books) has no
/// writer: the auditor finds only a surplus.
///
/// WHAT EPOCH I RAISES (boss, parcel 324):
///   * regular visits — Karasev in `district_visit_karasev_month`, Polushkina in
///     `district_visit_polushkina_month`, announced `district_visit_notice_days`
///     game days ahead (EventKind::kDistrictVisitAnnounced);
///   * extraordinary visits — Korenev the day after a failed plan (the raikom's
///     business: he asks about the failed year), and the senior of a junior's
///     channel after a regular visit that found something (Karasev → Stozharov,
///     Polushkina → Zhernova).
/// STUB: residents' complaint, an emergency, a newspaper satire and a
/// denunciation call nobody yet (the design's other four causes);
/// reception and gift are in the vocabulary and never raised, as the
/// chairman has no order for either and the faces have no personal
/// reputations.

#ifndef CORE_COMMON_DISTRICT_VISIT_STATE_H_
#define CORE_COMMON_DISTRICT_VISIT_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// @brief A face of the district (characters design §7). The seam word is the
/// lower-case surname — `korenev`, `stozharov`, `karasev`, `zhernova`,
/// `polushkina` — the same word as the fact subject's `district_face` and the
/// dialogue's (host 0.7.133). Stored in a save: append only.
enum class DistrictFace : std::uint8_t {
  kKorenev = 0,  ///< Secretary of the raikom; comes "по случаю".
  kStozharov,    ///< Senior instructor: ideology; extraordinary visits.
  kKarasev,      ///< Junior instructor: ideology; regular visits.
  kZhernova,     ///< Senior auditor: finances; extraordinary visits.
  kPolushkina,   ///< Junior auditor: finances; regular visits.

  kDistrictFaceCount,
};

/// @brief The kind of a visit (characters design §2). Seam words `regular`,
/// `extraordinary`, `reception`, `gift`. Stored in a save: append only.
enum class DistrictVisitKind : std::uint8_t {
  kRegular = 0,    ///< By the plan, announced ahead; a junior, formal.
  kExtraordinary,  ///< On a signal, unannounced; a senior, thorough.
  kReception,      ///< Polushkina received as a guest. STUB: never raised.
  kGift,           ///< Zhernova's answer to a gift. STUB: never raised.

  kDistrictVisitKindCount,
};

/// @brief What a visit found. Seam words `none`, `discrepancy`, `junior_miss`.
enum class DistrictVisitFinding : std::uint8_t {
  kNone = 0,     ///< Nothing.
  kDiscrepancy,  ///< The stores above the accumulation limit (InspectVisit, since 2026-09-18).
  kJuniorMiss,   ///< A senior found what the junior of the channel missed. STUB: never computed.

  kDistrictVisitFindingCount,
};

/// @brief The outcome of a gift visit. Seam words: empty, `accepted`,
/// `returned`.
enum class DistrictGiftOutcome : std::uint8_t {
  kNone = 0,  ///< Not a gift visit.
  kAccepted,
  kReturned,

  kDistrictGiftOutcomeCount,
};

/// @brief Why a visit was called — what the design names as its cause. Not a
/// seam word today (the host's form does not carry it: "какой сигнал его
/// вызвал, форма не несёт", 93 §4.1); kept so the four STUB causes have a place
/// to land. Stored in a save: append only.
enum class DistrictVisitCause : std::uint8_t {
  kSchedule = 0,  ///< A regular visit of the plan.
  kPlanFailed,    ///< The economic year closed kFailed.
  kJuniorSignal,  ///< A regular visit found something; the senior comes.
  kComplaint,     ///< Residents complained upward. STUB: never raised.
  kEmergency,     ///< A fire, a loss of herd. STUB: never raised.
  kSatire,        ///< A satire in the district paper. STUB: never raised.
  kDenunciation,  ///< A denunciation. STUB: never raised.

  kDistrictVisitCauseCount,
};

/// @brief A visit's outcome as the event carries it: every seam field of the
/// form, packed into SimEvent::amount by PackDistrictVisit.
struct DistrictVisitOutcome {
  DistrictFace face = DistrictFace::kKorenev;
  DistrictVisitKind kind = DistrictVisitKind::kRegular;
  DistrictVisitFinding found = DistrictVisitFinding::kNone;

  /// Whose miss a kJuniorMiss is; meaningful only then, and `has_miss_by`
  /// says whether the field names anybody (the seam's empty `miss_by`).
  DistrictFace miss_by = DistrictFace::kKorenev;
  bool has_miss_by = false;

  DistrictGiftOutcome gift = DistrictGiftOutcome::kNone;
};

/// @brief One visit announced or called and not arrived yet.
struct DistrictVisitRow {
  /// The campaign day the visit arrives, at the day's first tick. A regular
  /// visit's row is made `district_visit_notice_days` before it; an
  /// extraordinary visit's the day before, and announces nothing.
  std::uint32_t arrive_day = 0;

  DistrictFace face = DistrictFace::kKorenev;
  DistrictVisitKind kind = DistrictVisitKind::kRegular;
  DistrictVisitCause cause = DistrictVisitCause::kSchedule;
};

/// @brief Every visit on its way, in the order it was announced or called.
using DistrictVisitTable = StateTable<DistrictVisitId, DistrictVisitRow>;

/// @brief Packs an outcome into SimEvent::amount, one byte a field from the
/// lowest: face, kind, found, miss_by (0 = empty, else face + 1), gift. A
/// consumer reads the bytes, never the whole number.
std::int64_t PackDistrictVisit(const DistrictVisitOutcome& outcome);

/// @brief The inverse of PackDistrictVisit.
/// @return false, leaving `outcome` untouched, when a byte holds a value past
///         its enum's count — an amount that is not a visit's.
bool UnpackDistrictVisit(std::int64_t amount, DistrictVisitOutcome& outcome);

}  // namespace core

#endif  // CORE_COMMON_DISTRICT_VISIT_STATE_H_
