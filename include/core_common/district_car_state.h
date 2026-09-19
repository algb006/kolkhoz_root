/// @file
/// @brief The district's cars that come by the road for somebody of the
/// village — the ambulance, and the police car when it has a cause (health
/// «Скорая помощь из района», register 236; crime §10 «Машина милиции ждёт
/// повода», register 237; boss seq 210).
/// @threading SINGLE_THREADED
/// Written by the production sub-step of the decisions slot (phase 3) on the
/// sim thread; read by the presentation through the completed world.
///
/// NO CALL, AND THAT IS THE DESIGN'S CONVENTION (the human's word of
/// 2026-09-19: «Механику вызова машины милиции и скорой помощи не делаем…
/// в районе об этом как то быстро узнали»): the district learns by itself.
/// A car comes by the road from the district's border (`district_road_mark`
/// of tables/roads.csv, STUB), stops at the patient's house, carries him out
/// and goes back; he is then AWAY (ResidentRow::away_*) until he returns —
/// with the milk cart in its season, else on foot from the border.
///
/// ONE ROW FOR BOTH KINDS, and only the ambulance goes out today: the core
/// has no grave offence with a culprit (boss seq 210, 1).

#ifndef CORE_COMMON_DISTRICT_CAR_STATE_H_
#define CORE_COMMON_DISTRICT_CAR_STATE_H_

#include <cstdint>

#include "core_common/ids.h"
#include "core_common/state_table.h"

namespace core {

/// @brief Which of the district's cars.
enum class DistrictCarKind : std::uint8_t {
  kAmbulance = 0,  ///< For a resident whose health fell below the line.
  kPolice,         ///< For an offender; no cause in the core yet — STUB.

  /// NOT A KIND: the count, for the mirrors.
  kDistrictCarKindCount,
};

/// @brief Where a car is in its errand.
enum class DistrictCarPhase : std::uint8_t {
  /// Sent, and not on the road yet: a blizzard holds it in the district; it
  /// sets out the first passable morning.
  kWaiting = 0,
  kOnTheRoad,  ///< Coming from the border; at the house at `arrive_tick`.
  kAtTheYard,  ///< Standing at the house; the patient is carried out.

  /// NOT A PHASE: the count, for the mirrors.
  kDistrictCarPhaseCount,
};

/// @brief Why a resident is away in the district
/// (ResidentRow::away_reason).
enum class AwayReason : std::uint8_t {
  kNone = 0,       ///< Not away.
  kHospital,       ///< «лечится · районная больница».
  kInvestigation,  ///< «в районе · под следствием» — with the police car, STUB.

  /// NOT A REASON: the count, for the mirrors.
  kAwayReasonCount,
};

/// @brief One car on its errand. The row goes when the car leaves the house
/// with the patient; from then on the patient's own row says where he is.
struct DistrictCarRow {
  DistrictCarKind kind = DistrictCarKind::kAmbulance;
  DistrictCarPhase phase = DistrictCarPhase::kWaiting;

  /// Who it comes for.
  ResidentId resident;

  /// The tick it stands at the house: set when it sets out — the next
  /// morning after it was sent in dry weather, twice as long in the mud.
  std::uint64_t arrive_tick = 0;

  /// The tick it leaves the house with the patient.
  std::uint64_t leave_tick = 0;
};

/// @brief Every car on its errand, in the order sent.
using DistrictCarTable = StateTable<DistrictCarId, DistrictCarRow>;

}  // namespace core

#endif  // CORE_COMMON_DISTRICT_CAR_STATE_H_
